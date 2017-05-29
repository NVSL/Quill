#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file;
	char* buf = (char*) calloc(10, sizeof(char));

	int i;
	for(i=0; i<7; i++)
	{
		buf[i]='0'+i;
	}

	struct stat st;

	filename = DEFAULT_FILENAME1;

	file = open(filename, O_RDONLY);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");

	read(file, buf, 4);

	int fd2 = open(filename, O_RDWR);

	write(fd2, buf, 2);

	int fd3 = open(filename, O_RDWR);

	close(file);

	write(fd3, buf, 2);

	dup2(fd3, fd2);

	write(fd2, buf, 5);

	file = open(filename, O_RDWR);

	write(file, buf, 5);

	close(fd2);

	close(fd3);

	write(file, buf, 20);

	lseek(file, 5, SEEK_SET);

	read(file, buf, 2);

	close(file);

	MSG("Done.\n");

	return 0;
}

