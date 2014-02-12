// Header file for nvmfileops.c

#ifndef __NV_FILEOPS_H_
#define __NV_FILEOPS_H_

#include "nv_common.h"

#define ENV_NV_FOP "NVP_NV_FOP"


#ifndef SYNC_ON_WRITE
	// if 1, changes to file length during _bankshot2_write are immediately relayed to the OS
	// if 0, changes to file length during _bankshot2_write are updated on _bankshot2_close only
	// TODO: this feature requires additional testing
	#define SYNC_ON_WRITE 1 
#endif

#define _NVP_USE_DEFAULT_FILEOPS NULL

#define _NVP_CHECK_USE_DEFAULT_FILEOPS(name, ...) do{ \
	if(_NVP_fd_lookup[nvfile] == _NVP_USE_DEFAULT_FILEOPS) { \
		DEBUG("Using %s for %i\n", #name, nvfile); \
		return name(__VA_ARGS__); \
	} }while(0)

#define CHECK_VALID_FD(name) do{ \
	if( (!nvfile) || (!_NVP_fd_lookup) || (_NVP_fd_lookup[nvfile] == 0) || (nvfile < 0) ) { \
		ERROR("can't %s: invalid file descriptor (%i)\n", name, nvfile); \
		errno = EBADF; \
		return -1; \
	} }while(0)

#define CHECK_VALID_FD2(name) do{ \
	if( (fd2 < 1) ) { \
		ERROR("can't %s: invalid file descriptor (%i)\n", name, fd2);\
		errno = EBADF; \
		return -1; \
	} }while(0)

struct NVFile
{
	NVP_LOCK_DECL;
	volatile bool valid;
	int fd;
	volatile size_t* offset;
	bool canRead;
	bool canWrite;
	bool append;
	bool aligned;
	ino_t serialno; // duplicated so that iterating doesn't require following every node*
	struct NVNode* node;
	bool posix;	// Use Posix operations
	int cache_fd;	// Cache file fd
	ino_t cache_serialno; // duplicated so that iterating doesn't require following every node*
};

struct NVNode
{
	ino_t serialno;
	NVP_LOCK_DECL;
	char* volatile data; // the pointer itself is volatile
	volatile size_t length;
	volatile size_t maplength;
//	volatile int maxPerms;
//	volatile int valid; // for debugging purposes
	int cache_fd;	// Cache file fd
	ino_t cache_serialno; // duplicated so that iterating doesn't require following every node*
};

// declare and alias all the functions in ALLOPS
BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _bankshot2_, ALLOPS_WPAREN)

int _bankshot2_sync(struct NVFile* file, int flags);
int _bankshot2_resize_map (struct NVFile* file, int newLength);
int _bankshot2_resize_file(struct NVFile* file, int newLength);
int _bankshot2_remap(struct NVFile* file, int newLength);


#endif

