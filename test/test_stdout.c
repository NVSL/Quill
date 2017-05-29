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
//	int fd_stdin = 0;
	int fd_stdout = 1;
	int fd_stderr = 2;

	int fd1, fd2, fd3;

	char* filename = DEFAULT_FILENAME1;

//int asdf=1; while(asdf){};

	printf("dup(stdout)\n");
	fd1 = dup(fd_stdout);
	printf("close(stdout)\n");

	close(fd_stdout);

	printf("open\n");
	fd3 = open(filename, O_RDWR);

	printf("dup2(stderr)\n");
	fd2 = dup2(fd3, fd_stderr);

	printf("close everything (%i, %i, and %i)\n", fd1, fd2, fd3);
	close(fd1);
	close(fd2);
	close(fd3);

	printf("Success.  Goodbye!\n");

	return 0;
}

