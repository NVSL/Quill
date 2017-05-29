#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "test_common.h"

#define _XOPEN_SOURCE 500

ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);


int do_test_eval(int argc, char* argv[])
{
	int fd,fd2;

	char* filename = (char*) calloc(50, sizeof(char));
	strcpy(filename,"/tmp/test.XXXXXX");


	fd = mkstemp(filename);
	if (fd < 0)
		ERROR("Failed to create temp file\n");
	
	fd2 = dup(fd);	
		ERROR("Failed to dup file\n");


	CHECK_CLOSE(close(fd));
	CHECK_CLOSE(close(fd2));

	return 0;
}

