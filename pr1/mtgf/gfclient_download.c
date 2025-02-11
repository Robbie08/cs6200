#include <stdlib.h>
#include <pthread.h>
#include "gfclient-student.h"


#define MAX_THREADS 1024
#define PATH_BUFFER_SIZE 512

// This is our global struct that allows us to perform concurrent transactions
gfclient_pool_t delegate_pool;

delegate_tracker_t tracker;


#define USAGE                                                             \
  "usage:\n"                                                              \
  "  gfclient_download [options]\n"                                       \
  "options:\n"                                                            \
  "  -h                  Show this help message\n"                        \
  "  -s [server_addr]    Server address (Default: 127.0.0.1)\n"           \
  "  -p [server_port]    Server port (Default: 18968)\n"                  \
  "  -w [workload_path]  Path to workload file (Default: workload.txt)\n" \
  "  -t [nthreads]       Number of threads (Default 8 Max: 1024)\n"       \
  "  -n [num_requests]   Request download total (Default: 16)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"workload", required_argument, NULL, 'w'},
    {"nthreads", required_argument, NULL, 't'},
    {"nrequests", required_argument, NULL, 'n'},
    {NULL, 0, NULL, 0}};

static void Usage() { fprintf(stderr, "%s", USAGE); }

static void localPath(char *req_path, char *local_path) {
  static int counter = 0;

  sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static FILE *openFile(char *path) {
  char *cur, *prev;
  FILE *ans;

  /* Make the directory if it isn't there */
  prev = path;
  while (NULL != (cur = strchr(prev + 1, '/'))) {
    *cur = '\0';

    if (0 > mkdir(&path[0], S_IRWXU)) {
      if (errno != EEXIST) {
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }

    *cur = '/';
    prev = cur;
  }

  if (NULL == (ans = fopen(&path[0], "w"))) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

/* Callbacks ========================================================= */
static void writecb(void *data, size_t data_len, void *arg) {
  FILE *file = (FILE *)arg;
  fwrite(data, 1, data_len, file);
}

;

/* Main ========================================================= */
int main(int argc, char **argv) {
  /* COMMAND LINE OPTIONS ============================================= */
  char *workload_path = "workload.txt";
  char *server = "localhost";
  int option_char = 0;
  char *req_path = NULL;
  unsigned short port = 18968;


  int nthreads = 8;
  // int returncode = 0;
  int nrequests = 14;
  char local_path[PATH_BUFFER_SIZE];

  // gfcrequest_t *gfr = NULL;
  FILE *file = NULL;

  setbuf(stdout, NULL);  // disable caching

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:n:hs:t:r:w:", gLongOptions,
                                    NULL)) != -1) {
    switch (option_char) {

      case 's':  // server
        server = optarg;
        break;
      case 'w':  // workload-path
        workload_path = optarg;
        break;
      case 'r': // nrequests
      case 'n': // nrequests
        nrequests = atoi(optarg);
        break;
      case 't':  // nthreads
        nthreads = atoi(optarg);
        break;
      case 'p':  // port
        port = atoi(optarg);
        break;
      default:
        Usage();
        exit(1);


      case 'h':  // help
        Usage();
        exit(0);
    }
  }

  if (EXIT_SUCCESS != workload_init(workload_path)) {
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }
  if (port > 65331) {
    fprintf(stderr, "Invalid port number\n");
    exit(EXIT_FAILURE);
  }
  if (nthreads < 1 || nthreads > MAX_THREADS) {
    fprintf(stderr, "Invalid amount of threads\n");
    exit(EXIT_FAILURE);
  }
  gfc_global_init();


  // This is to hopefully optimize and not spin up unnecessary number of threads.
  if (nrequests < nthreads) {
    nthreads = nrequests;
  }


  int err;
  err = init_delegate_pool(nthreads);
  if (err != 0) {
    perror("client: failed to initialize the delegate pool");
    gfc_global_cleanup();
    exit(1);
  }

  err = init_delegate_tracker();
  if (err != 0) {
    perror("client: failed to initialize the request tracker object");
    gfc_global_cleanup();
    exit(1);
  }

  err = init_threads(nthreads);
  if (err != 0) {
    perror("client: failed to intialize the delgate threads");
    gfc_global_cleanup();
    exit(1);
  }

  /* Build your queue of requests here */
  for (int i = 0; i < nrequests; i++) {
    /* Note that when you have a worker thread pool, you will need to move this
     * logic into the worker threads */
    req_path = workload_get_path();

    if (strlen(req_path) > PATH_BUFFER_SIZE) {
      fprintf(stderr, "Request path exceeded maximum of %d characters\n.", PATH_BUFFER_SIZE);
      exit(EXIT_FAILURE);
    }

    localPath(req_path, local_path);

    file = openFile(local_path);

    delegation_request_t *req = create_delegation_request(req_path, local_path, server, port, file, writecb);
    if (req == NULL) {
      perror("client: failed to create the delegation request struct.");
      gfc_global_cleanup();
      exit(1);
    }

    pthread_mutex_lock(&delegate_pool.q_lock);
    steque_enqueue(&delegate_pool.q_request, req);
    pthread_cond_broadcast(&delegate_pool.q_not_empty);
    pthread_mutex_unlock(&delegate_pool.q_lock);

    /*
     * note that when you move the above logic into your worker thread, you will
     * need to coordinate with the boss thread here to effect a clean shutdown.
     */
  }

  // Push our sentinel request into the queue so that Delegates know when there are
  // no more requests to process
  delegation_request_t *dummy_request = create_sentinel_delegation_request();
  if (dummy_request == NULL) {
    perror("client: failed to create the sentinel delegation request struct.");
    cleanup_threading(nthreads);
    destroy_delegate_pool();
    destroy_delegate_tracker();
    gfc_global_cleanup();
    exit(1);
  }

  pthread_mutex_lock(&delegate_pool.q_lock);
  steque_enqueue(&delegate_pool.q_request, dummy_request);
  pthread_cond_broadcast(&delegate_pool.q_not_empty);
  pthread_mutex_unlock(&delegate_pool.q_lock);

  // Lets just wait until we get the completed signal and there aren't any active delegates
  pthread_mutex_lock(&tracker.active_delegates_lock);
  while (tracker.active_delegates > 0) {
      pthread_cond_wait(&tracker.completed, &tracker.active_delegates_lock);
      // printf("Recvd completed signal; active_delegates count: '%d'\n", tracker.active_delegates);
  }
  pthread_mutex_unlock(&tracker.active_delegates_lock);

  cleanup_threading(nthreads);
  destroy_delegate_pool();
  destroy_delegate_tracker();

  // printf("Starting gfc_global_cleanup!\n");
  gfc_global_cleanup();  /* use for any global cleanup for AFTER your thread
                          pool has terminated. */
  // printf("Completed gfc_global_cleanup!\n");

  return 0;
}

