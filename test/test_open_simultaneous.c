
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

	MSG("Testing open(2) on file \"%s\"\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_CLOSE(close(file));

	int f2;
	int f3;

	filename = DEFAULT_FILENAME3; // does not exist at start

	MSG("Opening the same nonexistant file with O_CREAT twice\n");

	f2 = open(filename, O_RDWR|O_CREAT, 0644);
	
	CHECK_LEN(filename, "");

	if(write(f2, buf, 5) != 5) {
		ERROR("Didn't write the correct length\n");
	}

	CHECK_LEN(filename, "01234");

	f3 = open(filename, O_RDWR|O_CREAT, 0644);

	CHECK_LEN(filename, "01234");

	if(write(f3, buf, 1) != 1) {
		ERROR("Didn't write the correct length\n");
	}

	CHECK_LEN(filename, "01234");

	if(write(f2, buf, 1) != 1) {
		ERROR("Didn't write the correct length\n");
	}

	CHECK_LEN(filename, "012340");

	MSG("Done.\n");

	return 0;
}


