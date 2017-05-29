#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{
	printf ("Hello World!\n");
	
	int file = open("helloworld.txt", O_RDWR|O_CREAT, 0666);

	if(file<0) {
		printf("ERROR: failed to open file %s!\n", "helloworld.txt");
		return -1;
	}

	char* buf = (char*) calloc(100, sizeof(char));

	memcpy(buf, "Hello world!\n", strlen("Hello world!\n"));

	int result = write(file, buf, strlen(buf));

	if(result != strlen(buf)) {
		printf("ERROR: Failed to write to file!\n");
		return -1;
	}

	result = close(file);

	if(result) {
		printf("ERROR: failed to close file!\n");
		return -1;
	}

	printf("Program complete.  Goodbye!\n");

	printf("./helloworld.testexe: RESULT: SUCCESS\n");

	return 0;
}

