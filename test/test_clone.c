
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>

#include "test_common.h"

int DoSomething(void*t) {return 0;}

int do_test_eval(int argc, char* argv[])
{
	char* filename;
	int file;
	char* buf = (char*) calloc(200, sizeof(char));



	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	MSG("Testing open(2) on file \"%s\"\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Hello");
	
	CHECK_READ(read(file, (void*)buf, 5), "Hello");

	CHECK_CLOSE(close(file));

	MSG("Testing clone...\n");


	void *child_stack = malloc(4096*1024);
       	int flags = CLONE_PARENT|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_VM|CLONE_THREAD;
	void *arg = NULL;


//  int waitforgdb = 1;
//while(waitforgdb) {};


	int pid = clone(DoSomething, child_stack, flags, arg);
	MSG("Child is %d\n", pid);

	filename = DEFAULT_FILENAME2;
	
	MSG("Testing open(2) on file \"%s\"\n", filename);

	file = open(filename, O_RDWR);
	CHECK_FD(file);

	CHECK_LEN(filename, "Croissant");
	
	CHECK_READ(read(file, (void*)buf, 5), "Croissant");

	CHECK_CLOSE(close(file));

	return 0;

}

