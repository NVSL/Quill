#include<sys/types.h>
#include<stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>
#include <sys/stat.h>


#include"Moneta.h"

#define MAGIC 0xDEADBEEF

#define MAX_OPEN_DEVS 10

int initialized = 0;

typedef struct {
     int magic;
     pid_t pid;
     char *device;
     int index;
} Channel;

Channel *openDevs[MAX_OPEN_DEVS];

void * LoadFunc(void * dl, char *name)
{
     void * dlsym_result = dlsym(dl, name);

     if(!dlsym_result) { 
	  fprintf(stderr, "Unable to load symbol %s...exiting.\n", name);
	  exit(1);
     } 
     return dlsym_result;
}

static int(*my_read)(int, void *, size_t);
static int(*my_write)(int, const void *, size_t);
static int(*my_lseek)(int, off_t, int);

#define LIBC_SO_LOC "/lib64/libc-2.5.so"

void Init();

size_t GetFileLength(int fd) {
     size_t l = my_lseek(fd, 0, SEEK_END);
     assert(l != -1);
     return l;
}


int moneta_CheckChannel(void *ci) {
     Channel *channel = (Channel*)ci;
     if (!channel) {
	  fprintf(stderr, "Null channel in process %d\n", getpid());
	  return 1;
     }

     if (channel->magic !=  MAGIC) {
	  fprintf(stderr, "Bad Magic in process %d:  %d\n", getpid(), channel->magic);
	  return 1;
     }

     if (channel->pid != getpid()) {
	  fprintf(stderr, "Process %d accessing channel for process %d\n", getpid(), channel->pid);
	  return 1;
     }
     return MONETA_SUCCESS;

}

static void CheckChannelSanity(void * ci)
{
     if (moneta_CheckChannel(ci)) {
	  exit(1);
     }
}

int moneta_OpenChannel(const char* chardev, void** ci)
{

     Init();


     int i;
     for(i = 0; i < MAX_OPEN_DEVS;i++) {
	  if (openDevs[i] != NULL && 
	      !strcmp(chardev, openDevs[i]->device)) {
	       return MONETA_ERROR_DEVICE_OPEN;
	  }
     }
     for(i = 0; i < MAX_OPEN_DEVS;i++) {
	  if (openDevs[i] == NULL) {
	       break;
	  }
     }
     if (i == MAX_OPEN_DEVS) {
	  fprintf(stderr, "Ran out of open device slots. Increase MAX_OPEN_DEVS in moneta_sim.c.  Exiting.\n");
	  exit(1);
     }

     Channel *channel = (Channel*)malloc(sizeof(Channel));
     openDevs[i] = channel;

     channel->magic = MAGIC;
     channel->pid = getpid();
     channel->device = strdup(chardev);
     channel->index = i;

     *ci = channel;
     
     return MONETA_SUCCESS;

}

int moneta_ReopenChannel(void **ci)
{
     Channel **c = (Channel**)ci;
     char * t = strdup((*c)->device);
     moneta_CloseChannel(*ci);
     moneta_OpenChannel(t, ci);
     free(t);
     return MONETA_SUCCESS;
}

int moneta_CloseChannel(void* ci)
{
     Channel *c = (Channel*)ci;
     if (openDevs[c->index] == NULL) {
	  return MONETA_ERROR_DEVICE_NOT_OPEN;
     }
     CheckChannelSanity(ci);
     openDevs[c->index] = NULL;
     free(ci);
     return MONETA_SUCCESS;
}


int ValidateAccess(void * ci, int file, uint64_t offset, size_t accessBytes)
{
     CheckChannelSanity(ci);

     struct stat f;
     int r = fstat(file, &f);
     if (r == -1) {
	  return MONETA_ERROR_SYS_ERROR;
     }

     int length = GetFileLength(file);
     int alignedLength;

     if ((length % MONETA_LENGTH_ALIGNMENT_BYTES) != 0) {
	  alignedLength = (length + MONETA_LENGTH_ALIGNMENT_BYTES)  - (length % MONETA_LENGTH_ALIGNMENT_BYTES);
     } else {
	  alignedLength = length;
     }
     
     if ((accessBytes % MONETA_LENGTH_ALIGNMENT_BYTES) != 0) {
	  return MONETA_ERROR_UNALIGNED_SIZE;
     }

     if (offset * MONETA_SECTOR_SIZE_BYTES + accessBytes > alignedLength) {
	  return MONETA_ERROR_ACCESS_TOO_LONG;
     }


     return MONETA_SUCCESS;
}


int moneta_Read(void* ci, int file, uint64_t offset, size_t length, void* buffer)
{
     int r = ValidateAccess(ci, file, offset, length);
     if (r != 0) {
	  return r;
     }

     {
	  int r = my_lseek(file,offset, SEEK_SET);
	  if (r == -1) {
	       perror("moneta_simple_sim");
	       exit(1);
	  }
     }

     memset(buffer, 0, length);

     {
	  int r = my_read(file,buffer, length);
	  if (r == -1) {
	       return r;
	  } else {
	       return MONETA_SUCCESS;
	  }
     }
}

int moneta_Write(void* ci, int file, uint64_t offset, size_t length, const void* buffer)
{
     int r = ValidateAccess(ci, file, offset, length);
     if (r != MONETA_SUCCESS) {
	  return r;
     }

     size_t filePos = offset * MONETA_SECTOR_SIZE_BYTES;
     
     size_t adjustedLength;


     if (filePos + length > GetFileLength(file)) {
	  adjustedLength = GetFileLength(file) - filePos;
     } else {
	  adjustedLength = length;
     }

     {
	  int r = my_lseek(file, filePos, SEEK_SET);
	  if (r == -1) {
	       perror("moneta_simple_sim");
	       exit(0);
	  }
     }
     
     
     {
	  int r = my_write(file, buffer, adjustedLength);
	  if (r == adjustedLength) {
	       return MONETA_SUCCESS;
	  } else if (r == -1) {
	       return MONETA_ERROR_SYS_ERROR;
	  } else {
	       assert(0);
	       return MONETA_ERROR_SYS_ERROR;
	  }
     }
}

void Init() {
     if (initialized != 0) {
	  return;
     }

     void * dl = dlopen(LIBC_SO_LOC, RTLD_LAZY|RTLD_LOCAL);
     
     my_read = (int (*)(int, void *, size_t))LoadFunc(dl, "read");
     my_write = (int (*)(int, const void *, size_t))LoadFunc(dl, "write");
     my_lseek = (int (*)(int, off_t, int))LoadFunc(dl, "lseek");

     int i;
     for(i = 0; i < MAX_OPEN_DEVS; i++) {
	  openDevs[i] = NULL;
     }
     initialized = 1;
}
