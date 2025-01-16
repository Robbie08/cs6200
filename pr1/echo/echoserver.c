#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFSIZE 1024

#define USAGE                                                        \
    "usage:\n"                                                         \
    "  echoserver [options]\n"                                         \
    "options:\n"                                                       \
    "  -p                  Port (Default: 48593)\n"                    \
    "  -m                  Maximum pending connections (default: 5)\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port",          required_argument,      NULL,           'p'},
    {"help",          no_argument,            NULL,           'h'},
    {"maxnpending",   required_argument,      NULL,           'm'},
    {NULL,            0,                      NULL,             0}
};


int main(int argc, char **argv) {
    int portno = 48593; /* port to listen on */
    int option_char;
    int maxnpending = 5;
  
    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'm': // server
            maxnpending = atoi(optarg);
            break; 
        case 'h': // help
            fprintf(stdout, "%s ", USAGE);
            exit(0);
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;                                        
        default:
            fprintf(stderr, "%s ", USAGE);
            exit(1);
        }
    }

    setbuf(stdout, NULL); // disable buffering

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }
    if (maxnpending < 1) {
        fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__, maxnpending);
        exit(1);
    }
    
    /* Socket Code Here */
    int status; // holds status that we use to check for errs
    struct addrinfo hints;
    struct addrinfo *servinfo; // points to the result
    struct addrinfo *curr;

    int sockfd, newfd;
    int yes = 1;


    // We want to ensure the struct is zero'd out and empty
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // to allow both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM; // Since we want to make this a TCP socket
    hints.ai_flags = AI_PASSIVE; // Tells getaddrinfo() to assign local host to the socket structures

    if ((status = getaddrinfo(NULL, portno, &hints, &servinfo)) != 0) {
        // Send error to stderr and stop the program since ther's no point to continue if getaddrinfo fails
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    // At this point the servinfo now points to the linked list of 1 or more struct addrinfos
    // Create socket and bind to the first result that the linked list has
    for (curr = servinfo; curr != NULL; curr = curr->ai_next) {

        // Attempt to create a socket until success
        if ((sockfd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        // If our port is still in use then lets just force it by allowing our program to use reuse it
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("server: setsockopt");
            exit(1); // No point in continuing if for some reason we can't reuse the port since subsequent code will fail
        }

        if (bind(sockfd, curr->ai_addr, curr->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue; // Just becuase this one failed to bind doesn't mean there isn't another one available
        }

        break; // if we made it this far then we've created a socket and associated it with a port number on our machine.
    }

    // Once we're done with servinfo let's free up the linked list
    freeaddrinfo(servinfo);

    if (curr == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, maxnpending) == -1) {
        perror("server: listen");
        exit(1);
    }

    // Adding this to seee if it's trackerd
} 
