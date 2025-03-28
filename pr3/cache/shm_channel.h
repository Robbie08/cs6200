#ifndef _SHM_CHANNEL_H
#define _SHM_CHANNEL_H

#include <stddef.h>
#include <mqueue.h>   // For POSIX Message Queue
#include <sys/mman.h> // For Shared Memory
#include <fcntl.h>    // For file descriptor flags
#include <semaphore.h> // For synchronization
#include <pthread.h>
#include "steque.h"
#include "cache-student.h"

#define MAX_FILENAME_LEN 256
#define MAX_FILE_SIZE 10485760  // 10MB max file size
#define MQ_MAX_SIZE 8192

/**
 * IPC CHANNEL STRUCTURES (Message Queue & Shared Memory)
 * Used to communicate between Proxy & Cache.
 */
 /**
 * Defines the type of operation to be performed on the cache. READ indicates
 * that the proxy is requesting the file from the cache, while WRITE indicates
 * that the cache is storing the file.
 */
typedef enum {
    CACHE_READ = 1,
    CACHE_WRITE = 2
} request_type_t;

/**
 * Defines the type of response from the cache. This is used by the proxy
 * to determine if the file was found in the cache or not.
 */
typedef enum {
    CACHE_HIT = 1,
    CACHE_MISS = 2
} response_type_t;

typedef struct {
    mqd_t mq_fd; // Message queue file descriptor
    struct mq_attr mq_attr; // Message queue attributes
    char mq_buffer[MQ_MAX_SIZE];

    int shm_fd; // Shared memory file descriptor
    void *shm_base; // Pointer to shared memory
    size_t segment_size;

    steque_t offset_pool;
    sem_t offset_pool_sem; 
    pthread_mutex_t offset_pool_lock;
} ipc_chan_t;

extern sem_t *cache_sem;  // Global for both proxy and cache to access
extern ipc_chan_t ipc_chan;

/**
 * COMMAND CHANNEL STRUCTURE (Message Queue)
 * Used to communicate cache queries between Proxy & Cache.
 */

/**
 * Structure for cache request. This gets published to the message queue
 * and consumed by the cache dameon.
 */
typedef struct {
    request_type_t request_type;  
    char file_name[MAX_FILENAME_LEN]; 
    size_t file_size;
    size_t shm_offset; 
} cache_request_t;

/**
 * Structure for cache response. This gets consumed by the proxy thread
 * through the private queue created by the proxy.
 */
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
    response_type_t response_type;
    size_t file_size;
    size_t total_size;
    size_t chunk_size;
    size_t bytes_transferred;
    int is_done;
    int is_valid;
    sem_t chunk_ready_sem;
    char data[];  
} shm_file_t;


/**
 * Initializes both the Message Queue and Shared Memory.
 * @param shm_size The size of the shared memory segment.
 * @return 0 on success, -1 on failure.
 */
int ipc_init(size_t segment_size, size_t segment_count);


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

/**
 * Created the pool of available offsets. The proxy will use this to 
 * fetch available offsets that it can then send to the cache. This will
 * be the Data Channel of communication between proxy and cache daemon.
 */
int shm_offset_pool_init(size_t seg_size, size_t segment_count);

/**
 * This function removes all the offsets from the queue, freeing them up one by one
 * and then destroys the queue.
 */
void shm_offset_pool_destroy();

/**
 * This is a blocking function that picks up the next available offset from the offset pool.
 */
ssize_t shm_channel_acquire_segment(void);

/**
 * This function releases the segment back into the offset pool.
 */
int shm_channel_release_segment(size_t offset);

/**
 * Initializes the Global named semaphore for synchronization between Proxy and Cache
 * workers to publish and consume requests and responses from the Message Queue
 * @return 0 on success, -1 on failure.
 */
int semaphore_init();

#endif // _SHM_CHANNEL_H
