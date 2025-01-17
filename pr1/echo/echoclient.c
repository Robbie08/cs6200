#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>

/* Be prepared accept a response of this length */
#define BUFSIZE 1024
#define MAX_MSG_SIZE 16

#define USAGE                                                                       \
    "usage:\n"                                                                      \
    "  echoclient [options]\n"                                                      \
    "options:\n"                                                                    \
    "  -p                  Port (Default: 48593)\n"                                  \
    "  -s                  Server (Default: localhost)\n"                           \
    "  -m                  Message to send to server (Default: \"Hello Spring!!\")\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"message", required_argument, NULL, 'm'},
    {"port", required_argument, NULL, 'p'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    char *message = "Hello Spring!!";
    unsigned short portno = 48593;
    char *hostname = "localhost";

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:m:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'm': // message
            message = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        }
    }

    setbuf(stdout, NULL); // disable buffering

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    if (NULL == message) {
        fprintf(stderr, "%s @ %d: invalid message\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == hostname) {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    /* Socket Code Here */
    struct addrinfo addressConfig;
    
    // Zero out and set up our address config
    memset(&addressConfig, 0, sizeof addressConfig);
    addressConfig.ai_family = AF_UNSPEC;
    addressConfig.ai_socktype = SOCK_STREAM;

    char portNoStr[32];
    memset(&portNoStr, 0, sizeof portNoStr);
    sprintf(portNoStr, "%d", portno);

    int status;
    struct addrinfo *addressesList;

    status = getaddrinfo(hostname, portNoStr, &addressConfig, &addressesList);
    if (status == -1) {
        perror("client: getaddrinfo");
        exit(1);
    }

    int sockfd;
    int err; 
    struct addrinfo *curr;

    // iterate over linked list until we find a connection
    for (curr = addressesList; curr != NULL; curr = curr->ai_next) {
        sockfd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol);
        if (sockfd == -1){
            perror("client: socket");
            continue;
        }

        err = connect(sockfd, curr->ai_addr, curr->ai_addrlen);
        if (err == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break; // we've connected another machine through the socket
    }

    if (curr == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    freeaddrinfo(addressesList); // we don't need the linked list anymore, so let's free it up

    // We can now start talking with our server
    int msgLen = strlen(message);
    int bytesSent = 0 , bytesRead = 0;
    
    // printf("client: msg sent - %s\n", message);

    // Since we are setting the length as the msgLen, it's possible that it exceeds the max length allowed on the server
    bytesSent = send(sockfd, message, msgLen, 0);
    if (bytesSent == -1) {
        perror("client: send");
        close(sockfd);
        exit(1);
    }

    char msgBuff[MAX_MSG_SIZE];
    memset(&msgBuff, 0, sizeof msgBuff); // zero out the buffer to ensure we're clean

    // read message from our socket connection
    bytesRead = recv(sockfd, msgBuff, MAX_MSG_SIZE, 0);
    if (bytesRead == -1) {
        perror("client: recv");
        close(sockfd);
        exit(1);
    } else if (bytesRead == 0) {
        // When this happens that means the other machine on the other end closed the connection
        perror("client: recv - server closed connection");
        close(sockfd);
        exit(1);
    }

    if (bytesRead < MAX_MSG_SIZE) {
        msgBuff[bytesRead] = '\0';
    }

    printf("%s", msgBuff);

    close(sockfd);
    return 0;
}
