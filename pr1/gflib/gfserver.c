#include "gfserver-student.h"

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
    size_t bytesSent;                       // This outlines the number of bytes sent
    gfstatus_t responseCode;                // The response associated with the request
};

void gfs_abort(gfcontext_t **ctx){
    if (ctx == NULL || *ctx == NULL) {
        return;
    }

    printf("Destorying context!\n");
    if ((*ctx) -> connFd != -1) {
        close((*ctx) -> connFd);
    }

    free(*ctx);
    *ctx = NULL;
    printf("Successfully destroyed context!\n");
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
        printf("Failed because request is NULL\n");
        return GF_INVALID; // Verify that this error fits the requirements
    }
    
    // Every request must have "GETFILE GET /", let's verify that
    const char *prefix = "GETFILE GET";
    int prefixLen = strlen(prefix);
    if (strncmp(request, prefix, prefixLen) != 0) {
        printf("Doesn't start with 'GETFILE GET /'\n");
        return GF_INVALID;
    }

    if (strstr(request, "\r\n\r\n") == NULL){
        printf("Didn't contain the '\r\n\r\n' suffix.\n");
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
        printf("number of spaces is: '%d' but should be '2'\n", spaceCount);
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
    printf("sending file!\n");
    return -1;
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
        perror("gfconnection_create: failed to allocate memory for the struct");
        exit(1);
    }

    memset(connectionConfig, 0, sizeof(gfcontext_t));
    connectionConfig -> connFd = -1; // to allow error detection during socket creation
    connectionConfig -> addrSize = sizeof(struct sockaddr_storage);
    
    return connectionConfig;
}

gfserver_t* gfserver_create(){
    gfserver_t *serverConfig = malloc(sizeof(gfserver_t));
    if (serverConfig == NULL) {
        perror("gfserver_create: failed to allocate memory for the struct");
        exit(1);
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
        exit(1);
    }
    (*gfs)->handler = handler;
}

void gfserver_set_port(gfserver_t **gfs, unsigned short port){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        exit(1);
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
        exit(1);
    }

    // Set and bind our server's file descriptor
    (*gfs) -> sockfd = createAndBindSocket(addressesList);

    int err = 0;
    err = listen((*gfs) -> sockfd, (*gfs) -> maxnpending);
    if (err == -1) {
        perror("server: listen");
        close((*gfs) -> sockfd);
        exit(1);
    }

    printf("Server is listening for connections!\n");
    for (;;) {
        gfcontext_t *ctx = context_create();
        ctx->connFd = accept((*gfs)->sockfd, (struct sockaddr *)&(ctx->connAddress), &(ctx->addrSize));
        if (ctx->connFd == -1) {
            perror("server: accept");
            continue;
        }

        int bytesRecv;
        bytesRecv = recv(ctx->connFd, ctx->request, REQ_MAX_LEN, 0);
        if (bytesRecv == -1) {
            perror("server: recv");
            gfs_abort(&ctx);
            continue;
        } else if (bytesRecv == 0) {
            // If we get 0 then that means the connection was terminated by the client.
            printf("server: client disconnected prematurely\n");
            gfs_abort(&ctx);
            continue;
        }

        if (bytesRecv < REQ_MAX_LEN) {
            ctx->request[bytesRecv] = '\0';
        }

        gfstatus_t valid = validateRequest(ctx->request);
        if (valid != GF_OK) {
            gfs_sendheader(&ctx, valid, 0);
            gfs_abort(&ctx);
            continue;
        }

        const char* extractedPath = extractPath(ctx->request);
        printf("Extracted path: '%s'\n", extractedPath);

        gfh_error_t status = gfs_handler(&ctx, extractedPath, (*gfs) -> handlerarg);
        printf("handler returned status: %lu\n", status);
        if (status != GF_OK){
            gfs_sendheader(&ctx, status, 0);
        }
        gfs_abort(&ctx);
    }
    
}

void gfserver_set_handlerarg(gfserver_t **gfs, void* arg){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        exit(1);
    }
    (*gfs)->handlerarg = arg;
}

void gfserver_set_maxpending(gfserver_t **gfs, int max_npending){
    if(gfs == NULL || *gfs == NULL) {
        perror("gfserver_set_port: gfserver_t pointer is NULL");
        exit(1);
    }
    (*gfs)->maxnpending = max_npending;
}


// This function creates a socket and binds to the first valid address in the addressList (linked list).
// The socket's file descriptor is retured if the operation succeeded.
int createAndBindSocket(struct addrinfo *adressesList) {
    int sockfd;
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
            exit(1); // No point in continuing if for some reason we can't reuse the port since subsequent code will fail
        }
        
        err = bind(sockfd, curr->ai_addr, curr->ai_addrlen);
        if (err == -1) {
            close(sockfd);
            perror("server: bind");
            continue; // Just becuase this one failed to bind doesn't mean there isn't another one available
        }

        break; // if we made it this far then we've created a socket and associated it with a port number on our machine.
    }

    // Once we're done with adressesList let's free up the linked list
    freeaddrinfo(adressesList);

    if (curr == NULL) {
        fprintf(stderr, "server: failed to bind");
        close(sockfd);
        exit(1);
    }

    return sockfd;
}