#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "test_common.h"

int do_test_eval(int argc, char* argv[])
{
	int fd;
	int result;
	struct stat st;
	char* buf = (char*) calloc(100, sizeof(char));

	char* filename = DEFAULT_FILENAME3;

	fd = open(filename, 578, -1);

	if(fd < 0)
	{
		ERROR("Failed to open file %s: %s\n", filename, strerror(errno));
	}
	
	CHECK_LEN_INT(filename, 0);

//	result = lseek(fd, 623495, -1);

//	if(result != -1)
//	{
//		ERROR("Got bad seek result (Got %i, expected -1)\n", result);
//	}

//	result = read(fd, buf, 555520);

//	if(result != 0)
//	{
//		ERROR("read expected 0, got %i\n", result);
//	}

	result = close(fd);

	if(result)
	{
		ERROR("Couldn't close file (got %i, expected 0)\n", result);
	}

	//open(filename, 1073842 );
	open(filename, 1073842 & (~O_EXCL) & (~O_TRUNC) & (~O_ASYNC) );
	//open(filename, O_RDWR );

	result = ftruncate(fd, 46);

	if(result != 0) 
	{
		ERROR("Failed to seek past end of file (got %i, expected 0): %s\n", result, strerror(errno));
	}

	CHECK_LEN_INT(filename, 46);

	result = read(fd, buf, 106432);

//	if(result != 46)	alignment is required, so this won't hold
//	{
//		ERROR("Read did not read 46 chars! (read %i)\n", result);
//	}

	CHECK_LEN_INT(filename, 46);
	
	CHECK_CLOSE(close(fd));

	return 0;
}