void* delegate_function(void *arg) {
  int returncode = 0;
  // pthread_t thread_id = pthread_self();  // Get the current thread ID

  for (;;) {
    pthread_mutex_lock(&delegate_pool.q_lock);

    // If there aren't any request to handle then go into the wait queue and release the lock
    while(steque_isempty(&delegate_pool.q_request) && !delegate_pool.completed) {
      pthread_cond_wait(&delegate_pool.q_not_empty, &delegate_pool.q_lock);
    }

    

    if (delegate_pool.completed) {
      pthread_mutex_unlock(&delegate_pool.q_lock);
      // printf("Thread '%lu' exiting since completed=true!\n", (unsigned long)thread_id);
      break;
    }

    // Only one thread will receive the sentinel request
    delegation_request_t *req = steque_pop(&delegate_pool.q_request);
    pthread_mutex_unlock(&delegate_pool.q_lock);

    if (req->sentinel) {
      // steque_enqueue(&delegate_pool.q_request, req);
      pthread_mutex_lock(&delegate_pool.q_lock);
      delegate_pool.completed = 1;
      pthread_cond_broadcast(&delegate_pool.q_not_empty);
      pthread_mutex_unlock(&delegate_pool.q_lock);

      destroy_delegation_request(&req);
      // printf("Thread '%lu' detected sentinel request!\n", (unsigned long)thread_id);
      break;
    }
    

    gfcrequest_t *gfr = NULL;
    gfr = gfc_create();
    gfc_set_path(&gfr, req->path);

    gfc_set_port(&gfr, req->port);
    gfc_set_server(&gfr, req->server);
    gfc_set_writearg(&gfr, req->writearg);
    gfc_set_writefunc(&gfr, req->writefunc);

    // fprintf(stdout, "Requesting %s%s\n", req->server, req->path);


    // printf("Thread %lu is processing request for path: %s\n", (unsigned long)thread_id, req->path);
    if (0 > (returncode = gfc_perform(&gfr))) {
      fprintf(stdout, "gfc_perform returned an error %d\n", returncode);
      fclose(req->writearg);
      if (0 > unlink(req->local_path))
        fprintf(stderr, "warning: unlink failed on %s\n", req->local_path);
    } else {
      fclose(req->writearg);
    }

    if (gfc_get_status(&gfr) != GF_OK) {
      if (0 > unlink(req->local_path)) {
        fprintf(stderr, "warning: unlink failed on %s\n", req->local_path);
      }
    }

    fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(&gfr)));
    fprintf(stdout, "Received %zu of %zu bytes\n", gfc_get_bytesreceived(&gfr),
            gfc_get_filelen(&gfr));

    destroy_delegation_request(&req);
    gfc_cleanup(&gfr);
  }

  // Let our Delegator thread know that we've terminated.
  pthread_mutex_lock(&tracker.active_delegates_lock);
  tracker.active_delegates -= 1; // Decrement the counter
  // printf("Thread '%lu' decremented the count to '%d'\n", (unsigned long)thread_id, tracker.active_delegates);
  if (tracker.active_delegates == 0) {
    pthread_cond_signal(&tracker.completed);
  }
  pthread_mutex_unlock(&tracker.active_delegates_lock);
  return NULL;
}

