#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file;
	char* buf = (char*) calloc(10, sizeof(char));

	int i;
	for(i=0; i<7; i++)
	{
		buf[i]='0'+i;
	}

	struct stat st;

	filename = DEFAULT_FILENAME1;

	MSG("Opening file %s for hole tests\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");

	int result = read(file, buf, 5);
	if(result!=5) {
		ERROR("Didn't read the expected file contents (got %i, expected %i)\n", result, 5);
	}

	MSG("Writing at a far location.\n");

	CHECK_SEEK(lseek(file, 100000000, SEEK_END), 100000005);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	MSG("Writing to some intermediate points.\n");

	CHECK_SEEK(lseek(file, 100, SEEK_SET), 100);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 100000, SEEK_SET), 100000);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 10000, SEEK_SET), 10000);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 100000000, SEEK_SET), 100000000);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 50, SEEK_SET), 50);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 50000, SEEK_SET), 50000);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 815247, SEEK_SET), 815247);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 999999, SEEK_SET), 999999);
	CHECK_WRITE(write(file, buf, 4), "asdf");

	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	CHECK_SEEK(lseek(file, 99999999, SEEK_SET), 99999999);
	CHECK_WRITE(write(file, buf, 4), "asdf");
	
	if(stat(filename, &st)){
		ERROR("failed to get file stats for %s\n", filename);
	} else if(st.st_size != 100000009) {
		ERROR("expected file length %i, got %i\n", 100000009, (int)st.st_size);
	}

	MSG("Done.\n");

	return 0;
}

