#include "gfserver-student.h"

#define REQ_MAX_LEN 4113 // GETFILE GET <path>\r\n\r\n\0 = 7+1+3+1+4096+4+1 = 4113 bytes
#define FILE_PATH_MAX_LEN 4096 // max length in a linux file system is 4096 bytes
#define MAX_PORT_DIGITS 6
#define GETFILE "GETFILE"
#define TIMEOUT_SEC 5
#define TIMEOUT_MILI 0

// Modify this file to implement the interface specified in
 // gfserver.h.


// This struct carries config information important to server
struct gfserver_t {
    int sockfd;             // the server's socket's file descriptor
    unsigned short port;    // port number the server is listening on
    int maxnpending;        // the max pending connections the server will queue up
    void *handlerarg;       // Argument for our handler function

    // Function ptr for the request handler described in gfserver.h
    gfh_error_t (*handler)(gfcontext_t **ctx, const char *path, void* arg);
};

struct gfcontext_t {
    int connFd;                             // File descriptor for the connection
    char request[REQ_MAX_LEN];              // the request made by client
    socklen_t addrSize;                     // The address size
    struct sockaddr_storage connAddress;    // The connection address
    size_t bytesRecvd;                      // This outlines the total number of bytes received 
    size_t bytesSent;                       // This outlines the number of bytes sent
    gfstatus_t responseCode;                // The response associated with the request
};

void gfs_abort(gfcontext_t **ctx){
    if (ctx == NULL || *ctx == NULL) {
        return;
    }

    if ((*ctx) -> connFd != -1) {
        close((*ctx) -> connFd);
    }

    free(*ctx);
    *ctx = NULL;
}

const char* extractPath(const char* requestPath) {
    char *pathStart = strchr(requestPath, ' ');
    if (pathStart != NULL) {
        pathStart = strchr(pathStart+1, ' '); // point to the next space which should be after GET
    }

    // at this point, I'm pointing to the space before the path
    // so I just need to copy the path characters up until I find '\r'
    pathStart++;
    
    char *pathEnd = strchr(pathStart, '\r'); // we know that our path ends when we encounter '\r'
    static char extractedPath[FILE_PATH_MAX_LEN];
    size_t pathLength = pathEnd - pathStart;  // Calculate path length

    if (pathLength >= REQ_MAX_LEN) {
        return NULL;  // Path too long
    }

    strncpy(extractedPath, pathStart, pathLength);
    extractedPath[pathLength] = '\0';  // Null-terminate the extracted path
    return extractedPath;

}

gfstatus_t validateRequest(const char *request) {
    // If request is null then return invalid code
    if (request == NULL) {
        perror("Failed because request is NULL");
        return GF_INVALID; // Verify that this error fits the requirements
    }
    
    // Every request must have "GETFILE GET /", let's verify that
    const char *prefix = "GETFILE GET /";
    int prefixLen = strlen(prefix);
    if (strncmp(request, prefix, prefixLen) != 0) {
        perror("Doesn't start with 'GETFILE GET /'\n");
        return GF_INVALID;
    }

    if (strstr(request, "\r\n\r\n") == NULL){
        perror("Didn't contain the delimiter suffix.\n");
        return GF_INVALID;
    }

    // let's ensure that we only have 2 spaces in the request
    const char *str = request;
    int spaceCount = 0;
    while((str = strchr(str, ' ')) != NULL) {
        spaceCount++;
        str++;
    }

    if (spaceCount != 2) {
        fprintf(stderr, "number of spaces is: '%d' but should be '2'\n", spaceCount);
        return GF_INVALID;
    }

    // let's verify that the path starts with '/' and doesn't exceed the length of the max
    char *pathStart = strchr(request, ' ');
    if (pathStart != NULL) {
        pathStart = strchr(pathStart+1, ' '); // point to the next space which should be after GET
    }

    if (pathStart == NULL || *(pathStart + 1) != '/' || strlen(pathStart + 1) >= REQ_MAX_LEN) {
        return GF_INVALID;
    }

    return GF_OK;
}

ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len){
    // not yet implemented
    const char *ptr = (const char *)data;
    ssize_t bytesSent, totalBytesSent = 0;
    ssize_t bytesToSend = len;
    while(bytesToSend > 0) {
        bytesSent = send((*ctx)->connFd, ptr, bytesToSend, 0);
        if (bytesSent == -1) {
            perror("server: send");
            return -1;
        } else if(bytesSent == 0) {
            perror("server: send failed because the client closed the connection.");
            return -1;
        }

        bytesToSend -= bytesSent;
        ptr += bytesSent;
        totalBytesSent += bytesSent;
        (*ctx)->bytesSent += bytesSent;
    }
    
    return totalBytesSent;
}

ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len){
    if (ctx == NULL || *ctx == NULL) {
        fprintf(stderr, "gfs_sendheader: Invalid context\n");
        return -1;
    }

    char header[REQ_MAX_LEN];
    memset(&header, 0, REQ_MAX_LEN);

    if (status == GF_OK) {
        snprintf(header, sizeof(header), "%s OK %zu\r\n\r\n", GETFILE, file_len);
    } else if(status == GF_INVALID) {
        snprintf(header, sizeof(header), "%s INVALID\r\n\r\n", GETFILE);
    } else if(status == GF_ERROR) {
        snprintf(header, sizeof(header), "%s ERROR\r\n\r\n", GETFILE);
    } else if(status == GF_FILE_NOT_FOUND) {
        snprintf(header, sizeof(header), "%s FILE_NOT_FOUND\r\n\r\n", GETFILE);
    } 
    
    size_t headerLen = strlen(header);
    ssize_t bytesSent;
    bytesSent = send((*ctx)->connFd, header, headerLen, 0);
    if (bytesSent == -1){
        perror("server: send");
        gfs_abort(ctx);
    } 
    return bytesSent;
}

