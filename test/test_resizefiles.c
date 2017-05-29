
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
	struct stat st;
	char* buf = (char*) calloc(10000, sizeof(char));

	filename = DEFAULT_FILENAME1;

	MSG("Opening test file %s\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");

	SET_BUFFER("Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged.");

	MSG("Checking extension by writing starting at the beginning.\n");

	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	CHECK_LEN(filename, buf);

	MSG("Closing file.\n");

	CHECK_CLOSE(close(file));

	CHECK_LEN(filename, buf);



	MSG("Opening the same file O_RDWR|O_TRUNC\n");

	file = open(filename, O_RDWR|O_TRUNC);
	CHECK_FD(file);

	CHECK_LEN(filename, "");

	CLEAR_BUFFER;

	MSG("Verifying that the file starts out empty.\n");

	CHECK_READ(read(file, buf, 10), "");

	MSG("Writing a few things to the file, testing to make sure the file is the correct length afterwards.\n");

	SET_BUFFER("fhqwhgads");

	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	CHECK_LEN(filename, buf);

	SET_BUFFER("\"Waiter! This coffee tastes like mud.\" \"Yes sir, it's fresh ground.\"");

	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	SET_BUFFER("fhqwhgads\"Waiter! This coffee tastes like mud.\" \"Yes sir, it's fresh ground.\"");

	CHECK_LEN(filename, buf);

	MSG("Closing file.\n");

	CHECK_CLOSE(close(file));

	CHECK_LEN(filename, buf);



	file = open(filename, O_RDWR);

	CHECK_LEN(filename, buf);

	MSG("Checking to make sure file length is not modified with seek alone\n");

	CHECK_SEEK(lseek(file, 1, SEEK_SET), 1);

	CHECK_LEN(filename, buf);

	CHECK_SEEK(lseek(file, 500, SEEK_SET), 500);

	CHECK_LEN(filename, buf);

	MSG("Opening another copy of the same file to check contents\n");
	
	int file2 = open(filename, O_RDWR);
	CHECK_FD(file2);

	CHECK_SEEK(lseek(file, -100, SEEK_CUR), 400);

	CHECK_LEN(filename, buf);
	
	char* buf2 = (char*) calloc(strlen(buf)+1, sizeof(char));
	strncpy(buf2, buf, strlen(buf));

	CLEAR_BUFFER;

	CHECK_READ(read(file2, buf, 10000), buf2);

	CHECK_LEN(filename, buf2);

	MSG("Closing second file.\n");

	CHECK_CLOSE(close(file2));

	CHECK_LEN(filename, buf2);

	MSG("Writing a byte at pos 400.\n");

	CHECK_WRITE(write(file, "1", 1), "1");

	free(buf); buf = (char*) calloc(10000, sizeof(char));

	memset(buf, 'x', 401);

	CHECK_LEN(filename, buf);

	MSG("Calling ftruncate with a value longer than the current EOF\n");

	CHECK_TRUNC(ftruncate(file, 1000), 0);
	
	if(stat(filename, &st)) {
		ERROR("failed to get file stats for %s\n", filename);
	} else if( st.st_size == 400) {
		MSG("File size unchanged (excellent)\n");
	} else if( st.st_size == 1000) {
		MSG("File size increased to 1000 (acceptable)\n");
	} else if( st.st_size < 0 ) {
		ERROR("ftruncate failed.\n");
	} else if( st.st_size > 1000 ) {
		ERROR("ftruncate to 1000 resulted in a file length > 1000\n");
	}

	MSG("Calling ftruncate with a value shorter than the current EOF\n");

	CHECK_TRUNC(ftruncate(file, 100), 0);

	buf[100] = '\0';

	CHECK_LEN(filename, buf);

	MSG("Writing again (at 401)\n");

	CHECK_WRITE(write(file, "4", 1), "1");

	memset(buf, 'x', 402);

	CHECK_LEN(filename, buf);

	MSG("Closing file.\n");

	CHECK_CLOSE(close(file));

	//MSG("Note: this test is not comprehensive.\n");

	return 0;
}

