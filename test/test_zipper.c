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
     ThreadState *t = (ThreadState*)arg;

     pthread_barrier_wait(&start_barrier);
     int i;
     for(i = 0; i < 100; i++) {
	  lseek(t->fd, i*t->thread_count + t->id, SEEK_SET);
	  write(t->fd, &t->id, 1);
	  pthread_barrier_wait(&barrier1);
	  fprintf(stderr, "Thread %d completed iteration %d\n", t->id, i);
     }
     return NULL;
}


int do_test_eval(int argc, char* argv[]) {

     
     int thread_count;
     if (argc > 1) {
	  thread_count = atoi(argv[1]);
     } else {
	  thread_count = 4;
     }
     fprintf(stderr, "Running test with %d threads\n", thread_count);
     
     pthread_barrier_init(&barrier1, NULL, thread_count);
     pthread_barrier_init(&barrier2, NULL, thread_count);
     pthread_barrier_init(&start_barrier, NULL, thread_count);
     
     ThreadState threads[thread_count];

     int i;
     for (i = 0; i < thread_count; i++) {
	  threads[i].fd = open(DEFAULT_FILENAME1, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);
	  if (threads[i].fd == -1) {
	       perror("test");
	       exit(1);
	  }
	  threads[i].id = i;
	  threads[i].thread_count = thread_count;
	  pthread_create(&threads[i].thread, NULL, Go, &threads[i]);
     }

     for (i = 0; i < thread_count; i++) {
	  pthread_join(threads[i].thread, NULL);
     }
     
     return 0;
     
}
