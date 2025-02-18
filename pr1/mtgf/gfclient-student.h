/*
 *  This file is for use by students to define anything they wish.  It is used by the gf client implementation
 */
#ifndef __GF_CLIENT_STUDENT_H__
#define __GF_CLIENT_STUDENT_H__

#include "workload.h"
#include "gfclient.h"
#include "gf-student.h"
#include "steque.h"

#define MAX_DELEGATES 64

typedef struct {
    pthread_t pool[MAX_DELEGATES];          // Defines the thread pool
    steque_t q_request;                     // Defines the request queue
    pthread_mutex_t q_lock;                 // Defines the lock for accessing the Q
    pthread_cond_t q_not_empty;             // Defines the conditional variable to singal work is available
    int completed;                          // This flag tells us if we need to even wait
} gfclient_pool_t;

/**
 * This struct is used by the Delegator and Delegates to communicate
 * on when the work is completed. The Delegator will receive a singal
 * letting it know when each delegate has completed.
 */
typedef struct {
    int active_delegates;                       // Is a counter that counts the number of active requests
    pthread_mutex_t active_delegates_lock;      // This lock is for updating the active_delegates counter
    pthread_cond_t completed;                   // Dfines the conditional variable for when the Delegator must finish
} delegate_tracker_t;

/**
 * This delegation request will be used to encapsulate the required fields 
 * for the delegate to perform their work. The Delegator will create the object
 * and push it to the request queue. The Delegate will consume it and perform
 * their work as needed.
 */
typedef struct {
    char *path;                 // this holds the path of the request
    char *local_path;           // this holds the local path of where the file is saved
    char *server;               // this holds the host for the server
    unsigned short port;        // this holds the port for the server
    int sentinel;               // this holds our flag for knowing if this is sentinel
    gfstatus_t status;          // this holds the status for the file requested
    void *writearg;             // this includes the writearg for our writefunc

    // this is callback function for the writefunc
    void (*writefunc)(void *data, size_t data_len, void *arg);
} delegation_request_t;


/**
 * This function creates a sentinel delegation request, it's used by the delegates
 * to know that there aren't any more requests left.
 */
delegation_request_t* create_sentinel_delegation_request();

/**
 * This function creates a delegation request with the information required
 * for the delegate threats to perform their job.
 */
delegation_request_t* create_delegation_request(char *path, char *local_path, char *server, unsigned short port, void* arg, void (*writefunc)(void *data, size_t data_len, void *arg));

/**
 * This function destroys the delegation request created
 */
void destroy_delegation_request(delegation_request_t **request);

/**
 * This function initializes the delegate pool which includes the
 * queue, queue mutex, and queue conditional variable.
 */
int init_delegate_pool(size_t numOfDelegates);

/**
 * This function creates the delegate threads and adds them to
 * the delegate pool.
 * 
 * I found this resource pretty useful for this function:
 * https://hpc-tutorials.llnl.gov/posix/joining_and_detaching/
 */
int init_threads(size_t numthreads);

/**
 * This function initializes the the request_tracker struct which 
 * includes the counter, mutex, and conditional variable.
 */
int init_delegate_tracker();

/**
 * This function handles the delegate's work. Each delegate in the pool 
 * will have the same task. 
 */
void* delegate_function(void *arg);

/**
 * This function cleans up our threads.
 * I found this resource pretty useful for this function:
 * https://hpc-tutorials.llnl.gov/posix/joining_and_detaching/
 */
void cleanup_threading(int nthreads);

/**
 * This function cleans up our delegate pool
 */
void destroy_delegate_pool();

/**
 * This function cleans up our delegte tracker
 */
void destroy_delegate_tracker();
 
 #endif // __GF_CLIENT_STUDENT_H__