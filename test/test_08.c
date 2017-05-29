#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "test_common.h"

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

int do_test_eval(int argc, char* argv[])
{
	int fd;
	void* buf;
	assert(!posix_memalign(((void**)&buf), 512, 67108864));

	char* filename = DEFAULT_FILENAME3;

	fd = open(filename, O_RDWR|O_CREAT|0x4000); // O_DIRECT

	pwrite(fd, buf, 67108864, 0);



	return 0;
}

