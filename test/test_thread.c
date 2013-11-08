#include<pthread.h>
#include "test_common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include<fcntl.h>
#include<assert.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>



pthread_barrier_t start_barrier;
pthread_barrier_t barrier1;
pthread_barrier_t barrier2;

typedef struct {
     int fd;
     char id;
     int thread_count;
     pthread_t thread;
} ThreadState;

void * Go(void *arg) {
     return NULL;
}


int do_test_eval(int argc, char* argv[]) {

     
     int thread_count;
     if (argc > 1) {
	  thread_count = atoi(argv[1]);
     } else {
	  thread_count = 1;
     }
     fprintf(stderr, "Running test with %d threads\n", thread_count);
     
     ThreadState threads[thread_count];

     int i;
     for (i = 0; i < thread_count; i++) {
	  pthread_create(&threads[i].thread, NULL, Go, &threads[i]);
     }

     for (i = 0; i < thread_count; i++) {
	  pthread_join(threads[i].thread, NULL);
     }
     
     return 0;
     
}
