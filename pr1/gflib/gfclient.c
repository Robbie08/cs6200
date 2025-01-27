
#include <stdlib.h>
#include <netdb.h>

#include "gfclient-student.h"

#define MAX_PORT_DIGITS 6

 // Modify this file to implement the interface specified in
 // gfclient.h.


struct gfcrequest_t {
  int sockfd;             // The socket's file descriptor between server and client
  unsigned short port;    // Port number that the server is listening on
  const char* path;       // Path of the file we are requesting from server
  const char* server;     // Address of the server
  void *writearg;         // The write arg for the registered writefunc callback
  void *headerarg;        // The header arg for the registered headercallback
  size_t bytesRecvd;      // The number of bytes received
  size_t fileLen;         // The length of the file we are receiving from server
  gfstatus_t respStatus;  // The response status sent from the server
  int parsedHeader;       // This flag lets us know if for each request we've parsed the header
  char response[BUFSIZ];  // This buffer stores the response provided by the client.


  // Function ptr for the registered callback for the headerfunc
  void (*headerfunc)(void *header_buffer, size_t header_buffer_length, void *handlerarg);

  // Function ptr for the registered callback for the writefunc
  void (*writefunc)(void *data_buffer, size_t data_buffer_length, void *handlerarg);
};

// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t **gfr) {
  if (gfr == NULL || *gfr == NULL) {
    return;
  }

  // printf("Destorying gfcrequest_t object\n");
  if ((*gfr)->sockfd != -1) {
    close((*gfr)->sockfd);
  }
  
  free(*gfr);
  *gfr = NULL; // to prevent dangling ptr
  //printf("Successfully destoryed gfcrequest_t object\n");
}

gfcrequest_t *gfc_create() {
  gfcrequest_t* config = malloc(sizeof(gfcrequest_t));
  if (config == NULL) {
    perror("gfc_create: failed to allocate memory for the gfcrequest_t object");
    exit(1);
  }

  memset(config, 0, sizeof(gfcrequest_t));
  config -> sockfd = -1; // to allow for error detection during socket creation
  config -> port = 0;
  config -> bytesRecvd = 0;
  config -> parsedHeader = 0;

  memset(&config->response, 0, BUFSIZ);

  return config;
}

size_t gfc_get_bytesreceived(gfcrequest_t **gfr) {
  // not yet implemented
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_get_bytesreceived: gfr or *gfr is NULL");
    return -1;
  }

  return (*gfr)->bytesRecvd;
}

size_t gfc_get_filelen(gfcrequest_t **gfr) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_get_filelen: gfr or *gfr is NULL");
    return -1;
  }

  return (*gfr)->fileLen;
}

gfstatus_t gfc_get_status(gfcrequest_t **gfr) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_get_status: gfr or *gfr is NULL");
    return -1;
  }

  return (*gfr)->respStatus;
}

void gfc_global_init() {
  // not yet implemented
}

void gfc_global_cleanup() {
  // not yet implemented
}

