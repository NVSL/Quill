// Header file for nvmfileops.c

#include "nv_common.h"

#define ENV_NV_FOP "NVP_NV_FOP"

#define _NVP_USE_DEFAULT_FILEOPS NULL

/******************* Data Structures ********************/

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
	bool posix;
	bool debug;
};

struct NVNode
{
	ino_t serialno;
	NVP_LOCK_DECL;
	char* volatile data; // the pointer itself is volatile
	volatile size_t length;
	volatile size_t maplength;
	unsigned long *root;
	unsigned int height;
	int reference;
//	volatile int maxPerms;
//	volatile int valid; // for debugging purposes
};

/******************* Locking ********************/

#define NVP_LOCK_FD_RD(nvf, cpuid)	NVP_LOCK_RD(	   nvf->lock, cpuid)
#define NVP_UNLOCK_FD_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->lock, cpuid)
#define NVP_LOCK_FD_WR(nvf)		NVP_LOCK_WR(	   nvf->lock)
#define NVP_UNLOCK_FD_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->lock)

#define NVP_LOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_RD(	   nvf->node->lock, cpuid)
#define NVP_UNLOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->node->lock, cpuid)
#define NVP_LOCK_NODE_WR(nvf)		NVP_LOCK_WR(	   nvf->node->lock)
#define NVP_UNLOCK_NODE_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->node->lock)

/******************* MMAP ********************/

#define IS_ERR(x) ((unsigned long)(x) >= (unsigned long)-4095)

#define MAX_MMAP_SIZE	2097152
#define	ALIGN_MMAP_DOWN(addr)	((addr) & ~(MAX_MMAP_SIZE - 1))

#define DO_ALIGNMENT_CHECKS 0

#define DO_MSYNC(nvf) do{ \
	DEBUG("NOT doing a msync\n"); }while(0)
/*
	DEBUG("Doing a msync on fd %i (node %p)\n", nvf->fd, nvf->node); \
	if(msync(nvf->node->data, nvf->node->maplength, MS_SYNC|MS_INVALIDATE)) { \
		ERROR("Failed to msync for fd %i\n", nvf->fd); \
		assert(0); \
	} }while(0)
*/

/******************* Checking ********************/

#define NOSANITYCHECK 1
#if NOSANITYCHECK
	#define SANITYCHECK(x)
#else
	#define SANITYCHECK(x) if(UNLIKELY(!(x))) { ERROR("NVP_SANITY("#x") failed!\n"); exit(101); }
#endif

#define NVP_CHECK_NVF_VALID(nvf) do{ \
	if(UNLIKELY(!nvf->valid)) { \
		DEBUG("Invalid file descriptor: %i\n", file); \
		errno = EBADF; \
		NVP_UNLOCK_FD_RD(nvf, cpuid); \
		return -1; \
	} \
	else \
	{ \
		DEBUG("this function is operating on node %p\n", nvf->node); \
		SANITYCHECKNVF(nvf); \
		DO_MSYNC(nvf); \
	} \
	} while(0)

#define NVP_CHECK_NVF_VALID_WR(nvf) do{ \
	if(UNLIKELY(!nvf->valid)) { \
		DEBUG("Invalid file descriptor: %i\n", file); \
		errno = EBADF; \
		NVP_UNLOCK_FD_WR(nvf); \
		return -1; \
	} \
	else \
	{ \
		DEBUG("this function is operating on node %p\n", nvf->node); \
		SANITYCHECKNVF(nvf); \
		DO_MSYNC(nvf); \
	} \
	} while(0)

#define SANITYCHECKNVF(nvf) \
		SANITYCHECK(nvf->valid); \
		SANITYCHECK(nvf->node != NULL); \
		SANITYCHECK(nvf->fd >= 0); \
		SANITYCHECK(nvf->fd < OPEN_MAX); \
		SANITYCHECK(nvf->offset != NULL); \
		SANITYCHECK(*nvf->offset >= 0); \
		SANITYCHECK(nvf->serialno != 0); \
		SANITYCHECK(nvf->serialno == nvf->node->serialno); \
		SANITYCHECK(nvf->node->length >=0); \
		SANITYCHECK(nvf->node->maplength >= nvf->node->length); \
		SANITYCHECK(nvf->node->data != NULL)

