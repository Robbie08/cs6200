#include <stdio.h>
#include <unistd.h>
#include <printf.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include "cache-student.h"
#include "shm_channel.h"
#include "simplecache.h"
#include "gfserver.h"

// CACHE_FAILURE
#if !defined(CACHE_FAILURE)
    #define CACHE_FAILURE (-1)
#endif 

#define MAX_CACHE_REQUEST_LEN 6100
#define MAX_SIMPLE_CACHE_QUEUE_SIZE 782  

unsigned long int cache_delay;
worker_pool_t worker_pool;
int nthreads;

static void _sig_handler(int signo){
	if (signo == SIGTERM || signo == SIGINT){
		// This is where your IPC clean up should occur
		cleanup_threading(nthreads);
		destroy_delegate_pool();
		simplecache_destroy();
		exit(signo);
	}
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default is 8, Range is 1-100)\n"      \
"  -d [delay]          Delay in simplecache_get (Default is 0, Range is 0-2500000 (microseconds)\n "	\
"  -h                  Show this help message\n"

//OPTIONS
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
  {"delay", 			 required_argument,		 NULL, 			 'd'}, // delay.
  {NULL,                 0,                      NULL,             0}
};

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

int main(int argc, char **argv) {
	nthreads = 8;
	char *cachedir = "locals.txt";
	char option_char;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "d:ic:hlt:x", gLongOptions, NULL)) != -1) {
		switch (option_char) {
			default:
				Usage();
				exit(1);
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;				
			case 'h': // help
				Usage();
				exit(0);
				break;    
            case 'c': //cache directory
				cachedir = optarg;
				break;
            case 'd':
				cache_delay = (unsigned long int) atoi(optarg);
				break;
			case 'i': // server side usage
			case 'o': // do not modify
			case 'a': // experimental
				break;
		}
	}

	if (cache_delay > 2500000) {
		fprintf(stderr, "Cache delay must be less than 2500000 (us)\n");
		exit(__LINE__);
	}

	if ((nthreads>100) || (nthreads < 1)) {
		fprintf(stderr, "Invalid number of threads must be in between 1-100\n");
		exit(__LINE__);
	}
	if (SIG_ERR == signal(SIGINT, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(CACHE_FAILURE);
	}
	if (SIG_ERR == signal(SIGTERM, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(CACHE_FAILURE);
	}

	int err;
	err = init_worker_pool(nthreads);
	if (err != 0) {
		perror("simplecached: failed to init the worker pool");
		exit(1);
	}

	err = init_threads(nthreads);
	if (err != 0) {
		perror("simplecached: failed to init the worker threads");
		exit(1);
	}

	/*Initialize cache*/
	simplecache_init(cachedir);

	// Cache should go here

	// Keep on reading from the MQ and delegate the 
	// requests to the worker threads.
	for(;;) {
		cache_request_t *request = malloc(sizeof(cache_request_t)); // TODO: remember to clean this up
		if (!request) {
			perror("simplecached: malloc failed");
			continue;
		}

		int bytes_recv = mq_receive(MQ_NAME, (char *)request, sizeof(cache_request_t), NULL);
		if (bytes_recv < 0) {
			perror("simplecached: mq_receive failed");
			continue;
		}

		// Publish the request to the steque 
		pthread_mutex_lock(&worker_pool.q_lock);
		steque_enqueue(&worker_pool.q_request, request);
		pthread_cond_signal(&worker_pool.q_not_empty);
		pthread_mutex_unlock(&worker_pool.q_lock);
	}

	// Line never reached
	return -1;
}

int init_worker_pool(size_t numOfDelegates) {
	int err = 0;
	steque_init(&worker_pool.q_request); // init our queue for the request queue within our delegate pool object
	err = pthread_mutex_init(&worker_pool.q_lock, NULL); // we must init our lock for the queue
	if (err != 0) {
		perror("simplecached: failed to initialize q_lock mutex");
		return -1;
	}

	err = pthread_cond_init(&worker_pool.q_not_empty, NULL); // we must init our conditional variable 
	if (err != 0) {
		perror("simplecached: failed to initialize q_not_empty condition variable");
		return -1;
	}

	// printf("successfully initialized delegate pool\n");
	return 0;
}

int init_threads(size_t num_threads) {
	for (int i = 0; i < num_threads; i++) {
		// we want the delegate threads to be joinable to the delegator thread
		int err = pthread_create(&worker_pool.pool[i], NULL, worker_process, NULL);
		if (err != 0) {
			perror("simplecached: pthread_create failed to create worker thread");
			return -1;
		}
	}
	return 0;
}

void * worker_process(void *args) {
	for(;;) {
		pthread_mutex_lock(&worker_pool.q_lock);

		// Wait until the signal is sent to process requests
		while(steque_isempty(&worker_pool.q_request)) {
			pthread_cond_wait(&worker_pool.q_not_empty, &worker_pool.q_lock);
		}

		cache_request_t *req = steque_pop(&worker_pool.q_request);
		pthread_mutex_unlock(&worker_pool.q_lock);

		// load our shm_file object for the segment that the proxy sent us
		shm_file_t *shm_file = (shm_file_t *)((char *)ipc_chan.shm_base + req->shm_offset);
		memset(shm_file, 0, sizeof(shm_file_t));

		int file_fd = simplecache_get(req->file_name);
		if (file_fd == -1) {
			// This indicates a CACHE_MISS so we can just update the shm_file object
			// reference in shared memory to CACHE_MISS and update the semaphore
			shm_file->response_type = CACHE_MISS;
			sem_post(&shm_file->chunk_ready_sem); // wake up proxy
			free(req);
			continue;
		}

		// This indicates a CACHE_HIT so we need to send the file in chunks to the shard memory
		int err = send_file_to_shm(shm_file, file_fd, req);
		if (err == -1) {
			perror("simplecached send_file_to_shm failed");
		}
	}
}

int send_file_to_shm(shm_file_t *shm_file, int file_fd, cache_request_t *req) {
	struct stat statbuff; // we want to store file info so that we can get the file size
	if (fstat(file_fd, &statbuff) == -1) {
		perror("simplecached fstat failed");
		shm_file->response_type = CACHE_MISS;
		sem_post(&shm_file->chunk_ready_sem);
		free(req);
		return -1;
	}

	shm_file->response_type = CACHE_HIT;
	shm_file->file_size = statbuff.st_size;
	shm_file->total_size = statbuff.st_size;
	shm_file->is_done = 0;
	shm_file->is_valid = 1;

	char buffer[CHUNK_SIZE];
	size_t total_bytes_sent = 0;
	size_t bytes_read = 0;

	sem_post(&shm_file->chunk_ready_sem); // Wake up our proxy
	while((bytes_read = read(file_fd, buffer, CHUNK_SIZE)) > 0) {
		memcpy(shm_file->data, buffer, bytes_read);
		shm_file->chunk_size = bytes_read; 
		sem_post(&shm_file->chunk_ready_sem); // let proxy know there is chunks to read

		total_bytes_sent += bytes_read;
		// We will keep looping until the proxy resets the chunk size to 0
		while(shm_file->chunk_size != 0) {
			usleep(200);
		}
	}

	if (bytes_read == 0) {
		perror("simplecached: read 0 bytes from file");
	}
	shm_file->is_done = 1;
	sem_post(&shm_file->chunk_ready_sem); // notify proxy that we're done
	free(req);
	return 0;
}

void cleanup_threading(int nthreads) {
	for (size_t i = 0; i < nthreads; i++) {
	  int err = pthread_join(worker_pool.pool[i], NULL);
	  if (err != 0) {
		  fprintf(stderr, "Error joining thread %zu: %s\n", i, strerror(err));
	  }
	  // printf("Successfully joined thread %zu\n", i);
	  
	}
}

void destroy_delegate_pool() {
	steque_destroy(&worker_pool.q_request);
	pthread_mutex_destroy(&worker_pool.q_lock);
	pthread_cond_destroy(&worker_pool.q_not_empty);
	// printf("Successfully destroyed delegate pool");
}  
  

int pmq_publish_response(cache_response_t *resp, mqd_t pmq_fd) {
	int err = mq_send(pmq_fd, (char *)resp, sizeof(cache_response_t), 0);
	if (err == -1) {
		perror("mq_send");
		return -1;
	}
	return 0;
}