int gfc_perform(gfcrequest_t **gfr) {
  struct addrinfo addrConfig;

  // Zero out and set up our address config
  memset(&addrConfig, 0, sizeof addrConfig);
  addrConfig.ai_family = AF_UNSPEC;
  addrConfig.ai_socktype = SOCK_STREAM; // Since we want to make this a TCP socket
  addrConfig.ai_socktype = SOCK_STREAM;

  char portStr[MAX_PORT_DIGITS];
  memset(&portStr, 0, sizeof portStr);
  sprintf(portStr, "%d", (*gfr)->port);

  int status;
  struct addrinfo *addressesList;
  status = getaddrinfo((*gfr)->server, portStr, &addrConfig, &addressesList);
  if (status != 0) {
      // Send error to stderr and stop the program since ther's no point to continue if getaddrinfo fails
      fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
      exit(1);
  }

  (*gfr)->sockfd = createSocketAndConnect(addressesList);
  if ((*gfr)->sockfd == -1) {
    perror("client: createSocketAndConnect");
    return -1;
  }

  // Step 1: Send request to the server
  char request[BUFSIZ];
  snprintf(request, sizeof(request), "GETFILE GET %s\r\n\r\n", (*gfr)->path);

  ssize_t bytesSent = send((*gfr)->sockfd, request, strlen(request), 0);
  if (bytesSent == -1) {
      perror("client: send failed");
      close((*gfr)->sockfd);
      return -1;
  }
  
  ssize_t bytesRecvd;
  while((bytesRecvd = recv((*gfr)->sockfd, (*gfr)->response, BUFSIZ, 0)) > 0) {
    // write the data received into the file. Of course, we don't want the header back
    // so we need to parse the response, if OK status then we write the contents to the file
    if (bytesRecvd < BUFSIZ) {
        (*gfr)->response[bytesRecvd] = '\0';
    }

    if ((*gfr)->parsedHeader == 0) {
      
      // Here is a valid generic response template:
      // <scheme> <status> <length>\r\n\r\n<content>
      // This method should parse the header and get a couple of things.
      // 1. Store the response code in gfr
      // 2. If the status is OK, store the file length in gfr
      
      parseResponseHeader(gfr, (*gfr)->response, bytesRecvd);
      if ((*gfr)->respStatus != GF_OK) {
        perror("client: response was a non 200 code");
        return -1;
      }
      (*gfr)->parsedHeader = 1;

      // we need to move our ptr to the start of the content which is based on the
      // generic response "<scheme> <status> <length>\r\n\r\n<content>"
      // Based on this format, we know that the content starts after the "\r\n\r\n"
      // and up until we don't get any more content.
      char *contentStart = strstr((*gfr)->response, "\r\n\r\n");
      if (contentStart == NULL) {
        perror("client: missing content delimiter '\r\n\r\n'");
        return -1;
      }

      contentStart += 4; // since there are 4 delimiting chars

      // Subtract the header size from the bytes received so that we know how many bytes to write
      size_t headerSize = contentStart - (*gfr)->response;
      size_t contentBytes = bytesRecvd - headerSize;

      // we can write our first chunk to disk
      if (contentBytes > 0) {
        (*gfr)->writefunc((void *)contentStart, contentBytes, (*gfr)->writearg);
      }

      (*gfr)->parsedHeader = 1; // ensures we don't come into this block again
      (*gfr) -> bytesRecvd += contentBytes;
      continue;
    }
    // Call write callback for subsequent chunks
    (*gfr)->writefunc((void *)(*gfr)->response, bytesRecvd, (*gfr)->writearg);
    (*gfr) -> bytesRecvd += bytesRecvd;
  }

  if (bytesRecvd == -1) {
    perror("client: recv");
    return -1;
  }

  return 0;
}

void gfc_set_port(gfcrequest_t **gfr, unsigned short port) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_port: gfr or *gfr is NULL");
    return;
  }
  (*gfr) -> port = port;
}

void gfc_set_headerarg(gfcrequest_t **gfr, void *headerarg) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_headerarg: gfr or *gfr is NULL");
    return;
  }
  (*gfr)->headerarg = headerarg;
}

void gfc_set_writearg(gfcrequest_t **gfr, void *writearg) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_writearg: gfr or *gfr is NULL");
    return;
  }
  (*gfr)->writearg = writearg;
}

void gfc_set_server(gfcrequest_t **gfr, const char *server) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_server: gfr or *gfr is NULL");
    return;
  }
  (*gfr)->server = server;
}

void gfc_set_path(gfcrequest_t **gfr, const char *path) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_path: gfr or *gfr is NULL");
    return;
  }
  (*gfr)->path = path;
}

void gfc_set_headerfunc(gfcrequest_t **gfr, void (*headerfunc)(void *, size_t, void *)) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_headerfunc: gfr or *gfr is NULL");
    return;
  }
  (*gfr)->headerfunc = headerfunc;
}

