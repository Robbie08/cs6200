/*
 *  This file is for use by students to define anything they wish.  It is used by the gf client implementation
 */
 #ifndef __GF_CLIENT_STUDENT_H__
 #define __GF_CLIENT_STUDENT_H__
 
 #include "gfclient.h"
 #include "gf-student.h"

 /**
 * This method creates a socket and connects with the first available server address, provided by the addressesList(linked list).
 * Returns the socket's file descriptor if successfully connected, otherwise terminates the program.
 */
int createSocketAndConnect(struct addrinfo *addressesList);


gfstatus_t parseResponseHeader(gfcrequest_t **gfr, const char* response, ssize_t bytesRecvd);

 
 #endif // __GF_CLIENT_STUDENT_H__
