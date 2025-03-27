/*
 You can use this however you want.
 */
 #ifndef __CACHE_STUDENT_H__844
 
 #define __CACHE_STUDENT_H__844

 #include "steque.h"
 #include "shm_channel.h"
 #include <stddef.h>
 #include <curl/curl.h> 

 #define MAX_WORKERS 64
 #define CHUNK_SIZE 8192

 #define SHM_NAME "/shm_cache_rortiz"
 #define SEM_NAME "/sem_cache_rortiz"
 #define MQ_NAME "/mq_cache_rortiz"


 /**
  * This struct encapsulates the variables for the worker pool. This
  * struct was influenced by my implementation of the boss worker
  * patter from pr1 part 2.
  */
 typedef struct {
	pthread_t pool[MAX_WORKERS];
	steque_t q_request;
	pthread_mutex_t q_lock;
	pthread_cond_t q_not_empty;
	int completed;
 } worker_pool_t;

 /**
 * Structure to hold the buffer and its size. This struct gets passed into the write_callback
 * function as the userdata parameter. The write_callback function will then use this struct
 * to store the buffer and its size.
 * 
 * Credit: I got a good portion of this code from the example code in the curl documentation:
 * https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
typedef struct {
	char *data; 	// ptr to the dynamically allocated buffer for response data
	size_t size; 	// current buff suize
} BuffStruct; 

size_t write_callback(void *data_ptr, size_t size, size_t nmemb, void *userdata);

char *get_full_url(const char *path, const char *server);

/**
 * This function initializes the delegate pool which includes the
 * queue, queue mutex, and queue conditional variable. 
 * 
 * I used this function from my implementation
 * of pr1 part2 from this semester.
 */
int init_worker_pool(size_t numOfDelegates);

/**
 * This function creates the delegate threads and adds them to
 * the delegate pool. 
 * 
 * I used this function from my implementation
 * of pr1 part2 from this semester.
 * 
 * I found this resource pretty useful for this function:
 * https://hpc-tutorials.llnl.gov/posix/joining_and_detaching/
 */
int init_threads(size_t numthreads);

/**
 * This function cleans up threads created by joining them back.ACCESSPERMS
 * I used this function from my implementation
 * of pr1 part2 from this semester.
 * 
 */
void cleanup_threading(int nthreads);

/**
 * Destroys the delegate pool created
 * I used this function from my implementation
 * of pr1 part2 from this semester.
 */
void destroy_delegate_pool();


/**
 * This handler performs the work that each cache daemon must perform.
 */
void * worker_process(void *args);


/**
 * This is a helper function created to send the contents of the file read
 * from shared memory buffer to the client.
 */
int send_file_to_shm(shm_file_t *shm_file, int file_fd, cache_request_t *req);

void cleanup(CURL *curl, char **full_path, BuffStruct *bufferStruct);
 #endif // __CACHE_STUDENT_H__844