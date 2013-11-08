#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

//void print_stats(FILE* stat);

ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

int main(int argc, char* argv[])
{
	size_t i;
	char* filename;

	int fd;
	void* wrbuf;
	void* rdbuf;
	
	size_t count;
	off_t offset;

	size_t BUF_SIZE       = (((size_t)1)<<26);
	//size_t MAX_FILE_SIZE  = ((size_t)61)*1024*1024*1024;
	size_t MAX_FILE_SIZE  = ((size_t)90)*1024*1024*1024;

	srand(time(NULL));

	if(argc != 2) {
		printf("Usage: ./iogen filename\n");
		return -1;
	} else {
		filename = argv[1];
		printf("Running iogen on %s\n", filename);
	}
	
	printf("My PID is %i\n", getpid());

	assert(RAND_MAX > MAX_FILE_SIZE / 512 ); // if not random offsets won't hit the whole file
	
	char* statwrbuf = (char*) calloc(100, sizeof(char));

	sprintf(statwrbuf, "/proc/%i/stat", getpid());

/*
	FILE* stat = fopen(statwrbuf, "r");
	
	if(fork())
	{
		while(1) {
			print_stats(stat);
			sleep(3);
		}
	}
*/
	printf("Creating two 512-aligned buffers of size %li (one for read, one for write)\n", BUF_SIZE);

	assert(!posix_memalign((&wrbuf), 512, BUF_SIZE));
	assert(!posix_memalign((&rdbuf), 512, BUF_SIZE));

	printf("Filling write buffer (size %li) with random characters...\n", BUF_SIZE);

	// fill wrbuf with random ASCII characters which are easy to display
	for(i=0; i<BUF_SIZE; i++) {
		((char*)wrbuf)[i] = (rand()%94)+32;
	}

	fd = open(filename, O_RDWR|O_CREAT|0x4000, 0666); // O_DIRECT

	if(fd < 0)
	{
		printf("ERROR: failed to open file %s: %s\n", filename, strerror(errno));
		return -1;
	}

	printf("Creating file of size %li bytes (%li GB) if it doesn't exist\n", MAX_FILE_SIZE, MAX_FILE_SIZE >> 30);

	 pwrite(fd, wrbuf, BUF_SIZE, MAX_FILE_SIZE - BUF_SIZE);

	printf("Filling holes by sequentially writing file in %li byte increments\n", BUF_SIZE);

	for(i=0; i<MAX_FILE_SIZE/BUF_SIZE; i++)
	{
		if(pwrite(fd, wrbuf, BUF_SIZE, BUF_SIZE*i) != BUF_SIZE){
			printf("Failed to write %li bytes at position %li\n", BUF_SIZE, BUF_SIZE*i);
			assert(0);
		}
		
	//	printf("%li%% ", (MAX_FILE_SIZE/BUF_SIZE)/(i+1));
	}

	printf("not doing massive io\n");
	return 0;

	printf("\nDone filling holes.  Time for some major forkage.\n");

	int thread_number = 0;
	if(fork()) { thread_number += 1; }
	if(fork()) { thread_number += 2; }
	if(fork()) { thread_number += 4; }
	if(fork()) { thread_number += 8; }
	printf("Thread %2i entering massive IO loop\n", thread_number);

	while(1)
	{
	//	count  = rand(); count  -= count  % 512;
		//count  = 1<<9;
		//offset = ((rand() >> 9) << 9) % MAX_FILE_SIZE; // offset -= offset % 512;
		//pread(fd, rdbuf, count, offset);
		
		//count = rand() % BUF_SIZE;
		//offset = rand() % MAX_FILE_SIZE;
		count  = 512;
		offset = (rand() * count) % MAX_FILE_SIZE;

	//	if(count + offset > MAX_FILE_SIZE) { count += MAX_FILE_SIZE - (count + offset) ; }
	
		pwrite(fd, wrbuf, count, offset);
	}

	return 0;
}

/*
void print_stats(FILE* stat)
{
	int pid;
	char* filename = (char*) calloc(500, sizeof(char));
	char state;
	int ppid;
	int grp;
	int session;
	int tty;
	int tpgid;
	long int flags;
	long int minor_faults;
	long int minor_faults_c;
	long int major_faults;
	long int major_faults_c;

	rewind(stat);
	fscanf(stat, "%d %s %c %d %d %d %d %d %lu %lu %lu %lu %lu", 
		&pid, filename, &state, &ppid, &grp, &session, &tty, &tpgid, &flags, &minor_faults, &minor_faults_c, &major_faults, &major_faults_c);
	assert(getpid() == pid);
	printf("Fault rate (minor  ) for PID %i is %li\n", pid, minor_faults);
	printf("Fault rate (minor_c) for PID %i is %li\n", pid, minor_faults_c);
	printf("Fault rate (major  ) for PID %i is %li\n", pid, major_faults);
	printf("Fault rate (major_c) for PID %i is %li\n", pid, major_faults_c);

	fflush(stdout);
}
*/

