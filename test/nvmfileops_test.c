// Test routines for nvmfileops.c

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
	int i;
	char* filename;
	int file;
	char* buf = (char*) calloc(200, sizeof(char));
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	MSG("Testing open() on file \"%s\"\n", filename);

	file = open(filename, O_RDWR, 0644);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_LEN(filename, "Hello");

	SET_BUFFER(" world.");

	MSG("Appending \"%s\"\n", buf);
	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	CHECK_LEN(filename, "Hello world.");

	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);
	CHECK_READ(read(file, buf, strlen("Hello world.")), "Hello world.");
	
	SET_BUFFER("Short");

	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);
	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	CHECK_LEN(filename, "Short world.");

	CLEAR_BUFFER;

	// there is no seek here; should NOT read whole file
	CHECK_READ(read(file, buf, 12), " world.");

	CHECK_TRUNC(ftruncate(file, 5), 5);

	CLEAR_BUFFER;

	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);
	CHECK_READ(read(file, buf, 20), "Hello");
	
	CHECK_LEN(filename, "Short");
	
	MSG("Done with file.  Closing.\n");

	CHECK_CLOSE(close(file)); 
	
	CHECK_LEN(filename, "Short");


	MSG("Done with first file.\n");

	MSG("Generating long meaningless string for tests.\n");
	char longbuf [50001];
	char longbuf2[50001];

	srand(time(NULL));
	for(i=0; i<50000; i++) {
		//longbuf[i] = (rand()%93)+32;
		longbuf[i]='x';
	}
	longbuf [50000] = '\0';
	memset(longbuf2, '\0', 50001);
	for(i=0; i<10; i++) {
		MSG("longbuf[%i]='%c'\n", i*10, longbuf[i*10]);
	}


	filename = DEFAULT_FILENAME2;

	MSG("Testing with file %s\n", filename);

	CHECK_LEN(filename, "Croissant");

	file = open(filename, O_RDWR | O_TRUNC);
	CHECK_FD(file);

	CHECK_LEN(filename, "");
	
	MSG("Attempting read from empty file\n");

	CHECK_READ(read(file, (void*)buf, 20), "");

	MSG("Appending a long string\n");
	for(i=0; i<10; i++) {
		MSG("longbuf[%i]='%c'\n", i*10, longbuf[i*10]);
	}
	CHECK_WRITE(write(file, longbuf, strlen(longbuf)), longbuf);

	CHECK_LEN(filename, longbuf);
	for(i=0; i<10; i++) {
		MSG("longbuf[%i]='%c'\n", i*10, longbuf[i*10]);
	}

	MSG("Reading new file contents\n");

	memcpy(longbuf2, longbuf, 50000);
	memset(longbuf, '\0',     50000);

	for(i=0; i<10; i++) {
		MSG("longbuf[%i]='%c'\n", i*10, longbuf[i*10]);
	}
	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);
	CHECK_READ(read(file, longbuf, 50000), longbuf2);

	for(i=0; i<10; i++) {
		MSG("longbuf[%i]='%c'\n", i*10, longbuf[i*10]);
	}
	MSG("Done with file.  Closing.\n");
	CHECK_CLOSE(close(file));
	
	CHECK_LEN(filename, longbuf);
	MSG("longbuf=\"%s\"\n", longbuf);
	for(i=0; i<10; i++) {
		MSG("longbuf[%i]='%c'\n", i*10, longbuf[i*10]);
	}
	
	filename = DEFAULT_FILENAME3;

	CLEAR_BUFFER;

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

	SET_BUFFER("Lorem ipsum dolor sit amet, consectetur adipiscing elit.");

	MSG("Testing seek and write beyond file length.\n");

	CHECK_LEN(filename, "");

	CHECK_SEEK(lseek(file, 10, SEEK_SET), 10);

	CHECK_LEN(filename, ""); // shouldn't be extended yet

	CHECK_WRITE(write(file, buf, strlen(buf)), buf);

	char* temp = "\0\0\0\0\0\0\0\0\0\0Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
	char* temp2= "1234567890Lorem ipsum dolor sit amet, consectetur adipiscing elit.";

	CHECK_LEN(filename, temp2);

	CHECK_SEEK(lseek(file, 0, SEEK_SET), 0);

	CHECK_BIN_READ(read(file, buf, 130), temp, strlen(temp2));

	CHECK_LEN(filename, temp2);

	CHECK_CLOSE(close(file));

	CHECK_LEN(filename, temp2);

	return 0;
}

