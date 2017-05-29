#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
//#include"FastRand.hpp"

#define _XOPEN_SOURCE 600
#include <stdlib.h>

ssize_t pread(int fd, void *buf, size_t count, off_t offset);
size_t pwrite(int fd, const void *buf, size_t count, off_t offset);

#define TIME 30
#define BLOCKSIZE (1)

#define MAX_FILE_SIZE (((long long int)50)*1024*1024*1024)

int main()
{
	void* buf;
	posix_memalign(((void**)&buf), 4096, 4096);
	//int fd = open("/tmp/memuram0/xddtestfile.txt", O_RDWR|0x4000);
	unlink("/tmp/memuram0/extendfile.txt");
	int fd = open("/tmp/memuram0/extendfile.txt", O_CREAT|O_RDWR, 0666);
	assert(fd>0);

	time_t start;

	size_t count = 0;
	
	int i;

	printf("Starting test append of %i bytes for %i seconds...\n", BLOCKSIZE, TIME);

	start = time(NULL);
	long long int offset = 0;

	while(time(NULL)-start < TIME)
	{
		for(i=0; i<1024; i++) {
			pwrite(fd, buf, BLOCKSIZE, offset);
			count++;
			offset += BLOCKSIZE;
			if(offset > MAX_FILE_SIZE) {
				assert(0);
			}
		}
	}

	printf("Done.  Ops: %li   IOPS: %li  (%f us/op)\n", count, count/TIME, (((float)TIME)*1000000)/count);
	return 0;
}
       
