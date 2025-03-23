/*
 You can use this however you want.
 */
 #ifndef __CACHE_STUDENT_H__844
 
 #define __CACHE_STUDENT_H__844

 #include "steque.h"
 #include "shm_channel.h"
 #include <stddef.h>
 #include <curl/curl.h> 

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
 * This function will create a private POSIX message queue for the worker thread on the proxy.
 */
int create_private_queue();

/**
 * This function will destroy the POSIX message queue created by the worker thread on the proxy.
 */
int destroy_private_queue();


/**
 * This returns an atomic integer which is used for creating unique private message queues.
 */
int atomic_int();

void cleanup(CURL *curl, char **full_path, BuffStruct *bufferStruct);
 #endif // __CACHE_STUDENT_H__844