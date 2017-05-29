
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "test_common.h"


int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file1, file2;
	char* buf = (char*) calloc(200, sizeof(char));
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	file1 = open(filename, O_RDWR);
	CHECK_FD(file1);

	file2 = open(filename, O_RDWR);
	CHECK_FD(file2);

	MSG("Two FDs (%i and %i) point to the same file (%s)\n", file1, file2, filename);
	
	if(file1==file2) {
		ERROR("Two calls to open on the same file returned the same file descriptor int.\n");
	}

	CHECK_LEN(filename, "Hello");
	
	MSG("Reading from both FDs\n");

	CHECK_READ(read(file1, (void*)buf, 5), "Hello");
	CHECK_READ(read(file2, (void*)buf, 5), "Hello");

	MSG("Seeking each FD to a different point to read\n");
	
	CHECK_SEEK(lseek(file1, 0, SEEK_SET), 0);
	CHECK_SEEK(lseek(file2, 4, SEEK_SET), 4);

	CHECK_READ(read(file1, buf, 2), "He");
	CHECK_READ(read(file2, buf, 2), "o");

	SET_BUFFER("1234567890");

	MSG("Writing on second FD\n");
	
	CHECK_WRITE(write(file2, buf, 10), "1234567890");

	CHECK_LEN(filename, "Hello1234567890");

	CLEAR_BUFFER;

	CHECK_SEEK(lseek(file1, 0, SEEK_SET), 0);

	MSG("Reading on the FD that didn't write\n");

	CHECK_READ(read(file1, buf, 10000), "Hello1234567890");

	MSG("Testing with dup2\n");
	
	file1 = dup2(file2, file1);
	CHECK_FD(file1);

	CHECK_SEEK(lseek(file1, 0, SEEK_SET), 0);

	CHECK_WRITE(write(file1, "aaa", 3), "aaa");
	CHECK_WRITE(write(file2, "bbb", 3), "bbb");

	MSG("Testing third FD with dup\n");

	int file3 = dup(file1);
	CHECK_FD(file3);

	MSG("Now we have 3 FDs: %i %i %i\n", file1, file2, file3);

	CHECK_SEEK(lseek(file3, 0, SEEK_SET), 0);

	CLEAR_BUFFER;

	MSG("Verifying file contents\n");

	CHECK_READ(read(file1, buf, 40), "aaabbb234567890");

	MSG("Closing all FDs\n");

	CHECK_CLOSE(close(file1));
	CHECK_CLOSE(close(file2));
	CHECK_CLOSE(close(file3));

	MSG("Note: this test is not comprehensive.\n");

	return 0;
}

