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

#define IS_ERR(x) ((unsigned long)(x) >= (unsigned long)-4095)

