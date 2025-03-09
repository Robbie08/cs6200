# Project PR3
**Professor:** 

**Author**: Roberto Ortiz

**Term:** Spring 2025

## Part 1

### Hive Level Summary:

I had to build a Proxy server. The proxy server takes in a GETFILE requests. The GETFILE requests will simply include the file that the client wants to receive. We can have multiple clients making concurrent requests; however, the multithreading portion is provided for us, we just need to be aware that it's possible for this to happen.

The proxy will then "translate" the GETFILE request into an HTTP request and forward that to the server hosting the files. The client will then respond with an HTTP response which includes headers but mainly we're looking for the http status code and the file in the succeess scenario. If the request succeeded (HTTP 200 status) then we simply respond back to the client with the GETFILE header and then send the file returned by the server. All other unsuccessful sceanrios we'll respond accordingly. 


### Design Choices to highlight
#### Choice of using libcurl-easy
I chose to go with `libcurl-easy` because of a couple of reasons:
1. It was the simplest to work and since the main code handles the multithreading through the boss worker modle we didn't need something fancy like the `libcurl-multi`.
2. Each thread was going to fetch 1 file at a time so blocking behavior for each thread was not a problem.
3. URL managment was simple enough where I'm just concatonating two strings(relatively small in size) so no need to use `libcurl-url`.


#### Choice of libcurl options
```c
curl_easy_setopt(curl, CURLOPT_URL, (const char *)full_path);
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L); 
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bufferStruct);
```
`CURLOPT_URL`:
- Required to know who the target server is

`CURLOPT_CONNECTTIMEOUT_MS, 5000L`: 
- I want to avoid the proxy server's worker threads from getting stuck waiting for servers that might be down or not accepting connections

`CURLOPT_FOLLOWLOCATION, 1L`
- Allows redirects with the ALL flag since we might be hitting some load balancer or CDN amd simplifies code on my end from having to perform redirect logic

`CURLOPT_MAXREDIRS, 5L`:
- Since I'm allowing redirects, I want to ensure we don't end up in some infinite loop or long chain of redirects. If a worker thread gets more than 5 redirects then we'll just terminate the transaction.

`CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS`:
- This was more for security, since I don't want to potentially be redirected to other servers with unsafe protocols. E.g. HTTPS -> HTTP, HTTP -> FTP, etc...

`CURLOPT_WRITEFUNCTION` & `CURLOPT_WRITEDATA`:
- Because I wanted to be able to use my own write_callback method to allow for more flexibility and simplicity on the implementation side. You can read more about that below.

#### Choice of using a Dynamic Buffer for Url Construction and HTTP Response Storage
In my implementation I chose to dynamically allocate memory for both the construction of the URL and the storage of response data from the server rather than using fixed buffers.

##### Dynamic Buffer for URL Construction
A URL consisted of `server` + `path` (e.g., `"http://localhost:8080"` + `"/master/some_file.jpg"` ->  `"http://localhost:8080/master/some_file.jpg"`).

Instead of setting a fixed size to the buffer I allocated the memory dynamically:
```c
char *get_full_url(const char *path, const char *server) {
	size_t url_len = strlen(server) + strlen(path) + 1;
	char *url = malloc(url_len);
	if (url == NULL) {
		perror("server: malloc failed to allocate memory");
		return NULL;
	}

	// The following will concatenate the base_url and the path
	snprintf(url, url_len, "%s%s", server, path);
	// printf("server: url: %s\n", url);
	return url;
}
```

##### Dynamic Buffer for HTTP Response Storage
An HTTP response can vary in size depending on the size of the file we're requesting on behalf of the client (e.g., Some files can be 1Kb and some files could be +30MB)

I created a custom data structure `BuffStruct` that allows us to keep track of buffer content and size of the content.
```c
typedef struct {
	char *data; 	// ptr to the dynamically allocated buffer for response data
	size_t size; 	// current buff suize
} BuffStruct; 
```

The buffer `data` grows dynamically using `realloc()`. I chose to use `realloc()` since this callback method could get called many times since the full data transfer might not happen in one shot.
I got the bulk of the code for this method from the example in the curl.se docs [https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html) and for the most part it's similar with variations me using a custom struct and handling error differently.
```c
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
```

Instead of using a fixed-size static buffer, I opted for dynamic allocation to prioritize scalability and fault tolerance:
1. Prevents buffer overflows: 
    - If I made the static buffer too small which could lead to server crashing or truncating the URL or data.
2. Avoids wasting memory:
    - If I created a static buffer too large in cases where most URLs or HTTP responses are generally short.
3. Scalability and Flexibility:
    - Handles arbitrary URL lengths and HTTP responses without having to worry about static constraints.
4. Negligble Overhead from `malloc()` and `realloc()`:
    - The `malloc()` and `realloc()` overhead is negligable even in scenarios where there are high volumes of requests in which case network latency will be more of an issue.


##### Choice of Using a Custom Write Callback for Dynamic Response Handling
I noticed we have the `curl_easy_setopt()` and `CURLOPT_WRITEFUNCTION` capabilities when using `libcurl` that allows me to pass my own custom call back method.

```c
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
```

The `write_callback()` method allows our proxy to be as scalable and fault tollarant as possible:
1. Flexible and Fault Tollarant:
    - Allows me to handle files of varying sizes without worrying about crashes or data tructation and I can update this write_callback method in the future.
2. Simplicity:
    - A bulk of the work gets taken care of by the `curl_easy_perform()` since it keeps on calling my callback method and allows me to easily track the size and memory usage.
3. Scalability and Performance:
    - No need to deal with I/O bottlenecks of using a temporary file approach and don't have to deal with complex logic.


### Testing

#### Basic Manual Testing
1. Ran `./webproxy` on one shell
2. Ran `./gfclient_download -p 16642` on a seperate shell.
3. I added some dummy values in the `workload.txt` to see if we handle the `HTTP 404` gracefully.

This downloaded all the files from `workload.txt` in the `master/` directory. 
![alt text](image-2.png)

#### Created Test Server for testing

I created a Golang server that would return HTTP status codes in a round robin manner. This was to ensure that my proxy server would handle the HTTP responses correctly and respond to clients with correct GETFILE status.

Here is the diagram of how the Go test server (Server_1) fits into the architecture of this system.
![alt text](image.png)

The possible response values for this test server:
```
HTTP 404 - Not Found
HTTP 403 - Forbidden
HTTP 500 - Internal Server Error
HTTP 502 - Bad Gateway
HTTP 503 - Service Unavailable
```

server output:
![alt text](image-4.png)

proxy output:
![alt text](image-3.png)


#### Stress Testing with large files

To stress test I uploaded 3 of my old text books from undergrad to my cs6200 Github repo. These pdfs range in size from ~3MB to ~30MB.

1. I replaced my `workload.txt` with the files I uploaded.

    ```yaml
    /master/test_files/A_First_Course_in_Differential_Equat.pdf
    /master/test_files/Linear_Algebra.pdf
    /master/test_files/SkienaTheAlgorithmDesignManual.pdf

    ```
2. I started the proxy and pointed the server to the server `"https://raw.githubusercontent.com/Robbie08/cs6200"`
3. Ran the `./gfclient_download -p 16642` from a seperate shell.
4. Verified that the files were downloaded into the `/master` directory and that pfds were in original conditions.


![alt text](image-5.png)


#### Stress testing with parallel calls
To stress test with parallel calls, I performed the same exercise from the previous stress test which downloads large files but I decided to run the `./gfclient_download -p 16642` in 5 multiple background processes and verified the server handled the requests and responed accordingly.


## Part 2