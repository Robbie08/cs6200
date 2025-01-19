#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#define BUFSIZE 512
#define NETWORK_BUFF_SIZE 1024
#define MAX_PORT_SIZE 6

#define USAGE                                                \
  "usage:\n"                                                 \
  "  transferclient [options]\n"                             \
  "options:\n"                                               \
  "  -p                  Port (Default: 23948)\n"            \
  "  -s                  Server (Default: localhost)\n"      \
  "  -h                  Show this help message\n"           \
  "  -o                  Output file (Default cs6200.txt)\n" 

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"output", required_argument, NULL, 'o'},
    {"server", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

// Function prototype declarations
int createSocketAndConnect(struct addrinfo *addressesList);
void saveFileSentByServer(int sockfd, int fileFd);

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    unsigned short portno = 23948;
    char *hostname = "localhost";
    char *filename = "cs6200.txt";

    setbuf(stdout, NULL);

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:o:hx", gLongOptions, NULL)) != -1) {
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
        case 'o': // filename
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        }
    }

    if (NULL == hostname) {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == filename) {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    /* Socket Code Here */
    struct addrinfo addressConfig;
    
    // Zero out and set up our address config
    memset(&addressConfig, 0, sizeof addressConfig);
    addressConfig.ai_family = AF_UNSPEC;
    addressConfig.ai_socktype = SOCK_STREAM;

    char portNoStr[MAX_PORT_SIZE];
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
    sockfd = createSocketAndConnect(addressesList);

    // We are connected so the server will start sending us the file
    // let's create a file and start appending the contents that were sent
    int fileFd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fileFd == -1) {
        perror("client: open");
        close(sockfd);
        return 1;
    }

    saveFileSentByServer(sockfd, fileFd);

    close(fileFd);
    close(sockfd);
    return 0;
}

// This method creates a socket and connects with the first available server address, provided by the addressesList(linked list).
// Returns the socket's file descriptor if successfully connected, otherwise terminates the program.
int createSocketAndConnect(struct addrinfo *addressesList) {
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
        perror("client: failed to connect");
        freeaddrinfo(addressesList);
        exit(2);
    }

    freeaddrinfo(addressesList); // we don't need the linked list anymore, so let's free it up
    return sockfd;
}

// This method reads the file sent by the server and saves it to the specified file. It's possible that
// the file is too large to be sent in one network transaction, so we need to keep reading from the socket,
// appending the contents into the file until the server closes the connection.
void saveFileSentByServer(int sockfd, int fileFd) {
    char buff[NETWORK_BUFF_SIZE];
    memset(&buff, 0, NETWORK_BUFF_SIZE);

    ssize_t bytesRecvd, bytesWritten;

    // Read contents until server closes connection (until recv returns 0)
    while((bytesRecvd = recv(sockfd, buff, NETWORK_BUFF_SIZE, 0)) > 0) {
        // write the recvd data into the file
        bytesWritten = write(fileFd, buff, bytesRecvd);
        if (bytesWritten == -1) {
            perror("client: write");
            close(fileFd);
            close(sockfd);
            exit(1);
        }
    }
    
    if (bytesRecvd == -1) {
        perror("client: recv");
    }
}