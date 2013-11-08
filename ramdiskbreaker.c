#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#ifndef O_DIRECT
#define O_DIRECT        00040000
#endif

#define filename "/tmp/memuram0/xddtestfile.txt"

#define THREAD_COUNT 20

void* map_location;

// this lock will be used to sync threads.
// parent thread grabs a wrlock before spawning threads.
// child threads wait for rdlocks to start.
// parent releases wrlock, allowing all children to start.
// children release rdlocks when finished.
// parent waits for wrlock to call the program finished.
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;;


void* thread_work(void* ptr)
{
	printf("Thread %llu waiting...\n", (long long unsigned int)ptr);
	
	char wrbuf[4096];
	
	pthread_rwlock_rdlock(&rwlock);

	memcpy(map_location+0x4000, wrbuf, 4096);

	pthread_rwlock_unlock(&rwlock);

	return ptr;
}

int main()
{
	printf("Attempting to cause a kernel panic.\n");
	printf("Thred count: %i\n", THREAD_COUNT);
	printf("File: %s\n", filename);

	struct stat file_st;
	
	if(access(filename, F_OK)) { 
		printf("File doesn't exist.\n");
		return -1;
	}
	if(stat(filename, &file_st)) { 
		perror("Couldn't stat the file");
		return -1;
	}
	if(!S_ISREG(file_st.st_mode)) { 
		printf("Not a regular file.\n");
		return -1;
	}
	
	size_t flen = file_st.st_size;

	if(flen<(32*1024*1024)) {
		printf("File too small; try something larger.\n");
		return 1;
	}
	
	int fd = open(filename, O_RDWR|O_DIRECT);
	if(fd<0) {
		perror("Coudln't open file");
		return 2;
	}
	
	size_t page_size = getpagesize();
	assert( page_size    >= 512);
	assert((page_size%2) == 0);
	size_t maplen = (flen/page_size + 1)*page_size;

	map_location = (char*) mmap(
		NULL, 			// starting address
		maplen,			// map length
		PROT_READ|PROT_WRITE,	// protection flags
		MAP_SHARED,		// flags
		fd,			// file descriptor
		0			// offset
	);

	if(map_location == ((void*)-1)) {
		perror("Failed to mmap");
		return 4;
	}

	pthread_rwlock_wrlock(&rwlock);
	
	// spawn threads
	size_t i;
	pthread_t threads[THREAD_COUNT];

	for(i=0; i<THREAD_COUNT; i++) {
		if(pthread_create(threads+i, NULL, thread_work, (void*)i)) {
			printf("Failed to create thread %i", i);
			perror("");
			return 3;
		}
	}
	sleep(1);

	// sync threads

	printf("Starting threads.\n");
	pthread_rwlock_unlock(&rwlock);
	
	sleep(1);

	pthread_rwlock_wrlock(&rwlock);

	printf("All threads done.  Goodbye.\n");

	pthread_rwlock_unlock(&rwlock);

	return 0;
}

