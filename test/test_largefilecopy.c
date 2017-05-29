
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
	int file;
	int result;
	char* buf = (char*) calloc(200, sizeof(char));

	char* rdbuf = (char*) calloc(20000000, sizeof(char));
	char* wrbuf = (char*) calloc(20000000, sizeof(char));
	
	int i;
        for(i=0; i<20000000; i++) {
		wrbuf[i] = (char)i;
	}

//volatile int asdf = 1; while(asdf){};	
	filename = DEFAULT_FILENAME1;

	MSG("Writing large random file to %s\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	result = write(file, wrbuf, 20000000);
	if(result!=20000000) {
		ERROR("Didn't write 20000000 chars! (wrote %i)\n", result);
	}

	filename = DEFAULT_FILENAME2;

	int file2 = open(filename, O_RDWR, 0000);
	CHECK_FD(file2);

	CHECK_READ(read(file, rdbuf, 20000000), wrbuf);

	CHECK_CLOSE(close(file));

	result = write(file2, rdbuf, 20000000);
	
	if(result!=20000000) {
		ERROR("Didn't write 20000000 chars!\n");
	}

	CHECK_CLOSE(close(file2));

	MSG("Note: this test is not comprehensive.\n");

	return 0;
}


