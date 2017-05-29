
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
//	int i;
	char* filename1;
	char* filename2;
	int file1, file2;
	char* buf = (char*) calloc(200, sizeof(char));
	
	struct stat st;
	
	filename1 = DEFAULT_FILENAME1;
	filename2 = DEFAULT_FILENAME2;

	file1 = open(filename1, O_RDWR);
	CHECK_FD(file1);
	
	file2 = open(filename2, O_RDWR);
	CHECK_FD(file2);

	CHECK_LEN(filename1, "Hello");
	CHECK_LEN(filename2, "Croissant");
	
	CHECK_READ(read(file1, (void*)buf, 5), "Hello");
	CHECK_READ(read(file2, (void*)buf, 9), "Croissant");

	CHECK_SEEK(lseek(file1, 0, SEEK_SET), 0);

	SET_BUFFER("asdf");

	CHECK_WRITE(write(file1, buf, strlen(buf)), buf);
	
	CHECK_SEEK(lseek(file2, 1, SEEK_SET), 1);

	CHECK_WRITE(write(file2, buf, strlen(buf)), buf);

	CLEAR_BUFFER;

	CHECK_CLOSE(close(file2));

	filename2 = DEFAULT_FILENAME4;

	SET_BUFFER("This is some text.");

	file2 = open(filename2, O_RDWR);
	CHECK_FD(file2);

	CHECK_LEN(filename2, "");

	CHECK_WRITE(write(file2, buf, strlen(buf)), buf);

	CHECK_CLOSE(close(file2));

	CHECK_CLOSE(close(file1));

	MSG("Note: this test is not comprehensive.\n");

	return 0;
}

