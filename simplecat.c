/*
  Simple program which acts like cat: given a file path, echoes its contents to stdout
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char* argv[])
{
	if(argc != 2) {
		printf("Usage: %s filepath.txt\n", argv[0]);
		exit(1);
	}

	int fp = open(argv[1], O_RDWR);

	if(fp < 0) {
		perror("ERROR: couldn't open file");
		exit(2);
	}

	printf("Contents of file %s:\n", argv[1]);

	char c;
	int i = read(fp, &c, 1);

	while(i) {
		printf("%c", c);
		i = read(fp, &c, 1);
	}

	close(fp);

	printf("\nGoodbye.\n");

	return 0;
}
