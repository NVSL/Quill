// Header file for nvmfileops.c

#ifndef __NV_FILEOPS_H_
#define __NV_FILEOPS_H_
#endif

#include "nv_common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fiemap.h>
#include <pthread.h>
#include <signal.h>
#include <pthread.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "perfcount.h"

#include "nvp_mman.h"
#include "nvp_lock.h"
#include "rbtree.h"

#include "../../bankshot2/kernel/bankshot2_cache.h"

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

/* Default mmap size : PAGE_SIZE */
#define PAGE_SIZE	4096

struct NVFile
{
	struct NVNode* node;
	NVP_LOCK_DECL;
	volatile bool valid;
	int fd;
	volatile size_t* offset;
	bool canRead;
	bool canWrite;
	bool append;
	bool aligned;
	ino_t serialno; // duplicated so that iterating doesn't require following every node*
	bool posix;	// Use Posix operations
	ino_t cache_serialno; // duplicated so that iterating doesn't require following every node*
};

struct NVNode
{
	struct rb_root extent_tree;
	struct rb_root mmap_extent_tree;
	unsigned long *root;
	unsigned int height;
	int reference;	// How many fds refered this node
	int num_extents;
	ino_t serialno;
	NVP_LOCK_DECL;
	pthread_mutex_t mutex;
	char* volatile data; // the pointer itself is volatile
	volatile size_t length;
	volatile size_t maplength;
//	volatile int maxPerms;
//	volatile int valid; // for debugging purposes
	volatile size_t cache_length;
	ino_t cache_serialno; // duplicated so that iterating doesn't require following every node*
	unsigned long num_reads;
	unsigned long num_writes;
	unsigned long num_posix_writes;
	unsigned long num_read_kernels;
	unsigned long num_write_kernels;
	unsigned long num_read_mmaps;
	unsigned long num_write_mmaps;
	unsigned long num_read_segfaults;
	unsigned long num_write_segfaults;
	unsigned long long memcpy_read;
	unsigned long long memcpy_write;
	unsigned long long total_read;
	unsigned long long total_write;
	unsigned long long total_read_mmap;
	unsigned long long total_write_mmap;
	unsigned long long total_read_actual;
	unsigned long long total_write_actual;
	unsigned long long total_read_required;
	unsigned long long total_write_required;
};

struct extent_cache_entry
{
	struct rb_node node;
	struct rb_node mmap_node;
	off_t offset;
	size_t count;
	int dirty;
	unsigned long mmap_addr;
//	struct extent_cache_entry *next;
};

struct bankshot2_extent_info
{
	// Input value
	uint64_t offset; //file offset in bytes
	size_t request_len;
	// Output value
	uint64_t mmap_offset; //Mmap offset, align to MMAP_SIZE
	size_t mmap_length; //mmap length
	size_t extent_length;
	uint64_t file_length; //total file length in bytes
	unsigned long mmap_addr; // returned mmap address
};

// declare and alias all the functions in ALLOPS
BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _bankshot2_, ALLOPS_WPAREN)

int _bankshot2_sync(struct NVFile* file, int flags);
int _bankshot2_resize_map (struct NVFile* file, int newLength);
int _bankshot2_resize_file(struct NVFile* file, int newLength);
int _bankshot2_remap(struct NVFile* file, int newLength);

void bankshot2_print_extent_tree(struct NVNode *node);
void bankshot2_cleanup_extent_tree(struct NVNode *node);
int find_extent(struct NVFile *nvf, off_t *offset, size_t *count,
			unsigned long *mmap_addr);
void remove_extent(struct NVFile *nvf, off_t offset);
void add_extent(struct NVFile *nvf, off_t offset, size_t count, int write,
			unsigned long mmap_addr);
int first_extent(struct NVFile *nvf, off_t *offset, size_t *count, int *dirty,
			unsigned long *mmap_addr);

int find_extent_btree(struct NVFile *nvf, off_t *offset, size_t *count,
			unsigned long *mmap_addr);
void remove_extent_btree(struct NVFile *nvf, off_t offset, int btree_only);
void add_extent_btree(struct NVFile *nvf, off_t offset, size_t count, int write,
			unsigned long mmap_addr);
void bankshot2_cleanup_extent_btree(struct NVNode *node);
