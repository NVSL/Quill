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
	int fd;
	int result;
//	struct stat st;
	char* buf = (char*) calloc(10000000, sizeof(char));

	char* filename = DEFAULT_FILENAME1;

	fd = open(filename, 12178
//		&(~0x200)	// TRUNC
//		&(~0x400)	// APPEND
//		&(~0x80)	// EXCL
//		&(~0x2000)	// ASYNC
//		&(~0x100)	// NOCTTY
//		&(~0x800)	// NONBLOCK, NDELAY
	);

	if(fd < 0) {
		ERROR("Failed to open file %s: %s\n", filename, strerror(errno));
	}
	
//	CHECK_LEN_INT(filename, 0);
	
//	result = pwrite(fd, buf, 950272, 67108864);

	result = write(fd, buf, 4095);

	result = write(fd, buf, 1);
	result = write(fd, buf, 1);
	result = write(fd, buf, 1);

	result = write(fd, buf, 4099);

//	CHECK_LEN_INT(filename, 0);
	
	CHECK_CLOSE(close(fd));

	return 0;
}

