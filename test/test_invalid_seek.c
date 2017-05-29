
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
	
	MSG("Testing some invalid seeks\n");

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");

	CHECK_READ(read(file, buf, 2), "He");

	for(i=-20; i!=25; i+=3)
	{
		lseek(file, 10, i);
	}

	CHECK_LEN(filename, "Hello");

	CHECK_SEEK(lseek(file, -1, SEEK_END), 4);

	CHECK_WRITE(write(file, buf, 5), "01234");

	CHECK_LEN(filename, "Hell01234");

	MSG("Done.\n");

	return 0;
}

