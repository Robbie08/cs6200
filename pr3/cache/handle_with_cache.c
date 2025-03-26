#include "gfserver.h"
#include "cache-student.h"

#define BUFSIZE (834)

int queue_counter = 0;
pthread_mutex_t queue_counter_lock = PTHREAD_MUTEX_INITIALIZER;

ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg) {
	(void) ctx;
	const char *server = (const char *)arg;
	(void) path;
	errno = ENOSYS;

	// Create a private message queue for the worker thread
	size_t q_name_size = 64;
	char q_name[q_name_size];
	mqd_t pqm_fd;
	int err = create_private_queue(q_name, &pqm_fd, q_name_size);
	if (err == -1) {
		perror("server: create_private_queue failed");
		return -1;
	}

	ssize_t shm_offset = shm_channel_acquire_segment();
	if (shm_offset == -1) {
		fprintf(stderr, "Failed to acquire shared memory segment\n");
		destroy_private_queue(q_name, pqm_fd);
		return -1;
	}

	// Maps shm_file_t object into shared memory starting at the offset.
	shm_file_t *shm_file = (shm_file_t *)((char *)ipc_chan.shm_base + shm_offset);
	memset(shm_file, 0, sizeof(shm_file_t));

	// Init the unnamed semaphore with pshared=1 to register the sem internally by
	// the kernel as a shared process semaphore so that the cache can use it. 
	// This lives in the shared memory region
	// Larned more about it through the discussion here: 
	// https://stackoverflow.com/questions/13145885/name-and-unnamed-semaphore
	if (sem_init(&shm_file->chunk_ready_sem, 1, 0) == -1) {
		perror("sem_init failed for shm_file struct");
		shm_channel_release_segment(shm_offset);
		destroy_private_queue(q_name, pqm_fd);
		return -1;
	}

	// Create Request for to publish to the MQ. We need to send the private MQ fd
	// to the cache so it can send the response back to the worker thread.
	cache_request_t request = {
		.request_type = CACHE_READ,
		.private_mq_fd = pqm_fd,
		.shm_offset = shm_offset
	};

	// copy the path of the file to the request.file_name
	strncpy(request.file_name, path, MAX_FILENAME_LEN);

	// Publish the request to the cache
	err = mq_publish_request(&request);
	if (err == -1) {
		perror("server: mq_publish_request failed");
		destroy_private_queue(q_name, pqm_fd);
		shm_channel_release_segment(shm_offset);
		return -1;
	}

	// Consume the response from the cache
	cache_response_t response;
	err = pmq_consume_request(&response, pqm_fd); // This will block until the cache responds to the private queue
	if (err == -1) {
		perror("server: pmq_consume_request failed");
		destroy_private_queue(q_name, pqm_fd);
		shm_channel_release_segment(shm_offset);
		return -1;
	}

	// If we get a cache hit then we can just read the file from shared memory
	// and send it to the client
	if (response.response_type == CACHE_HIT) {
		gfs_sendheader(ctx, GF_OK, response.file_size);
		// TODO: Read the file from shared memory using the offset in the response
		// TODO: Respond to the client with the file

		size_t total_sent = 0;
		while(1) {
			if (sem_wait(&shm_file->chunk_ready_sem) == -1) {
				perror("sem_wait: failed while reading from shared memory");
				destroy_private_queue(q_name, pqm_fd);
				shm_channel_release_segment(shm_offset);
				return -1;
			}

			// The cache daemon worker will update the chunk_size based what it's sending
			if (shm_file->chunk_size > 0) {
				ssize_t bytes_sent = gfs_send(ctx, shm_file->data, shm_file->chunk_size);
				if (bytes_sent < 0) {
					fprintf(stderr, "Error while sending chunk to client\n");
					destroy_private_queue(q_name, pqm_fd);
					shm_channel_release_segment(shm_offset);
					return -1;
				}
				total_sent += bytes_sent;
				shm_file->chunk_size = 0; // reset chunk_size for daemon worker
			}
			
			// The cache daemon worker will update is_done flag to true when done sending all contents
			if (shm_file->is_done && total_sent >= shm_file->total_size) {
				break;
			}
		}

		sem_destroy(&shm_file->chunk_ready_sem); // avoid lingering semaphores
		destroy_private_queue(q_name, pqm_fd);
		shm_channel_release_segment(shm_offset);
		return total_sent;
	}

	// If the file isn't found on the cache then request it from the server
	CURL *curl = curl_easy_init(); 
	if (curl == NULL) {
		perror("server: curl_easy_init");
		return -1;
	}

	char *full_path = get_full_url(path, server);
	if (full_path == NULL) {
		perror("server: get_full_url failed");
		curl_easy_cleanup(curl);
		return -1;
	}

	printf("server: full_path: %s\n", full_path);
	curl_easy_setopt(curl, CURLOPT_URL, (const char *)full_path); // must define the path otherwise no trasnfer will occur
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L); // timeout after 5 seconds if can't perform successful TCP handshake
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects if necessary using the ALL flag
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L); // Allow up to 5 redirects to avoid infinite loops or long chain of redirects
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS); // Only allow redirects to HTTPS servers
	
	// Let's just create our own callback function for writing data
	BuffStruct bufferStruct = {NULL, 0};
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bufferStruct); // Allows the write_callback to write to the BufferStruct
	
	CURLcode res;
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		// clean the allocated memory
		fprintf(stderr, "server: curl_easy_perform returned unexpected error: %s\n", curl_easy_strerror(res));
		gfs_sendheader(ctx, GF_ERROR, 0);
		cleanup(curl, &full_path, &bufferStruct);
		return -1;
	}

	// Lets get the http code from the response
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	if (http_code == 404 || http_code == 403) {
		// If we couldn't find the file then let's send a 404 error to the client
		fprintf(stderr, "server: curl_easy_perform returned 404 or 403 error... Responding to client with 'GF_FILE_NOT_FOUND' status.\n");
		gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		cleanup(curl, &full_path, &bufferStruct);
		return -1;
	} else if (http_code >= 400) {
		// For any other error 4xx and 5xx errors lets return a GF_ERROR
		fprintf(stderr, "server: curl_easy_perform returned the error code: %ld\n", http_code);
		gfs_sendheader(ctx, GF_ERROR, 0);
		cleanup(curl, &full_path, &bufferStruct);
		return -1;
	}

	// Send the GETFILE response
	printf("server: responding to client with 'GF_OK' status\n");
	// If we get here then we have a successful response so just return GF_OK header and then data
	gfs_sendheader(ctx, GF_OK, bufferStruct.size); // Send the buffer size to the client
	gfs_send(ctx, bufferStruct.data, bufferStruct.size); // Send the actual data to the client

	// clean the allocated memory
	size_t total_size = bufferStruct.size;
	cleanup(curl, &full_path, &bufferStruct);
	return total_size;	// need to return the file size here
}

