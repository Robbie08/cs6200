/*
 Optional file to help with organization 
 */
 #ifndef __SERVER_STUDENT_H__846
 #define __SERVER_STUDENT_H__846
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
	char *data; 	// ptr to the dynamically allocated buffer
	size_t size; 	// current buff suize
} BuffStruct; 

extern CURL *curl;
extern char *full_path;
extern BuffStruct bufferStruct;

size_t write_callback(void *data_ptr, size_t size, size_t nmemb, void *userdata);

char * get_full_url(const char *path);

void cleanup(CURL *curl, char **full_path, BuffStruct *bufferStruct);

//  void signal_handler(int signum);
 
 #endif // __SERVER_STUDENT_H__846