delegation_request_t* create_delegation_request(char *path, char *local_path, char *server, unsigned short port, void* arg, void (*writefunc)(void *data, size_t data_len, void *arg)) {
    delegation_request_t* request = malloc(sizeof(delegation_request_t));
    if (request == NULL) {
        perror("client: failed to allocate memory for the delegation request object");
        return NULL;
    }

    request->path = strdup(path);
    if (request->path == NULL) {
        perror("client: failed to allocate memory for the path field");
        free(request);
        return NULL;
    }

    request->local_path = strdup(local_path);
    if (request->local_path == NULL) {
      perror("client: failed to allocate memory for the local_path field");
      free(request->path);
      free(request);
      return NULL;
    }

    request->server = strdup(server);
    if (request->server == NULL) {
        perror("client: failed to allocate memory for the server field");
        free(request->path);
        free(request->local_path);
        free(request);
        return NULL;
    }

    request->port = port;
    request->sentinel = 0;
    request->writearg = arg;
    request->writefunc = writefunc;
    return request;
}

delegation_request_t* create_sentinel_delegation_request() {
    delegation_request_t* request = malloc(sizeof(delegation_request_t));
    if (request == NULL) {
        perror("client: failed to allocate memory for sentinel delegation request");
        return NULL;
    }

    request->path = NULL;
    request->server = NULL;
    request->local_path = NULL;
    request->port = 0;
    request->writefunc = NULL;
    request->writearg = NULL;
    request->sentinel = 1;  // Set the sentinel flag to mark this request as a dummy

    return request;
}


void destroy_delegation_request(delegation_request_t **request) {
  // printf("Destroying delegation request\n");
    if (request == NULL || *request == NULL) {
        return;
    }

    
    if ((*request)->path != NULL) {
      free((*request)->path);
      // (*request)->path = NULL;
    }
    // printf("freed path\n");

    if ((*request)->local_path != NULL) {
      free((*request)->local_path);
      // (*request)->local_path = NULL;
    }

    // printf("freed local_path\n");

    if ((*request)->server != NULL) {
      free((*request)->server);
      (*request)->server = NULL;
    }

    // printf("freed server\n");

    free(*request);
    // printf("freed request pointer");
    *request = NULL;
    // printf("Successfully destroyed delegation request\n");
    return;
}

int init_threads(size_t numthreads) {
	for (int i = 0; i < numthreads; i++) {
		// we want the delegate threads to be joinable to the delegator thread
		int err = pthread_create(&delegate_pool.pool[i], NULL, delegate_function, NULL);
		if (err != 0) {
			perror("client: pthread_create failed to create delegate thread");
			return -1;
		}
    // Update our counter with the active requests
    // Since the delegates don't access this until after this method is created then we're ok not locking it
    
		// printf("created thread '%d' of '%ld'\n", i+1, numthreads);
    tracker.active_delegates += 1; 
    //printf("Total active_delegates count: '%d'\n", tracker.active_delegates);
	}
  return 0;
}

void cleanup_threading(int nthreads) {
  for (size_t i = 0; i < nthreads; i++) {
    int err = pthread_join(delegate_pool.pool[i], NULL);
    if (err != 0) {
        fprintf(stderr, "Error joining thread %zu: %s\n", i, strerror(err));
    }
    // printf("Successfully joined thread %zu\n", i);
    
  }
}


int init_delegate_pool(size_t numOfDelegates) {
  int err = 0;
	steque_init(&delegate_pool.q_request); // init our queue for the request queue within our delegate pool object
	err = pthread_mutex_init(&delegate_pool.q_lock, NULL); // we must init our lock for the queue
	if (err != 0) {
		perror("client: failed to initialize q_lock mutex");
		return -1;
	}

	err = pthread_cond_init(&delegate_pool.q_not_empty, NULL); // we must init our conditional variable 
	if (err != 0) {
		perror("client: failed to initialize q_not_empty condition variable");
		return -1;
	}

  delegate_pool.completed = 0;

	// printf("successfully initialized delegate pool\n");
	return 0;
}

void destroy_delegate_pool() {
  steque_destroy(&delegate_pool.q_request);
  pthread_mutex_destroy(&delegate_pool.q_lock);
  pthread_cond_destroy(&delegate_pool.q_not_empty);
  // printf("Successfully destroyed delegate pool");
}

int init_delegate_tracker() {
  tracker.active_delegates = 0;
  int err = 0;
  err = pthread_mutex_init(&tracker.active_delegates_lock, NULL);
  if (err != 0) {
    perror("client: failed to initialize the active_request_lock");
    return -1;
  }

  err = pthread_cond_init(&tracker.completed, NULL);
  if (err != 0) {
    perror("client: failed to initialize the completed conditional variable");
    return -1;
  }

  return 0;
}

void destroy_delegate_tracker() {
  pthread_mutex_destroy(&tracker.active_delegates_lock);
  pthread_cond_destroy(&tracker.completed);
}