/**
 * Callback function for writing data from the curl request and return the
 * total size of the data written. This function matches the prototype of
 * the CURLOPT_WRITEFUNCTION option for curl_easy_setopt and uses the userdata
 * parameter to store the buffer and its size as specified by the setopt function.
 * 
 * Credit: I got a good portion of this code from the example function cb in the
 * curl documentation: https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
size_t write_callback(void *data_ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb; // Calculate the actual size that will be written
    BuffStruct *buff = (BuffStruct *)userdata;

    char *temp_buff = realloc(buff->data, buff->size + total_size + 1); // Allocate or reallocate memory for the buffer
    if (temp_buff == NULL) {
        fprintf(stderr, "server: realloc failed to allocate memory\n");
        return 0; // Return 0 to signal an error
    }

    buff->data = temp_buff; // Update the buffer pointer
    memcpy(&buff->data[buff->size], data_ptr, total_size); // Copy the new data to the buffer
    buff->size += total_size; // Update the buffer size
    buff->data[buff->size] = '\0'; // Null-terminate the buffer

    return total_size;
}

/**
 * This function will concatenate the base url with the path to the file to get the full url
 * to the file. This function allocates memory for the full url.
 */
char *get_full_url(const char *path, const char *server) {
    size_t url_len = strlen(server) + strlen(path) + 1;
    char *url = malloc(url_len);
    if (url == NULL) {
        perror("server: malloc failed to allocate memory");
        return NULL;
    }

    // The following will concatenate the base_url and the path
    snprintf(url, url_len, "%s%s", server, path);
    return url;
}

