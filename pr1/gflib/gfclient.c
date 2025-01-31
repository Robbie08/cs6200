
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

  //// printf("Destorying gfcrequest_t object\n");
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
    return NULL;
  }

  memset(config, 0, sizeof(gfcrequest_t));
  config -> sockfd = -1; // to allow for error detection during socket creation
  config -> port = 0;
  config -> bytesRecvd = 0;
  config -> parsedHeader = 0;
  config -> respStatus = GF_OK;

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

  int addrinfoStatus;
  struct addrinfo *addressesList;
  addrinfoStatus = getaddrinfo((*gfr)->server, portStr, &addrConfig, &addressesList);
  if (addrinfoStatus != 0) {
      // Send error to stderr and stop the program since ther's no point to continue if getaddrinfo fails
      fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(addrinfoStatus));
      return -1;
  }

  (*gfr)->sockfd = createSocketAndConnect(addressesList);
  freeaddrinfo(addressesList); // we don't need the linked list anymore, so let's free it up
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
  size_t totalHeaderBytes = 0;
  char headerBuff[BUFSIZ]; 
  memset(&headerBuff, 0 , sizeof headerBuff);

  while((bytesRecvd = recv((*gfr)->sockfd, (*gfr)->response, BUFSIZ, 0)) > 0) {
    if (totalHeaderBytes + bytesRecvd >= BUFSIZ) {
      perror("client: header exceeds the BUFSIZE.");
      (*gfr)->respStatus = GF_OK;
      return 0;
    }

    strncat(headerBuff, (*gfr)->response, bytesRecvd); // append the response to the headerBuff so that we can analyze
    totalHeaderBytes += bytesRecvd;

    if (strstr(headerBuff, "\r\n\r\n") != NULL){
      break; // if we received the delimeter then we have our header and can start parsing
    }
  }

  // It's possible we got 0 or -1 before we transfered the entire header
  if (bytesRecvd == -1) {
    perror("client: recv got -1 indicating some issue with the transfer");
    (*gfr)->respStatus = GF_INVALID;
    return -1;
  } else if (bytesRecvd == 0 && strstr(headerBuff, "\r\n\r\n") == NULL) {
    perror("client: the server terminated the connection during transfer of the message header");
    (*gfr)->respStatus = GF_INVALID;
    return -1;
  }

  // printf("------- parsing header START--------\n");
  gfstatus_t status = parseResponseHeader(gfr, (*gfr)->response, bytesRecvd);
  // printf("------- parsing header DONE --------\n");
  if (status == GF_INVALID) {
    perror("client: issue with receiving the response from server.");
    return -1;
  } else if (status == GF_FILE_NOT_FOUND || status == GF_ERROR) {
    return 0; // We should return 0 in these cases
  }

  char *contentStart = strstr(headerBuff, "\r\n\r\n");
  // since there are 4 delimiting chars we need to move up 4 to get to the content
  contentStart += 4; 
  size_t headerSize = contentStart - headerBuff;
  size_t contentBytes = totalHeaderBytes - headerSize;

  // Process the first chunk of content
  if (contentBytes > 0) {
      (*gfr)->writefunc((void *)contentStart, contentBytes, (*gfr)->writearg);
      (*gfr)->bytesRecvd += contentBytes;
  }

  // At this point all we need to do is get the actual content so we just keep looping until we get 0
  while ((bytesRecvd = recv((*gfr)->sockfd, (*gfr)->response, BUFSIZ, 0)) > 0) {
    (*gfr)->writefunc((void *)(*gfr)->response, bytesRecvd, (*gfr)->writearg);
    (*gfr)->bytesRecvd += bytesRecvd;

    if ((*gfr)->bytesRecvd >= (*gfr)->fileLen) {
      break;  // Stop when file length is reached
    }
  }

  // Validate any error scenarios from recv() like:
  // 1) Got a generic issu with transfering data
  // 2) Got a disconnect before sending all bytes
  if (bytesRecvd == -1) {
    perror("client: recv got -1 indicating some issue with the transfer");
    (*gfr)->respStatus = GF_INVALID;
    return -1;
  } else if (bytesRecvd == 0) {
    if((*gfr)->bytesRecvd != (*gfr)->fileLen) {
      perror("client: the server terminated the connection during the transfer of message body");
      (*gfr)->respStatus = status;
      return -1;
    } else {
      (*gfr)->respStatus = GF_OK;
    }
  } 
  // Ensure proper cleanup
  close((*gfr)->sockfd);
  (*gfr)->sockfd = -1;

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
        return -1;
    }

    return sockfd;
}

