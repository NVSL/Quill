#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include"FastRand.hpp"

#define _XOPEN_SOURCE 600
#include <stdlib.h>

#define SEED &myseed


ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t  pwrite(int fd, const void *buf, size_t count, off_t offset);

int main(int argc, char* argv[])
{
	time_t start;
	size_t count = 0;
	size_t offset = 0;
	int i;

	size_t myseed = 0xC0FF33;

	if((argc != 5))
	{
		printf("Usage: megadd time footKB req_size rwratio\n");
		exit(EXIT_FAILURE);
	}

	size_t runtime = atoll(argv[1]);

	size_t range = atoll(argv[2]);
	size_t req_size = atoll(argv[3]);
	int rwratio = (int)atof(argv[4]);

	assert(range>0);
	assert(req_size>0);
	assert(rwratio >= 0);
	assert(rwratio <= 100);
	
	printf("time %li\nfootKB %li\nreqsize %li\nrwratio %i\n", runtime, range, req_size, rwratio);

	void* buf;
	posix_memalign(((void**)&buf), 4096, req_size); 

	int fd = open("/tmp/memuram0/xddtestfile.txt", O_RDWR|0x4000);
	assert(fd>0);

	printf("Starting timing\n");
	fflush(stdout);
	sleep(1);

	start = time(NULL);

	while(time(NULL)-start < runtime)
	{
		for(i=0; i<16384; i++)
		{
			offset = (RandLFSR(SEED) % range) & (~((size_t)511));
			
			if((RandLFSR(SEED) % 100) >= rwratio) {
				pwrite(fd, buf, req_size, offset);
			} else {
				pread(fd, buf, req_size, offset);
			}

			count++;
		}
	}

	printf("Done.  Ops: %li   IOPS: %li   BW(MB/s): %li\n", count, count/runtime, (count*req_size)/runtime/(1024*1024));
	return 0;
}
       
