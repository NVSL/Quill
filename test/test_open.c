
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
	char* filename;
	int file;
	int result;
	char* buf = (char*) calloc(200, sizeof(char));
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	MSG("Testing open(2) on file \"%s\"\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_CLOSE(close(file));


	MSG("Testing open(3) with mode = 0000\n");

	file = open(filename, O_RDWR, 0000);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_CLOSE(close(file));


	MSG("Testing open(3) with mode = 0777\n");

	file = open(filename, O_RDWR, 0000);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_CLOSE(close(file));


	MSG("Testing open(3) with mode = 0777\n");

	file = open(filename, O_RDWR, 0000);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_CLOSE(close(file));



	filename = DEFAULT_FILENAME3;
	
	MSG("Testing open with nonexistant file %s\n", filename);

	file = open(filename, O_RDWR);

	if(file != -1) {
		ERROR("Successfully opened nonexistant file %s\n", filename);
	}

	MSG("Testing O_CREAT with nonexistant file %s\n", filename);

	file = open(filename, O_RDWR|O_CREAT, 0644);
	CHECK_FD(file);

	CHECK_LEN(filename, "");

	CHECK_READ(read(file, buf, 20), "");

	CHECK_CLOSE(close(file));

	if(stat(filename, &st)) {
		ERROR("Failed to get file stats for %s\n", filename);
	} else {
		if(st.st_size != 0) {
			ERROR("st.st_size != 0\n");
		}
		if((st.st_mode&0777) != 0644) {
			ERROR("st.st_mode != 0644 (st.st_mode = %o)\n", st.st_mode&0777);
		}
	}


	MSG("Testing open(O_RDONLY)\n");

	filename = DEFAULT_FILENAME2;

	file = open(filename, O_RDONLY);
	CHECK_FD(file);

	CHECK_LEN(filename, "Croissant");

	CHECK_READ(read(file, buf, 3), "Cro");

	result = write(file, buf, 2);

	if(result > -1) {
		ERROR("Was able to write to a O_RDONLY file!\n");
	}

	CHECK_READ(read(file, buf, 3), "iss");

	CHECK_CLOSE(close(file));

	filename = DEFAULT_FILENAME6;

	file = open(filename, O_RDONLY);

	if(file>=0) {
		ERROR("Got read perms on a file without read perms!\n");
	}

	file = open(filename, O_RDWR);

	if(file>=0) {
		ERROR("Got readwrite perms on a file without read perms!\n");
	}

	filename = DEFAULT_FILENAME5;

	file = open(filename, O_RDWR);

	if(file>=0) {
		ERROR("Got readwrite perms on a file without write perms!\n");
	}

	file = open(filename, O_RDONLY);

	if(file<0) {
		ERROR("Didn't get read-only perms on a file with read perms!\n");
	}

	CHECK_CLOSE(close(file));


	MSG("Note: this test is not comprehensive.\n");

	return 0;
}

