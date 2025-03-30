#ifndef _SHM_CHANNEL_H
#define _SHM_CHANNEL_H

#include <stddef.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>
#include "steque.h"
#include "cache-student.h"

#define MAX_FILENAME_LEN 256
#define MQ_MAX_SIZE 8192

/**
 * This enum is used to differentiate the request 
 */
typedef enum {
    CACHE_READ = 1,
    CACHE_WRITE = 2,
    CACHE_INIT = 3
} request_type_t;

/**
 * Defines the type of response from the cache. This is used by the proxy
 * to determine if the file was found in the cache or not.
 */
typedef enum {
    CACHE_HIT = 1,
    CACHE_MISS = 2
} response_type_t;

/**
 * This data structure is used to contain the global means variables
 * needed for the IPC
 */
typedef struct {
    mqd_t mq_fd; // Message queue file descriptor
    struct mq_attr mq_attr; // Message queue attributes
    char mq_buffer[MQ_MAX_SIZE];

    int shm_fd; // Shared memory file descriptor
    void *shm_base; // Pointer to shared memory
    size_t segment_size;
    size_t segment_count;
    steque_t offset_pool;
    sem_t offset_pool_sem; 
    pthread_mutex_t offset_pool_lock;
} ipc_chan_t;


/**
 * Structure for cache request. This gets published to the message queue
 * and consumed by the cache dameon.
 */
typedef struct {
    request_type_t request_type;  
    char file_name[MAX_FILENAME_LEN]; 
    size_t file_size;
    size_t shm_offset; 
    size_t segment_count;
    size_t segment_size;
} cache_request_t;

/**
 * This struct is used to transfer files through shared memory (Data Channel)
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

extern sem_t *cache_sem;  // Global for both proxy and cache to access
extern ipc_chan_t ipc_chan;



/**
 * Initializes both the Message Queue and Shared Memory.
 * shm_size The size of the shared memory segment.
 * returns: 0 on success, -1 on failure.
 */
int ipc_init(size_t segment_size, size_t segment_count);


/**
 * Destroys both the Message Queue and Shared Memory.
 * returns: 0 on success, -1 on failure.
 */
int ipc_destroy();


/**
 * Initializes the message queue for Proxy ↔ Cache communication.
 * returns: 0 on success, -1 on failure.
 */
int mq_channel_init();

/**
 * Sends a cache lookup request to the cache. This is used from the proxy side.
 * request Pointer to the cache_request_t struct.
 * returns: 0 on success, -1 on failure.
 */
int mq_publish_request(cache_request_t *request);


/**
 * Destroys the message queue.
 * returns: 0 on success, -1 on failure.
 */
int mq_channel_destroy();

/**
 * Initializes the shared memory segment.
 * size The size of the shared memory segment.
 * returns: 0 on success, -1 on failure.
 */
int shm_channel_init(size_t size);


/**
 * Created the pool of available offsets. The proxy will use this to 
 * fetch available offsets that it can then send to the cache. This will
 * be the Data Channel of communication between proxy and cache daemon.
 * seg_size is the size we want for each segment
 * segment_count is the total number of segments we want
 * returns: 0 on success, -1 on failure. 
 */
int shm_offset_pool_init(size_t seg_size, size_t segment_count);

/**
 * This function removes all the offsets from the queue, freeing them up one by one
 * and then destroys the queue.
 */
void shm_offset_pool_destroy();

/**
 * This is a blocking function that picks up the next available offset from the offset pool.
 * returns: -1 on failure, otherwise returns the segment acquired.
 */
ssize_t shm_channel_acquire_segment(void);

/**
 * This function releases the segment back into the offset pool.
 * offset is the segment we want to release
 * returns: 0 on success, -1 on failure.
 */
int shm_channel_release_segment(size_t offset);

/**
 * Initializes the Global named semaphore for synchronization between Proxy and Cache
 * workers to publish and consume requests and responses from the Message Queue
 * returns: 0 on success, -1 on failure.
 */
int semaphore_init();

#endif // _SHM_CHANNEL_H
