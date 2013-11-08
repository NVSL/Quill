#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "test_common.h"

int do_test_eval(int argc, char* argv[])
{
	int fd;
	void* buf;
	assert(!posix_memalign(((void**)&buf), 512, 1<<21));

	char* filename = DEFAULT_FILENAME1;

	fd = open(filename, O_RDWR|0x4000); // O_DIRECT

	fork();
	fork();
	fork();

	int i;
	for(i=9; i<22; i++)
	{
		write(fd, buf, 1<<i);
	}
	
	for(i=9; i<22; i++)
	{
		lseek(fd, 1<<(i-1), SEEK_SET);
		write(fd, buf, 1<<i);
	}


	return 0;
}

