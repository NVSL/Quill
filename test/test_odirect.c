#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "test_common.h"

#define _XOPEN_SOURCE 600

int do_test_eval(int argc, char* argv[])
{
	int fd;
	int result;
	struct stat st;
	void* buf; // = (char*) calloc(1<<18, sizeof(char));

	char* filename = DEFAULT_FILENAME1;

	assert(!posix_memalign(((void**)&buf), 512, 1<<18));
	
	fd = open(filename, O_RDWR | 0x4000);

	if(fd < 0) {
		ERROR("Failed to open file %s: %s\n", filename, strerror(errno));
	}

	CHECK_LEN_INT(filename, 5);

	result = ftruncate(fd, 1<<20);

	if(result) {
		ERROR("Failed to truncate to 4096 (result %i)\n", result);
	}

	result = read(fd, buf, 1);

	if(result != -1) {
		ERROR("Successfully performed unaligned read on O_DIRECT fd %i (result %i)\n", fd, result);
	}

	result = write(fd, buf, 1);

	if(result != -1) {
		ERROR("Successfully performed unaligned write on O_DIRECT fd %i (result %i)\n", fd, result);
	}

	if(result != 0) {
	//	ERROR("Failed aligned seek to 0 (result %i: %s)\n", result, strerror(errno));
	}

	result = read(fd, buf, 4096);
		
	if(result != 4096) {
		ERROR("Failed to perform 4096-aligned read on O_DIRECT fd %i (result %i)\n", fd, result);
	}

	result = write(fd, buf, 4096);
		
	if(result != 4096) {
		ERROR("Failed to perform 4096-aligned write on O_DIRECT fd %i (result %i)\n", fd, result);
	}

	result = lseek(fd, 0, SEEK_SET);

	result = read(fd, buf, 512);
		
	if(result != 512) {
		ERROR("Failed to perform 512-aligned read on O_DIRECT fd %i (result %i)\n", fd, result);
	}

	result = write(fd, buf, 512);
		
	if(result != 512) {
		ERROR("Failed to perform 512-aligned write on O_DIRECT fd %i (result %i)\n", fd, result);
	}

	lseek(fd, 5, SEEK_SET);

	close(fd);

	MSG("Done.\n");

	return 0;
}
