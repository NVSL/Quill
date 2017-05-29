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
	char* filename = DEFAULT_FILENAME1;

	int fd = open(filename, O_RDWR);

	assert(fd>=0);
	
	int result;

	result = dup2(fd, 2);
/*
	int i;
	for(i=0; i<3; i++)
	{
		result = dup2(fd, i);

		if(result != i) {
			return -1;
		}
	}
*/
	printf("Success\n");
	
	return 0;
}

