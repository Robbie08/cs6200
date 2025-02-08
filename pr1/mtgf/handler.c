#include "gfserver-student.h"
#include "gfserver.h"
#include "workload.h"
#include "content.h"
#include <stdlib.h>

gfserver_delegate_pool_t delegate_pool;


request_t* create_request(gfcontext_t **ctx, const char *path) {
	//printf("boss: Attempting to create request object...\n");
	request_t *request = malloc(sizeof(request_t));
	if (request == NULL) {
		perror("server: failed to allocate memory for the request_t");
		return NULL;
	}

	request->ctx = *ctx;

	// this ensures we keep the exact copy that we received
	// since it's possible that this address itself can get
	// used by another object
	request->path = strdup(path);

	if (request->path == NULL) {
		perror("server: failed to allocate memory for path");
		free(request);
		return NULL;
	} 

	//printf("boss: Handler successfully created request\n");
	return request;
}

void destory_request(request_t *request) {
	//printf("Attempting to destroy request...\n");
	if (request == NULL) {
		return;
	}

	if (request->path != NULL) {
		free(request->path);
	}
	free(request);
	//printf("Successfully destroyed request!\n");
}

//
//  The purpose of this function is to handle a get request
//
//  The ctx is a pointer to the "context" operation and it contains connection state
//  The path is the path being retrieved
//  The arg allows the registration of context that is passed into this routine.
//  Note: you don't need to use arg. The test code uses it in some cases, but
//        not in others.
//
gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void* arg){
	if (ctx == NULL || *ctx == NULL) {
		perror("server: ctx is NULL in the gfs_handler");
		return GF_ERROR; // Maybe look into if we should have this be INVALID?
	}

	if (path == NULL) {
		perror("server: NULL path in the gfs_handler");
		return GF_ERROR;
	}

	request_t *request = create_request(ctx, path);
	if (request == NULL) {
		perror("server: failed to create a request wrapper");
		return GF_ERROR;
	}

	// pseudocode:
	// 	lock(pool.m)
	// 	q.add(*ctx)
	// 	send_signal(pool.queue_is_not_empty); // let's delegates that there are tasks
	// 	unlock(pool.m)

	pthread_mutex_lock(&delegate_pool.q_lock);
	steque_enqueue(&delegate_pool.request_q, request);
	pthread_cond_signal(&delegate_pool.q_not_empty);
	pthread_mutex_unlock(&delegate_pool.q_lock);

	*ctx = NULL; // This is required to avoid dangling pointer from getting reassigned or accessed
	return GF_OK;
}

void* delegate_function(void *args){
	//printf("Thread starting up delegate function.\n");
	for (;;) {
		pthread_mutex_lock(&delegate_pool.q_lock); // Get the mutex so that we can safely add ourselves to the waiting queue

		while(steque_isempty(&delegate_pool.request_q)) {
			// we need to wait in the wait queue and release the queue lock
			//printf("Thread adding itself to the worker queue and releasing lock.\n");
			pthread_cond_wait(&delegate_pool.q_not_empty, &delegate_pool.q_lock);
		}

		//printf("Thread woke up and picking up request from queue.\n");
		request_t *request = (request_t *) steque_pop(&delegate_pool.request_q);
		pthread_mutex_unlock(&delegate_pool.q_lock); // unlock the mutex so that others can continue their flow

		if (request->ctx == NULL) {
            //printf("Warning: ctx is NULL. It may have been freed by gfserver.c.\n");
            destory_request(request);
            continue;
        }

		// Initially I though this was not thread safe so I put a lock here
		// However, wrapping content_get with a mutex was not fully using the
		// power of multithreading
		int fd = content_get(request->path);

		if (fd == -1) {
			perror("server: failed to get file descriptor for the path requested");
			gfs_sendheader(&request->ctx, GF_ERROR, 0);
			destory_request(request);
			continue;
		}
		struct stat f_stats;
		int err = fstat(fd, &f_stats);
		if (err == -1) {
			perror("server: failed to fstat the file descriptor");
			gfs_sendheader(&request->ctx, GF_ERROR, 0);
			destory_request(request);
			continue;
		}

		size_t fileSize = f_stats.st_size;
		gfs_sendheader(&request->ctx, GF_OK, fileSize);
		err = sendFileContents(request, fd);
		if (err == -1) {
			perror("server: failed to sendFileContents");
		}

		destory_request(request);
	}
	return NULL;
}

int init_delegate_pool(size_t numOfDelegates) {
	int err = 0;
	steque_init(&delegate_pool.request_q); // init our queue for the request queue within our delegate pool object
	err = pthread_mutex_init(&delegate_pool.q_lock, NULL); // we must init our lock for the queue
	if (err != 0) {
		perror("serverv: failed to initialize q_lock mutex");
		return -1;
	}

	err = pthread_cond_init(&delegate_pool.q_not_empty, NULL); // we must init our conditional variable 
	if (err != 0) {
		perror("server: failed to initialize q_not_empty condition variable");
		return -1;
	}
	delegate_pool.pool_size = numOfDelegates;

	//printf("successfully initialized delegate pool\n");
	return 0;
}

void init_threads(size_t numthreads) {
	for (int i = 0; i < numthreads; i++) {
		// we want the delegate threads to be joinable to the delegator thread
		int err = pthread_create(&delegate_pool.delegate_pool[i], NULL, delegate_function, NULL);
		if (err != 0) {
			perror("server: pthread_create failed to create delegate thread");
			return;
		}
		//printf("created thread '%d' of '%ld'\n", i+1, numthreads);
	}
}


void cleanup_threads() {
	steque_destroy(&(delegate_pool.request_q));
	pthread_mutex_destroy(&delegate_pool.q_lock);
	pthread_cond_destroy(&delegate_pool.q_not_empty);
}


// This method writes the file's content based on the file's file descriptor and writes them into the
// connection's file descriptor in chunks. It's possible we cannot fit all the contents of the file
// in one network transaction so we need keep sending chunks until we've sent all the file's contents.
int sendFileContents(request_t *request, int filefd) {
	//printf("Attempting to send file conents to the client.\n");
    char buff[CHUNK_SIZE];
    memset(&buff, 0, CHUNK_SIZE);
	off_t offset = 0; 
    ssize_t bytesRead, bytesSent;
	
	// I was using read() since I basically used an almost identical implementation as the warmup
	// however, the read() is not thread safe and therefore was getting issues.Switched to pread()
	// was essential.
    while ((bytesRead = pread(filefd, buff, sizeof(buff), offset)) > 0) {
        char *bufPtr = buff; // Allows me to keep track of the next chunk of bytes I need to send
        ssize_t bytesToSend = bytesRead;
        while(bytesToSend > 0) {
            bytesSent = gfs_send(&request->ctx, bufPtr, bytesToSend);
            if (bytesSent == -1) {
                perror("server: send");
                return -1;
            } else if (bytesSent == 0) {
                perror("server: send failed because the client closed the connection.");
                return -1;
            }
            bytesToSend -= bytesSent; // subtract the bytes we sent from the total bytes we need to send
            bufPtr += bytesSent; // move our bufPtr to the start of next chunk
        }
		offset += bytesRead; // update the offset for next read
    }

    if (bytesRead == -1) {
        perror("server: send");
		return -1;
    }
	//printf("Successfully sent all the file contents\n");
	return 0;
}
