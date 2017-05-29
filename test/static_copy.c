//Copy file using mmap()

// http://www.c.happycodings.com/Gnu-Linux/code6.html

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#define PACKAGE "mmap"

int main(int argc, char *argv[]) {
 int input, output;
 size_t filesize;
 void *source, *target;

 if(argc != 3)
  fprintf(stderr, "%s SOURCE DEST\n"), exit(1);

 if((input = open(argv[1], O_RDONLY)) == -1)
  fprintf(stderr, "%s: Error: opening file: %s\n", PACKAGE, argv[1]), exit(1);

 if((output = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, 0666)) == -1)
  fprintf(stderr, "%s: Error: opening file: %s\n", PACKAGE, argv[2]), exit(1);

 filesize = lseek(input, 0, SEEK_END);
 lseek(output, filesize - 1, SEEK_SET);
 write(output, '\0', 1);

 if((source = mmap(0, filesize, PROT_READ, MAP_SHARED, input, 0)) == (void *) -1)
  fprintf(stderr, "Error mapping input file: %s\n", argv[1]), exit(1);

 if((target = mmap(0, filesize, PROT_WRITE, MAP_SHARED, output, 0)) == (void *) -1)
  fprintf(stderr, "Error mapping ouput file: %s\n", argv[2]), exit(1);

 memcpy(target, source, filesize);

 munmap(source, filesize);
 munmap(target, filesize);

 close(input);
 close(output);

 return 0;
}

