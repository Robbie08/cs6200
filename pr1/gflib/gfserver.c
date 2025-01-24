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
    char reqPath[REQ_PATH_MAX_LEN];         // the path that the client requested
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

ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len){
    // not yet implemented
    return -1;
}

ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len){
    // not yet implemented
    return -1;
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

        printf("Recieved request!\n");
        
        // read request
        int bytesRecv;
        bytesRecv = recv(ctx->connFd, ctx->reqPath, REQ_PATH_MAX_LEN, 0);
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

        // prep for sending file for now, let's just print hello world and terminate
        if (bytesRecv < REQ_PATH_MAX_LEN) {
            ctx->reqPath[bytesRecv] = '\0';
        }

        printf("%s\n", ctx->reqPath);

        int bytesSent = send(ctx->connFd, ctx->reqPath, bytesRecv, 0);
        if (bytesSent == -1) {
            perror("server: send");
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