gfcontext_t* context_create(){
    gfcontext_t* connectionConfig = malloc(sizeof(gfcontext_t));
    if (connectionConfig == NULL) {
        perror("context_create: failed to allocate memory for the struct");
        return NULL;
    }

    memset(connectionConfig, 0, sizeof(gfcontext_t));
    connectionConfig -> connFd = -1; // to allow error detection during socket creation
    connectionConfig -> addrSize = sizeof(struct sockaddr_storage);
    connectionConfig -> bytesSent = 0;
    connectionConfig -> bytesRecvd = 0;
    
    return connectionConfig;
}

gfserver_t* gfserver_create(){
    gfserver_t *serverConfig = malloc(sizeof(gfserver_t));
    if (serverConfig == NULL) {
        perror("gfserver_create: failed to allocate memory for the struct");
        return NULL;
    }

    memset(serverConfig, 0, sizeof(gfserver_t));
    
    // set fields to default values
    serverConfig -> sockfd = -1; // Defaults to invalid socket, allows us from continuing in case issue with socket creation
    serverConfig -> port = 0;
    serverConfig -> maxnpending = 0;
    serverConfig -> handlerarg = NULL;
    serverConfig -> handler = NULL;
    
    return serverConfig;
}

void gfserver_set_handler(gfserver_t **gfs, gfh_error_t (*handler)(gfcontext_t **, const char *, void*)){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        return;
    }
    (*gfs)->handler = handler;
}

void gfserver_set_port(gfserver_t **gfs, unsigned short port){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        return;
    }
    (*gfs)->port = port;
}

