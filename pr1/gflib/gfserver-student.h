/*
 *  This file is for use by students to define anything they wish.  It is used by the gf server implementation
 */
#ifndef __GF_SERVER_STUDENT_H__
#define __GF_SERVER_STUDENT_H__

#include "gf-student.h"
#include "gfserver.h"
#include <stdlib.h>
#include <netdb.h>

/*
 * This function creates a socket and binds to the first valid address in the addressList (linked list).
 * The socket's file descriptor is retured if the operation succeeded.
 */
int createAndBindSocket(struct addrinfo *adressesList);

/*
 * This function creates and initilizes the gfcontext_t object
 */
gfcontext_t* context_create();

/**
 * This function sanitizes the request and returns the valid status
 */
gfstatus_t validateRequest(const char *request);


/**
 * This function extracts the path from the request
 */
const char* extractPath(const char* requestPath);

#endif // __GF_SERVER_STUDENT_H__