// <scheme> <status> <length>\r\n\r\n<content>
// This method should parse the header and get a couple of things.
// 1. Store the response code in gfr
// 2. If the status is OK, store the file length in gfr
gfstatus_t parseResponseHeader(gfcrequest_t **gfr, const char* response, ssize_t bytesRecvd) {
  if (gfr == NULL || *gfr == NULL || response == NULL) {
    (*gfr) ->respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }

 // printf("checking GETFILE is there\n");
  const char *prefix = "GETFILE";
  int prefixLen = strlen(prefix);
  if (strncmp(response, prefix, prefixLen) != 0) {
    perror("client: the server returned an incompatible response header");
    (*gfr)->respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }


 // printf("checking delimiters are present\n");
  if (strstr(response, "\r\n\r\n") == NULL){
    perror("client: response didn't contain the '\r\n\r\n' suffix.");
    (*gfr)->respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }

  // let's ensure that we only have 2 spaces in the request since it's an OK status
 // printf("Checking the number of spaces in the string");
  const char *str = response;
  int spaceCount = 0;
  while((str = strchr(str, ' ')) != NULL) {
      spaceCount++;
      str++;
  }

 // printf("The number of spaces is: %d\n", spaceCount);

  if (spaceCount > 2 || spaceCount < 1) {
    (*gfr) -> respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }


  // OK status header:    <scheme> <status> <length>\r\n\r\n<content>
  // !OK sttatus header:  <scheme> <status>\r\n\r\n

 // printf("Extracting the status\n");
  char *statusStart = strchr(response, ' '); // points to the first space which should be after the <scheme>
  char *statusEnd;
  if(statusStart != NULL && spaceCount == 2) {
    statusEnd = strchr(statusStart+1, ' '); // points to the next space which should be after the <status>
  } else if (statusStart != NULL && spaceCount == 1) {
    statusEnd = strchr(statusStart+1, '\r'); // points to the start of '\r\n\r\n'
  }
  
  statusStart++; // move up to the first char of the status code
  if (statusEnd == NULL) {
    perror("client: statusEnd is NULL, invalid response format");
    (*gfr)->respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }
  size_t statusLen = statusEnd - statusStart;

  char extractedStatus[16];
  memset(&extractedStatus, 0, sizeof(extractedStatus));

 // printf("Ensuring that the statusLen of '%zu' doesn't exceede '%zu'\n", statusLen, sizeof extractedStatus);
  if (statusLen >= sizeof extractedStatus){
    (*gfr)->respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }

  strncpy(extractedStatus, statusStart, statusLen);
  extractedStatus[statusLen] = '\0';
 // printf("status recv: '%s'\n", extractedStatus);

  // Map the status string to enum
  if (strcmp(extractedStatus, "OK") == 0) {
      (*gfr)->respStatus = GF_OK;
  } else if (strcmp(extractedStatus, "FILE_NOT_FOUND") == 0) {
      (*gfr)->respStatus = GF_FILE_NOT_FOUND;
      return (*gfr)->respStatus;
  } else if (strcmp(extractedStatus, "ERROR") == 0) {
      (*gfr)->respStatus = GF_ERROR;
      return (*gfr)->respStatus;
  } else {
      (*gfr)->respStatus = GF_INVALID;
      return (*gfr)->respStatus;
  }

  char *fileLenStart = statusEnd+1;
  char *fileLenEnd = strchr(fileLenStart, '\r');

  // extract the length
  char extractedFileLen[32];
  memset(&extractedFileLen, 0, 32);
  size_t fileLenLength = fileLenEnd - fileLenStart;
  strncpy(extractedFileLen, fileLenStart, fileLenLength);

  extractedFileLen[fileLenLength] = '\0';

  if ((*gfr)->respStatus != GF_OK) {
    (*gfr)->fileLen = 0;
    return (*gfr)->respStatus;
  }


  if (sscanf(extractedFileLen, "%zu", &(*gfr)->fileLen) != 1){
    (*gfr)->respStatus = GF_INVALID;
    return (*gfr)->respStatus;
  }

  return (*gfr)->respStatus; 
}
