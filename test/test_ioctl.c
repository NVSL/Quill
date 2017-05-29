// Test routines for ioctl

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stropts.h>

#include "test_common.h"


int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file;
	int result;
	char* buf = (char*) calloc(200, sizeof(char));
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	MSG("Opening file \"%s\"\n", filename);

	file = open(filename, O_RDWR, 0644);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_LEN(filename, "Hello");

	SET_BUFFER(" world.");

	MSG("Appending \"%s\"\n", buf);
	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	CHECK_LEN(filename, "Hello world.");

	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);
	CHECK_READ(read(file, buf, strlen("Hello world.")), "Hello world.");
	
	CLEAR_BUFFER;
	
	MSG("Testing I_PEEK\n");

	result = ioctl(file, I_PEEK);

	if(result != 0) {
		ERROR("expected 0 stream messages, got %i: %s\n", result, strerror(errno));
	}

	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);

	result = ioctl(file, I_PEEK);

	if(result != 1) {
		ERROR("expected 1 stream message, got %i: %s\n", result, strerror(errno));
	}
		
	MSG("Testing I_NRD\n");

	int *x = 0;

	if(ioctl(file, I_NREAD, x)) {
		ERROR("Failed to ioctl on file %i: %s\n", file, strerror(errno));
	}
	if(x && (*x != strlen("Hello world.")+1)) {
		ERROR("Got an unexpected value for I_NREAD on file %i: expected %i, got %i: %s\n",
			file, (int)strlen("Hello world.")+1, *x, strerror(errno));
	}

	return 0;
}

