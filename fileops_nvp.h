// Header file for nvmfileops.c

#ifndef __NV_FILEOPS_H_
#define __NV_FILEOPS_H_

#include "nv_common.h"

#define ENV_NV_FOP "NVP_NV_FOP"


#ifndef SYNC_ON_WRITE
	// if 1, changes to file length during _nvp_write are immediately relayed to the OS
	// if 0, changes to file length during _nvp_write are updated on _nvp_close only
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
	void* datastart;
	int p; // offset from datastart to R/W position
	int length;
	bool canRead;
	bool canWrite;
	bool append;
	int fd;
	int prot;  // used for _nvp_remap
	int flags; // used for _nvp_remap
};

// declare and alias all the functions in ALLOPS
BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _nvp_, ALLOPS_WPAREN)

int _nvp_sync(struct NVFile* file, int flags);
int _nvp_resize_map (struct NVFile* file, int newLength);
int _nvp_resize_file(struct NVFile* file, int newLength);
int _nvp_remap(struct NVFile* file, int newLength);


#endif