/**
 * This function will clean up the allocated memory for the full path and the buffer struct
 * and call the curl_easy_cleanup function to clean up the curl handle.
 */
void cleanup(CURL *curl, char **full_path, BuffStruct *bufferStruct) {
    if (*full_path != NULL) {
        free(*full_path);
        *full_path = NULL;
    }

    if (bufferStruct->data != NULL) {
        free(bufferStruct->data);
        bufferStruct->data = NULL;
    }

    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
}

/*
 __.__
Replace with your implementation
 __.__
*/
ssize_t handle_with_cache_old(gfcontext_t *ctx, const char *path, void* arg){
	size_t file_len;
    size_t bytes_transferred;
	char *data_dir = arg;
	ssize_t read_len;
    ssize_t write_len;
	char buffer[BUFSIZE];
	int fildes;
	struct stat statbuf;

	strncpy(buffer,data_dir, BUFSIZE);
	strncat(buffer,path, BUFSIZE);

	if( 0 > (fildes = open(buffer, O_RDONLY))){
		if (errno == ENOENT)
			//If the file just wasn't found, then send FILE_NOT_FOUND code
			return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		else
			//Otherwise, it must have been a server error. gfserver library will handle
			return SERVER_FAILURE;
	}

	//Calculating the file size
	if (fstat(fildes, &statbuf) < 0) {
		return SERVER_FAILURE;
	}
	file_len = (size_t) statbuf.st_size;
	///

	gfs_sendheader(ctx, GF_OK, file_len);

	//Sending the file contents chunk by chunk

	bytes_transferred = 0;
	while(bytes_transferred < file_len){
		read_len = read(fildes, buffer, BUFSIZE);
		if (read_len <= 0){
			fprintf(stderr, "handle_with_file read error, %zd, %zu, %zu", read_len, bytes_transferred, file_len );
			return SERVER_FAILURE;
		}
		write_len = gfs_send(ctx, buffer, read_len);
		if (write_len != read_len){
			fprintf(stderr, "handle_with_file write error");
			return SERVER_FAILURE;
		}
		bytes_transferred += write_len;
	}

	return bytes_transferred;


}

int create_private_queue(char *q_name, mqd_t *mq_fd, size_t len) {
	if (q_name == NULL || mq_fd == NULL) {
		return -1;
	}

	snprintf(q_name, len, "/mq_private_rortiz_%d", atomic_int());
	
	struct mq_attr attr = {
		.mq_flags = 0,
		.mq_maxmsg = 10,
		.mq_msgsize = sizeof(cache_request_t),
		.mq_curmsgs = 0
	};

	*mq_fd = mq_open(q_name, O_CREAT | O_RDWR, 0666, &attr);
	if (*mq_fd == (mqd_t)-1) {
		perror("mq_open failed in create_private_queue");
		return -1;
	}
	return 0;
}

int destroy_private_queue(const char *q_name, mqd_t mq_fd) {
	if (q_name == NULL || mq_fd == (mqd_t)-1) {
		return -1;
	}

	if (mq_close(mq_fd) == -1) {
		perror("mq_close failed in destroy_private_queue");
		return -1;
	}

	if (mq_unlink(q_name) == -1) {
		perror("mq_unlink failed in destroy_private_queue");
		return -1;
	}
	return 0;
}

int atomic_int() {
	int count;
	pthread_mutex_lock(&queue_counter_lock);
	count = queue_counter++;
	pthread_mutex_unlock(&queue_counter_lock);
	return count;
}