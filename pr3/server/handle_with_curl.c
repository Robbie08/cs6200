#include "proxy-student.h"
#include "gfserver.h"

#define MAX_REQUEST_N 512
#define BUFSIZE (6226)

ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void* arg) {
	(void) ctx;
	(void) arg;
	(void) path;
	errno = ENOSYS;

	CURL *curl = curl_easy_init(); 
	if (curl == NULL) {
		perror("server: curl_easy_init");
		return -1;
	}

	char *full_path = get_full_url(path);
	if (full_path == NULL) {
		perror("server: get_full_url failed");
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, (const char *)full_path); // must define the path otherwise no trasnfer will occur
	curl_easy_setopt(curl, CURLOPT_ACCEPTTIMEOUT_MS, 5000L); // Don't wait longer than 5 seconds for FTP server to respond
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects if necessary using the ALL flag
	// curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // Fail on HTTP 4xx or 5xx errors so maybe we don't want this.

	BuffStruct bufferStruct = {NULL, 0}; // Initialize the buffer struct

	// Let's just create our own callback function for writing data
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

	if (http_code == 404) {
		// If we couldn't find the file then let's send a 404 error to the client
		fprintf(stderr, "server: curl_easy_perform returned 404 error\n");
		gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		cleanup(curl, &full_path, &bufferStruct);
		return -1;
	} else if (http_code >= 400) {
		// For any other error 4xx and 5xx errors lets return a GF_ERROR
		fprintf(stderr, "server: curl_easy_perform returned unexpected http code: %ld\n", http_code);
		gfs_sendheader(ctx, GF_ERROR, 0);
		cleanup(curl, &full_path, &bufferStruct);
		return -1;
	}

	// If we get here then we have a successful response so just return GF_OK header and then data
	gfs_sendheader(ctx, GF_OK, bufferStruct.size); // Send the buffer size to the client
	gfs_send(ctx, bufferStruct.data, bufferStruct.size); // Send the actual data to the client

	// clean the allocated memory
	size_t total_size = bufferStruct.size;
	cleanup(curl, &full_path, &bufferStruct);
	return total_size;	// need to return the file size here
}

/**
 * Callback function for writing data from the curl request adn return the
 * total size of the data written. This function matches the prototype of
 * the CURLOPT_WRITEFUNCTION option for curl_easy_setopt and uses the userdata
 * parameter to store the buffer and its size as specified by the setopt function.
 * 
 * Credit: I got a good portion of this code from the example function cb in the
 * curl documentation: https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
size_t write_callback(void *data_ptr, size_t size, size_t nmemb, void *userdata) {
	size_t total_size = size * nmemb; // Calc the actual size that will be written
	BuffStruct *buff = (BuffStruct *)userdata;

	char *temp_buff = realloc(buff->data, buff->size + total_size + 1); // allocate or reallocate memory for the buffer
	if (temp_buff == NULL) {
		fprintf(stderr, "server: realloc failed to allocate memory\n");
		return 0; // return 0 to signal an error
	}

	buff->data = temp_buff; // update the buffer ptr
	memcpy(&buff->data[buff->size], data_ptr, total_size); // copy the new data to buffer
	buff->size += total_size; // update the buffer size
	buff->data[buff->size] =  '\0'; // null terminate the buffer

	return total_size;
}

/**
 * This function will concatenate the base url with the path to the file to get the full url
 * to the file. This function allocates memory for the full url.
 */
char * get_full_url(const char *path) {
	char *base_url = "https://raw.githubusercontent.com/gt-cs6200/image_data";
	size_t url_len = strlen(base_url) + strlen(path) + 1;
	char *url = malloc(url_len);
	if (url == NULL) {
		perror("server: malloc failed to allocate memory");
		return NULL;
	}

	// The following will concatenate the base_url and the path
	snprintf(url, url_len, "%s%s", base_url, path);
	printf("server: url: %s\n", url);
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
 * We provide a dummy version of handle_with_file that invokes handle_with_curl as a convenience for linking!
 */
ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg){
	return handle_with_curl(ctx, path, arg);
}	
