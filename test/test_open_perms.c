
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"


int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file;
	char* buf = (char*) calloc(200, sizeof(char));

	int i;
	for(i=0; i<30; i++)
	{
		buf[i] = '0'+(i%10);
	}
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;
	
	MSG("Testing open of the same file with different perms.\n");

	file = open(filename, O_RDONLY);
	CHECK_FD(file);

	CHECK_READ(read(file, buf, 2), "He");

	if(write(file, buf, 5) >=0)
	{
		ERROR("Successfully wrote to O_RDONLY file from O_RDONLY fd.\n");
	}

	int fd2 = open(filename, O_RDWR);
	CHECK_FD(fd2);

	CHECK_READ(read(fd2, buf, 2), "He");

	CHECK_WRITE(write(fd2, buf, 3), "012");

	if(write(file, buf, 5) >=0)
	{
		ERROR("Successfully wrote to O_RDONLY file from O_RDONLY fd.\n");
	}

	CHECK_LEN(filename, "He012");

	MSG("Done.\n");

	return 0;
}