void gfserver_serve(gfserver_t **gfs){
    struct addrinfo addrConfig;
    memset(&addrConfig, 0, sizeof addrConfig);
    addrConfig.ai_family = AF_UNSPEC; // to allow both IPv4 and IPv6
    addrConfig.ai_socktype = SOCK_STREAM; // Since we want to make this a TCP socket
    addrConfig.ai_flags = AI_PASSIVE; // Tells getaddrinfo() to assign local host to the socket structures

    char portStr[MAX_PORT_DIGITS];
    memset(&portStr, 0, sizeof portStr);
    sprintf(portStr, "%d", (*gfs)->port);

    int status;
    struct addrinfo *addressesList;
    status = getaddrinfo(NULL, portStr, &addrConfig, &addressesList);
    if (status != 0) {
        // Send error to stderr and stop the program since ther's no point to continue if getaddrinfo fails
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return;
    }

    // Set and bind our server's file descriptor
    (*gfs) -> sockfd = createAndBindSocket(addressesList);
    // Once we're done with adressesList let's free up the linked list
    freeaddrinfo(addressesList);

    if ((*gfs)->sockfd == -1) {
        perror("server: createAndBindSocket");
        return;
    }

    int err = 0;
    err = listen((*gfs) -> sockfd, (*gfs) -> maxnpending);
    if (err == -1) {
        perror("server: listen");
        close((*gfs) -> sockfd);
        return;
    }

    for (;;) {
        gfcontext_t *ctx = context_create();
        ctx->connFd = accept((*gfs)->sockfd, (struct sockaddr *)&(ctx->connAddress), &(ctx->addrSize));
        if (ctx->connFd == -1) {
            perror("server: accept");
            gfs_sendheader(&ctx, GF_ERROR, 0);
            gfs_abort(&ctx);
            continue;
        }

        // Resource on how to create a timeout for recv during client connection: https://linux.die.net/man/3/setsockopt
        // Resource for defining the timeval struct: https://linux.die.net/man/3/timercmp
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT_SEC; 
        timeout.tv_usec = TIMEOUT_MILI;

        // Let's set the socket option so that it timeouts after X seconds to avoid hanging connections
        if (setsockopt(ctx->connFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("server: setsockopt (SO_RCVTIMEO) failed");
            gfs_sendheader(&ctx, GF_ERROR, 0);
            gfs_abort(&ctx);
            continue;
        }

        size_t bytesRecv;        
        bytesRecv = recvHeader(ctx);
        if (bytesRecv <= 0) {
            perror("server: recvHeader");
            gfs_sendheader(&ctx, GF_INVALID, 0);
            gfs_abort(&ctx);
            continue;
        }

        if (bytesRecv < REQ_MAX_LEN) {
            ctx->request[bytesRecv] = '\0';
        } else {
            ctx->request[REQ_MAX_LEN-1] = '\0';
        }

        gfstatus_t valid = validateRequest(ctx->request);
        if (valid != GF_OK) {
            gfs_sendheader(&ctx, valid, 0);
            gfs_abort(&ctx);
            continue;
        }

        const char* extractedPath = extractPath(ctx->request);
        
        gfh_error_t status = (*gfs)->handler(&ctx, extractedPath, (*gfs) -> handlerarg);
        if (status != GF_OK){
            gfs_sendheader(&ctx, status, 0);
        }
        gfs_abort(&ctx);
    }
    
}

void gfserver_set_handlerarg(gfserver_t **gfs, void* arg){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        return;
    }
    (*gfs)->handlerarg = arg;
}

void gfserver_set_maxpending(gfserver_t **gfs, int max_npending){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        return;
    }
    (*gfs)->maxnpending = max_npending;
}


// This function creates a socket and binds to the first valid address in the addressList (linked list).
// The socket's file descriptor is retured if the operation succeeded.
int createAndBindSocket(struct addrinfo *adressesList) {
    int sockfd = -1;
    struct addrinfo *curr;
    int yes = 1;
    int err;
    for (curr = adressesList; curr != NULL; curr = curr->ai_next) {

        // Attempt to create a socket until success
        sockfd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol);
        if (sockfd == -1) {
            perror("server: socket");
            continue;
        }
        
        err = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        // If our port is still in use then lets just force it by allowing our program to use reuse it
        if(err == -1) {
            perror("server: setsockopt");
            close(sockfd);
            return -1; // No point in continuing if for some reason we can't reuse the port since subsequent code will fail
        }
        
        err = bind(sockfd, curr->ai_addr, curr->ai_addrlen);
        if (err == -1) {
            close(sockfd);
            perror("server: bind");
            continue; // Just becuase this one failed to bind doesn't mean there isn't another one available
        }

        break; // if we made it this far then we've created a socket and associated it with a port number on our machine.
    }

    if (curr == NULL) {
        fprintf(stderr, "server: failed to bind");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

size_t recvHeader(gfcontext_t *ctx) {

    char headerBuf[REQ_MAX_LEN];
    memset(&headerBuf, 0, sizeof headerBuf);

    size_t bytesRecv;
    for(;;) {

        bytesRecv = recv(ctx->connFd, headerBuf, REQ_MAX_LEN, 0);

        if (bytesRecv == 0) {
            // If we get 0 then that means the connection was terminated by the client.
            perror("server: client disconnected prematurely");
            ctx->responseCode = GF_INVALID;
            return -1;
        } else if (bytesRecv == -1) {
            if (errno == EWOULDBLOCK) {
                perror("server: recv timed out while waiting for entire header");
            } else {
                perror("server: recv");
            }
            ctx->responseCode = GF_INVALID;
            return -1;
        }

        if (ctx->bytesRecvd + bytesRecv >= REQ_MAX_LEN-1) {
            perror("server: client sent a larger header than expected");
            return -1;
        }

        strncat(ctx->request, headerBuf, bytesRecv);
        ctx->bytesRecvd += bytesRecv;

        if(strstr(ctx->request, "\r\n\r\n") != NULL) {
            break;
        }
    }
        
    return ctx->bytesRecvd;
}