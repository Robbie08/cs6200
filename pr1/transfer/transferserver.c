#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <getopt.h>
#include <fcntl.h>

#define BUFSIZE 512
#define NETWORK_BUFF_SIZE 1024
#define MAX_PORT_SIZE 6

#define USAGE                                                \
    "usage:\n"                                               \
    "  transferserver [options]\n"                           \
    "options:\n"                                             \
    "  -f                  Filename (Default: 6200.txt)\n"   \
    "  -p                  Port (Default: 23948)\n"          \
    "  -h                  Show this help message\n"         \

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"filename", required_argument, NULL, 'f'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

int createAndBindSocket(struct addrinfo *adressesList);
void sendFileContents(int filefd, int connectionFd);

int main(int argc, char **argv)
{
    int option_char;
    char *filename = "6200.txt"; /* file to transfer */
    int portno = 23948;             /* port to listen on */
    int maxnpending = 5;

    setbuf(stdout, NULL); // disable buffering

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:hf:x", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        case 'f': // file to transfer
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        }
    }


    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }
    
    if (NULL == filename) {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }    

    /* Socket Code Here */
    struct addrinfo addrConfig;

    // We want to ensure the struct is zero'd out and empty
    memset(&addrConfig, 0, sizeof addrConfig);
    addrConfig.ai_family = AF_UNSPEC; // to allow both IPv4 and IPv6
    addrConfig.ai_socktype = SOCK_STREAM; // Since we want to make this a TCP socket
    addrConfig.ai_flags = AI_PASSIVE; // Tells getaddrinfo() to assign local host to the socket structures

    // convert port that's an int to string 
    char portNoStr[MAX_PORT_SIZE];
    memset(&portNoStr, 0, sizeof portNoStr);
    sprintf(portNoStr, "%d", portno);

    int status; // holds status that we use to check for errs
    struct addrinfo *adressesList; // points to the linked list containing the socket addresses resolved by getaddrinfo

    status = getaddrinfo(NULL, portNoStr, &addrConfig, &adressesList);
    if (status != 0) {
        // Send error to stderr and stop the program since ther's no point to continue if getaddrinfo fails
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    // At this point the adressesList now points to the linked list of 1 or more struct addrinfos
    int sockfd;
    sockfd = createAndBindSocket(adressesList);

    int err = 0;
    err = listen(sockfd, maxnpending);
    if (err == -1) {
        perror("server: listen");
        close(sockfd);
        exit(1);
    }

    int filefd = open(filename, O_RDONLY);
    if (filefd == -1) {
        perror("server: open");
        close(sockfd);
        exit(1);
    }

    int newConFd;
    socklen_t addrSize;
    struct sockaddr_storage connectorsAddress; // usng sockaddr_storage since its big enough to hold IPv4 and IPv6 addresses

    for(;;) {
        addrSize = sizeof connectorsAddress;

        // Attempt to connect with the connection (blocking call)
        newConFd = accept(sockfd, (struct sockaddr *)&connectorsAddress, &addrSize);
        if (newConFd == -1) {
            perror("server: accept");
            continue;
        }

        // Now that we're connected, let's send the contents of the file
        sendFileContents(filefd, newConFd);
        
        close(newConFd);
        lseek(filefd, 0, SEEK_SET); // reset the filefd to the start of the file so that we can read from the start the next time around
    }

    close(filefd);
    close(sockfd);
    return 0;
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


// This method writes the file's content based on the file's file descriptor and writes them into the
// connection's file descriptor in chunks. It's possible we cannot fit all the contents of the file
// in one network transaction so we need keep sending chunks until we've sent all the file's contents.
void sendFileContents(int filefd, int connectionFd) {
    char buff[NETWORK_BUFF_SIZE];
    memset(&buff, 0, NETWORK_BUFF_SIZE);
    size_t bytesRead, bytesSent;
    while ((bytesRead = read(filefd, buff, sizeof(buff))) > 0) {
        char *bufPtr = buff; // Allows me to keep track of the next chunk of bytes I need to send
        ssize_t bytesToSend = bytesRead;
        while(bytesToSend > 0) {
            bytesSent = send(connectionFd, bufPtr, bytesToSend, 0);
            if (bytesSent == -1) {
                perror("server: send");
                close(connectionFd);
                continue;
            } else if (bytesSent == 0) {
                perror("server: send failed because the client closed the connection.");
                close(connectionFd);
                continue;
            }
            bytesToSend -= bytesSent; // subtract the bytes we sent from the total bytes we need to send
            bufPtr += bytesSent; // move our bufPtr to the start of next chunk
        }
    }

    if (bytesRead == -1) {
        perror("server: send");
    }
}
