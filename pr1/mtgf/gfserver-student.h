/*
 *  This file is for use by students to define anything they wish.  It is used by the gf server implementation
 */
#ifndef __GF_SERVER_STUDENT_H__
#define __GF_SERVER_STUDENT_H__

#include "gf-student.h"
#include "gfserver.h"
#include "content.h"
#include "steque.h"
#include <pthread.h>

#define MAX_DELEGATES 64
#define CHUNK_SIZE 4096

typedef struct {
    size_t pool_size;
    pthread_t delegate_pool[MAX_DELEGATES];
    steque_t request_q;
    pthread_mutex_t q_lock;
    pthread_mutex_t file_lock;
    pthread_cond_t q_not_empty;
} gfserver_delegate_pool_t;

typedef struct {
    gfcontext_t *ctx;   // The ctx is the context passed by the Delegator
    char *path;         // The path is the path being retrieved
    void *arg;          // The arg provided by the handler
} request_t;


void init_threads(size_t numthreads);
void cleanup_threads();

/**
 * This function initializes the delegate pool which includes the
 * queue, queue mutex, and queue conditional variable.
 */
int init_delegate_pool(size_t numOfDelegates);

/**
 * This function handles the delegate's work. Each delegate in the pool 
 * will have the same task. 
 */
void delegate_function(void *args);

/**
 * This method allows us to create the request which acts as a wrapper
 * object for our context and path.
 */
request_t create_request(gfcontext_t **ctx, char *path, void *arg);

/**
 * This method will allow us to free up the space created for the request
 */
void destory_request(request_t *request);

/**
 * This function will be in charge of sending the entire file to the client
 */
int sendFileContents(request_t *request, int filefd);

#endif // __GF_SERVER_STUDENT_H__