void gfc_set_writefunc(gfcrequest_t **gfr, void (*writefunc)(void *, size_t, void *)) {
  if (gfr == NULL || *gfr == NULL) {
    perror("gfc_set_writefunc: gfr or *gfr is NULL");
    return;
  }
  (*gfr)->writefunc = writefunc;
}

const char *gfc_strstatus(gfstatus_t status) {
  const char *strstatus = "UNKNOWN";

  switch (status) {

    case GF_FILE_NOT_FOUND: {
      strstatus = "FILE_NOT_FOUND";
    } break;

    case GF_OK: {
      strstatus = "OK";
    } break;

   case GF_INVALID: {
      strstatus = "INVALID";
    } break;
   
   case GF_ERROR: {
      strstatus = "ERROR";
    } break;

  }

  return strstatus;
}

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

      // <scheme> <status> <length>\r\n\r\n<content>
      // This method should parse the header and get a couple of things.
      // 1. Store the response code in gfr
      // 2. If the status is OK, store the file length in gfr
void parseResponseHeader(gfcrequest_t **gfr, const char* response, ssize_t bytesRecvd) {
  if (gfr == NULL || *gfr == NULL || response == NULL) {
    (*gfr) ->respStatus = GF_INVALID;
    return;
  }

  const char *prefix = "GETFILE";
  int prefixLen = strlen(prefix);
  if (strncmp(response, prefix, prefixLen) != 0) {
    perror("client: the server returned an incompatible response header");
    (*gfr)->respStatus = GF_INVALID;
    return;
  }

  if (strstr(response, "\r\n\r\n") == NULL){
      printf("Didn't contain the '\r\n\r\n' suffix.\n");
      (*gfr)->respStatus = GF_INVALID;
      return;
  }

  // let's ensure that we only have 2 spaces in the request
  const char *str = response;
  int spaceCount = 0;
  while((str = strchr(str, ' ')) != NULL) {
      spaceCount++;
      str++;
  }

  if (spaceCount != 2) {
      printf("number of spaces is: '%d' but should be '2'\n", spaceCount);
      (*gfr)->respStatus = GF_INVALID;
      return;
  }


  char *statusStart = strchr(response, ' '); // points to the first space which should be after the <scheme>
  char *statusEnd;
  if(statusStart != NULL) {
    statusEnd = strchr(statusStart+1, ' '); // points to the next space which should be after the <status>
  }
  statusStart++; // move up to the first char of the status code

  size_t statusLen = statusEnd - statusStart;

  if (statusLen >= sizeof((*gfr)->respStatus)){
    (*gfr)->respStatus = GF_INVALID;
    return;
  }

  char extractedStatus[16];
  memset(&extractedStatus, 0, sizeof(extractedStatus));
  strncpy(extractedStatus, statusStart, statusLen);
  extractedStatus[statusLen] = '\0';

  // Map the status string to enum
  if (strcmp(extractedStatus, "OK") == 0) {
      (*gfr)->respStatus = GF_OK;
  } else if (strcmp(extractedStatus, "FILE_NOT_FOUND") == 0) {
      (*gfr)->respStatus = GF_FILE_NOT_FOUND;
  } else if (strcmp(extractedStatus, "ERROR") == 0) {
      (*gfr)->respStatus = GF_ERROR;
  } else {
      (*gfr)->respStatus = GF_INVALID;
      return;
  }

  if ((*gfr)->respStatus != GF_OK) {
    perror("client: response was a non 200 status code.");
    return;
  }

  char *fileLenStart = statusEnd+1;
  char *fileLenEnd = strchr(fileLenStart, '\r'); // This is the 

  // extract the length
  char extractedFileLen[32];
  memset(&extractedFileLen, 0, 32);
  size_t fileLenLength = fileLenEnd - fileLenStart;
  strncpy(extractedFileLen, fileLenStart, fileLenLength);

  extractedFileLen[fileLenLength] = '\0';

  if (sscanf(extractedFileLen, "%zu", &(*gfr)->fileLen) != 1){
    (*gfr)->respStatus = GF_INVALID;
    return;
  }
}