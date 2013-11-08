
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"


int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file;
	char* buf = (char*) calloc(200000, sizeof(char));

	int i;
	for(i=0; i<190000; i++)
	{
		buf[i] = '0'+(i%10);
	}
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	CHECK_LEN(filename, "Hello");

	file = open(filename, O_RDWR);

	CHECK_LEN(filename, "Hello");

	CHECK_SEEK(lseek(file, 10000000, SEEK_SET), 10000000);

	CHECK_WRITE(write(file, buf, 4), "0123");

	MSG("Checking to see if we can trunc the file with holes\n");

	CHECK_TRUNC(ftruncate(file, 100000), 0);

	buf[100000]='\0';
	
	CHECK_LEN(filename, buf);

	MSG("Checking SEEK_END\n");

	CHECK_SEEK(lseek(file, 10000, SEEK_END), 110000);

	CHECK_LEN(filename, buf);

	CHECK_READ(read(file, buf, 2), "");

	CHECK_WRITE(write(file, buf, 2), "01");

	buf[100000]='x';
	buf[110002]='\0';

	CHECK_LEN(filename, buf);

	MSG("Checking ftruncate extension\n");

	CHECK_TRUNC(ftruncate(file, 170000), 0);

	buf[110002]='x';
	buf[170000]='\0';

	CHECK_LEN(filename, buf);

	CHECK_CLOSE(close(file));

	MSG("Done.\n");

	return 0;
}


