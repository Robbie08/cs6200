#include "proxy-student.h"
#include "gfserver.h"



#define MAX_REQUEST_N 512
#define BUFSIZE (6226)

ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void* arg){
	(void) ctx;
	(void) arg;
	(void) path;
	//Your implementation here
	errno = ENOSYS;
	return -1;	
}

/*
 * We provide a dummy version of handle_with_file that invokes handle_with_curl as a convenience for linking!
 */
ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg){
	return handle_with_curl(ctx, path, arg);
}	
