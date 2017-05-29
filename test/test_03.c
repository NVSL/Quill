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
	int fd1, fd2, fd3;
	char* buf = (char*) calloc(100, sizeof(char));

	char* filename = DEFAULT_FILENAME1;
	char* filename2= DEFAULT_FILENAME2;

	fd1 = open(filename, O_RDWR);
	close(fd1);
	fd1 = open(filename, O_RDWR);
	close(fd1);
	fd1 = open(filename, O_RDWR);
	close(fd1);
	fd1 = open(filename, O_RDWR);
	close(fd1);
	fd1 = open(filename, O_RDWR);
	close(fd1);
	fd1 = open(filename, O_RDWR);
	close(fd1);

	fd1 = open(filename, O_RDWR);
	fd2 = open(filename2, O_RDWR);
	close(fd1);
	close(fd2);
	fd1 = open(filename, O_RDWR);
	fd2 = open(filename2, O_RDWR);
	close(fd1);
	close(fd2);
	fd1 = open(filename, O_RDWR);
	close(fd1);
	fd2 = open(filename2, O_RDWR);
	close(fd2);
	
	close(fd1);
	fd1 = open(filename, O_RDWR);
	close(fd2);
	fd2 = open(filename2, O_RDWR);
	close(fd2);
	close(fd1);

	fd1 = dup(fd1);
	fd1 = dup(fd1);
	
	fd1 = open(filename, O_RDWR);
	fd2 = open(filename2, O_RDWR);

	fd3 = dup(fd1);
	fd3 = dup(fd1);
	fd3 = dup(fd1);
	fd3 = dup(fd2);
	fd3 = dup(fd1);
	fd3 = dup(fd2);

	fd3 = dup2(fd1, fd3);
	fd3 = dup2(fd2, fd3);
	fd3 = dup2(fd1, fd2);
	fd3 = dup2(fd2, fd1);

	fd3 = dup2(fd3, fd3);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	close(fd1);
	close(fd2);
	close(fd3);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);


	fd1 = open(filename, O_RDWR);
	fd2 = open(filename, O_RDWR);
	fd3 = open(filename, O_RDWR);

	fd2 = dup2(fd1, fd2);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	fd3 = dup2(fd3, fd1);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	close(fd1);
	close(fd2);
	close(fd3);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	fd1 = open(filename, O_RDWR);
	fd2 = open(filename, O_RDWR);
	fd3 = open(filename, O_RDWR);

	fd3 = dup2(fd1, fd2);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	fd3 = dup2(fd3, fd2);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	close(fd1);
	close(fd2);
	close(fd3);

	read(fd1, buf, 10);
	read(fd2, buf, 10);
	read(fd3, buf, 10);

	return 0;
}

