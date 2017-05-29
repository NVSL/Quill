#include "test_common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include<fcntl.h>
#include<assert.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include<unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include"FastRand.hpp"
typedef struct {
     int fd;
     char id;
     int thread_count;
     pid_t pid;
} ThreadState;

void * Go(void *arg) {
     ThreadState *t = (ThreadState*)arg;

     uint16_t seed = t->id;
    
#define ITERATIONS  10000
     //(1 << BITS)
     int k;
     for(k = 0; k < ITERATIONS; k++) {
	  uint16_t i = RandLFSR16(&seed) % 1000;
	  assert((i*t->thread_count + t->id)>0);
	  lseek(t->fd, i*t->thread_count + t->id, SEEK_SET);
	  write(t->fd, &t->id, 1);
	  //	  fprintf(stderr, "Thread %d completed iteration %d\n", t->id, k);
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
     
     ThreadState threads[thread_count];

     int i;
     for (i = 0; i < thread_count; i++) {
	  threads[i].fd = open(DEFAULT_FILENAME1, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);
	  if (threads[i].fd == -1) {
	       perror("test");
	       exit(1);
	  }
	  threads[i].id = i + 1;
	  threads[i].thread_count = thread_count;

	  pid_t pid;
	  pid = fork();
	  if(pid == 0) {
	       Go((void*)&threads[i]);
	       return 0;
	  } else {
	       threads[i].pid = pid;
	  }
     }

     for (i = 0; i < thread_count; i++) {
	  fprintf(stderr, "waiting for %d\n", threads[i].pid);
	  waitpid(threads[i].pid, NULL, 0);
     }
     fprintf(stderr, "Done.\n");
     
     return 0;
     
}
