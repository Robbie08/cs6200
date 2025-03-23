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

int ipc_init(size_t shm_size) {
    if (mq_channel_init() == -1) {
        fprintf(stderr, "Failed to initialize Message Queue\n");
        return -1;
    }

    if (shm_channel_init(shm_size) == -1) {
        fprintf(stderr, "Failed to initialize Shared Memory\n");
        return -1;
    }

    if (semaphore_init() == -1) {
        fprintf(stderr, "Failed to initialize Semaphore\n");
        return -1;
    }

    return 0;
}


int ipc_destroy() {
    if (mq_close(ipc_chan.mq_fd) == -1) {
        perror("mq_close failed");
        return -1;
    }
    if (mq_unlink(MQ_NAME) == -1) {
        perror("mq_unlink failed");
        return -1;
    }
    if (shm_unlink(SHM_NAME) == -1) {
        perror("shm_unlink failed");
        return -1;
    }

    if (sem_close(cache_sem) == -1) {
        perror("sem_close failed");
        return -1;
    }

    if (sem_unlink(SEM_NAME) == -1) {
        perror("sem_unlink failed");
        return -1;
    }
    
    return 0;
}
