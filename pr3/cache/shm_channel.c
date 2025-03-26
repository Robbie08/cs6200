#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mqueue.h>
#include "shm_channel.h"

#define MQ_MAX_SIZE 8192
#define SHM_NAME "/shm_cache_rortiz"
#define SEM_NAME "/sem_cache_rortiz"
#define MQ_NAME "/mq_cache_rortiz"


/**
 * SHARED MEMORY FUNCTIONS
 */

ipc_chan_t ipc_chan;

int shm_channel_init(size_t size) {
    ipc_chan.shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (ipc_chan.shm_fd == -1) {
        perror("shm_open failed");
        return -1;
    }
    if (ftruncate(ipc_chan.shm_fd, size) == -1) {
        perror("ftruncate failed");
        return -1;
    }
    ipc_chan.shm_base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_chan.shm_fd, 0);
    if (ipc_chan.shm_base == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }
    return 0;
}

int shm_channel_attach() {
    ipc_chan.shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (ipc_chan.shm_fd == -1) {
        perror("shm_open attach failed");
        return -1;
    }
    return 0;
}

int semaphore_init() {
    cache_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (cache_sem == SEM_FAILED) {
        perror("sem_open failed");
        return -1;
    }
    return 0;
}

/**
 * MESSAGE QUEUE FUNCTIONS
 */

int mq_channel_init() {
    ipc_chan.mq_attr.mq_flags = 0;
    ipc_chan.mq_attr.mq_maxmsg = 10;
    ipc_chan.mq_attr.mq_msgsize = MQ_MAX_SIZE;
    ipc_chan.mq_attr.mq_curmsgs = 0;

    ipc_chan.mq_fd = mq_open(MQ_NAME, O_CREAT | O_RDWR, 0666, &ipc_chan.mq_attr);
    if (ipc_chan.mq_fd == (mqd_t)-1) {
        perror("mq_open failed");
        return -1;
    }

    return 0;
}

/**
 * IPC CREATE AND DESTROY FUNCTIONS
 */

int ipc_init(size_t segment_size, size_t segment_count) {
    int err = mq_channel_init();
    if (err == -1) {
        fprintf(stderr, "Failed to initialize Message Queue\n");
        return -1;
    }

    size_t shm_size = segment_size * segment_count;
    err = shm_channel_init(shm_size);
    if (err == -1) {
        fprintf(stderr, "Failed to initialize Shared Memory\n");
        return -1;
    }

    err = shm_offset_pool_init(segment_size, segment_count);
    if (err == -1) {
        fprintf(stderr, "Failed to initialize shared memory offset pool");
        return -1;
    }

    err = semaphore_init();
    if (err == -1) {
        fprintf(stderr, "Failed to initialize Semaphore\n");
        return -1;
    }

    return 0;
}


int ipc_destroy() {
    int err;
    err = mq_close(ipc_chan.mq_fd);
    if (err == -1) {
        perror("mq_close failed");
        return -1;
    }

    err = mq_unlink(MQ_NAME);
    if (err == -1) {
        perror("mq_unlink failed");
        return -1;
    }

    err = shm_unlink(SHM_NAME);
    if (err == -1) {
        perror("shm_unlink failed");
        return -1;
    }

    shm_offset_pool_destroy();

    err = sem_close(cache_sem);
    if (err == -1) {
        perror("sem_close failed");
        return -1;
    }

    err = sem_unlink(SEM_NAME);
    if (err == -1) {
        perror("sem_unlink failed");
        return -1;
    }

    return 0;
}

int shm_offset_pool_init(size_t seg_size, size_t segment_count) {
	ipc_chan.segment_size = seg_size;
	steque_init(&ipc_chan.offset_pool);

    for (int i = 0; i < segment_count; i++) {
        size_t *offset = malloc(sizeof(size_t)); // allocate enough memory for the segment
        if (!offset) {
            perror("shm_offset_pool_init: malloc");
            return -1;
        }
        *offset = i * ipc_chan.segment_size; // This allows us to calculate the offset for the current segment
        steque_enqueue(&ipc_chan.offset_pool, offset); // add ptr to queue
    }
    return 0;
}

void shm_offset_pool_destroy() {
    while (!steque_isempty(&ipc_chan.offset_pool)) {
        void *ptr = steque_pop(&ipc_chan.offset_pool);
        if (ptr) {
            free(ptr);
        }
    }

    steque_destroy(&ipc_chan.offset_pool);
}

ssize_t shm_channel_acquire_segment(void) {

    // Blocking call, waits until segment is available
    int err = sem_wait(&ipc_chan.offset_pool_sem);
    if (err == -1) {
        perror("shm_channel_acquire_segment");
        return -1;
    }
    err = pthread_mutex_lock(&ipc_chan.offset_pool_lock);
    if (err != 0) {
        perror("shm_channel_acquire_segment: pthread_mutex_lock");
        return -1;
    }

    void *ptr = steque_pop(&ipc_chan.offset_pool);
    err = pthread_mutex_unlock(&ipc_chan.offset_pool_lock);
    if (err != 0) {
        perror("shm_channel_acquire_segment: pthread_mutex_unlock");
        return -1;
    }

    // This shouldn't happen but adding it as a safety measure in case I need to debug.
    if (!ptr) {
        fprintf(stderr, "shm_channel_acquire_segment failed because the offset pool is empty.");
        return -1;
    }

    size_t offset = *((size_t *)ptr); 
    free(ptr); // we need to clean this temp ptr up or we're going to leak
    return (ssize_t)offset;
}

int shm_channel_release_segment(size_t offset) {
    // Basically similar to aquire but in reverse

    size_t *ptr = malloc(sizeof(size_t));
    if (!ptr) {
        perror("shm_channel_release_segment: malloc");
        return -1;
    }
    *ptr = offset;

    int err = pthread_mutex_lock(&ipc_chan.offset_pool_lock);
    if (err != 0) {
        perror("shm_channel_release_segment: pthread_mutex_lock");
        free(ptr); // lets avoid leaking
        return -1;
    }

    steque_enqueue(&ipc_chan.offset_pool, ptr); // enqueue the offset back into the pool

    err = pthread_mutex_unlock(&ipc_chan.offset_pool_lock);
    if (err != 0) {
        perror("shm_channel_release_segment: pthread_mutex_lock");
        return -1;
    }

    err = sem_post(&ipc_chan.offset_pool_sem);
    if (err == -1) {
        perror("shm_channel_release_segment: sem_post");
        return -1;
    }

    return 0;
}


int mq_publish_request(cache_request_t *req) {
    mqd_t mq = ipc_chan.mq_fd;
    if (mq_send(mq, (char *)req, sizeof(cache_request_t), 0) == -1) {
        perror("mq_publish_request: mq_send");
        return -1;
    }
    return 0;
}

int pmq_consume_request(cache_response_t *resp, mqd_t pmq_fd) {
    ssize_t bytes_recvd = mq_receive(pmq_fd, (char *)resp, sizeof(cache_response_t), NULL);
    if (bytes_recvd == -1) {
        perror("pmq_consume_request: mq_receive");
        return -1;
    }

    if ((size_t)bytes_recvd != sizeof(cache_response_t)) {
        fprintf(stderr, "pmq_consume_request: received partial message (%zd bytes)\n", bytes_recvd);
        return -1;
    }
    return 0;
}
