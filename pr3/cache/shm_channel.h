#ifndef _SHM_CHANNEL_H
#define _SHM_CHANNEL_H

#include <stddef.h>
#include <mqueue.h>   // For POSIX Message Queue
#include <sys/mman.h> // For Shared Memory
#include <fcntl.h>    // For file descriptor flags
#include <semaphore.h> // For synchronization

#define MAX_FILENAME_LEN 256
#define MAX_FILE_SIZE 10485760  // 10MB max file size


typedef enum {
    CACHE_READ = 1,
    CACHE_WRITE = 2
} request_type_t;

typedef enum {
    CACHE_HIT = 1,
    CACHE_MISS = 2
} response_type_t;

/**
 * COMMAND CHANNEL STRUCTURE (Message Queue)
 * Used to communicate cache queries between Proxy & Cache.
 */
typedef struct {
    request_type_t request_type;  
    char file_name[MAX_FILENAME_LEN]; 
    size_t file_size; 
} cache_request_t;

typedef struct {
    response_type_t response_type;
    size_t shm_offset; 
    size_t file_size;  
} cache_response_t;

/**
 * DATA CHANNEL STRUCTURE (Shared Memory)
 * Used to store cached files.
 */
typedef struct {
    char file_name[MAX_FILENAME_LEN];  
    size_t file_size;  
    int is_valid;  
    char data[];  
} shm_file_t;


/**
 * Initializes both the Message Queue and Shared Memory.
 * @param shm_size The size of the shared memory segment.
 * @return 0 on success, -1 on failure.
 */
int ipc_init(size_t shm_size);


/**
 * Destroys both the Message Queue and Shared Memory.
 * @return 0 on success, -1 on failure.
 */
int ipc_destroy();


// ======================================
//  MESSAGE QUEUE (COMMAND CHANNEL)
// ======================================

/**
 * Initializes the message queue for Proxy ↔ Cache communication.
 * @return 0 on success, -1 on failure.
 */
int mq_channel_init();

/**
 * Sends a cache lookup request to the cache. This is used from the proxy side.
 * @param request Pointer to the cache_request_t struct.
 * @return 0 on success, -1 on failure.
 */
int mq_publish_request(cache_request_t *request);

/**
 * Receives a cache lookup response from the cache.
 * @param response Pointer to cache_response_t struct to store the response.
 * @return 0 on success, -1 on failure.
 */
int mq_consume_request(cache_response_t *response);

/**
 * Destroys the message queue.
 * @return 0 on success, -1 on failure.
 */
int mq_channel_destroy();

// ================================
// SHARED MEMORY (DATA CHANNEL)
// ================================

/**
 * Initializes the shared memory segment.
 * @param size The size of the shared memory segment.
 * @return 0 on success, -1 on failure.
 */
int shm_channel_init(size_t size);

/**
 * Attaches to an existing shared memory segment.
 * @return 0 on success, -1 on failure.
 */
int shm_channel_attach();

/**
 * Writes file data to shared memory.
 * @param file_name Name of the file.
 * @param data Pointer to file data.
 * @param file_size Size of the file data.
 * @return Offset in shared memory on success, -1 on failure.
 */
ssize_t shm_channel_write(const char *file_name, void *data, size_t file_size);

/**
 * Reads file data from shared memory.
 * @param shm_offset Offset of the file in shared memory.
 * @param dest Buffer to store the file data.
 * @param file_size Size of the file data.
 * @return Number of bytes read on success, -1 on failure.
 */
ssize_t shm_channel_read(size_t shm_offset, void *dest, size_t file_size);

/**
 * Destroys the shared memory segment.
 * @return 0 on success, -1 on failure.
 */
int shm_channel_destroy();

#endif // _SHM_CHANNEL_H
