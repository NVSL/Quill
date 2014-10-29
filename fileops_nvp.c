// a module which repalces the standart POSIX functions with memory mapped equivalents

#include "nv_common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>

#include "perfcount.h"

#include "nvp_mman.h"
#include "nvp_lock.h"

#include "fileops_nvp.h"


BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _nvp_, ALLOPS_WPAREN)

int MMAP_PAGE_SIZE;

void* _nvp_zbuf; // holds all zeroes.  used for aligned file extending. TODO: does sharing this hurt performance?

pthread_spinlock_t	node_lookup_lock;

struct NVFile* _nvp_fd_lookup;
struct NVNode* _nvp_node_lookup;
int _nvp_ino_lookup[1024];

void _nvp_init2(void);

MODULE_REGISTRATION_F("nvp", _nvp_, _nvp_init2(); );

#define NVP_WRAP_HAS_FD(op) \
	RETT_##op _nvp_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_nvp_);\
		DEBUG("_nvp_"#op" is just wrapping %s->"#op"\n", _nvp_fileops->name); \
		if(UNLIKELY(file>=OPEN_MAX)) { DEBUG("file descriptor too large (%i > %i)\n", file, OPEN_MAX-1); errno = EBADF; return (RETT_##op) -1; } \
		if(UNLIKELY(file<0)) { DEBUG("file < 0 (file = %i).  return -1;\n", file); errno = EBADF; return (RETT_##op) -1; } \
		if(UNLIKELY(!_nvp_fd_lookup[file].valid)) { DEBUG("That file descriptor (%i) is invalid\n", file); errno = EBADF; return -1; } \
		DEBUG("_nvp_" #op " is calling %s->" #op "\n", _nvp_fileops->name); \
		return (RETT_##op) _nvp_fileops->op( CALL_##op ); \
	}

#define NVP_WRAP_NO_FD(op) \
	RETT_##op _nvp_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_nvp_);\
		DEBUG("_nvp_"#op" is just wrapping %s->"#op"\n", _nvp_fileops->name); \
		return _nvp_fileops->op( CALL_##op ); \
	}

#define NVP_WRAP_HAS_FD_IWRAP(r, data, elem) NVP_WRAP_HAS_FD(elem)
#define NVP_WRAP_NO_FD_IWRAP(r, data, elem) NVP_WRAP_NO_FD(elem)

BOOST_PP_SEQ_FOR_EACH(NVP_WRAP_HAS_FD_IWRAP, placeholder, (ACCEPT))
BOOST_PP_SEQ_FOR_EACH(NVP_WRAP_NO_FD_IWRAP, placeholder, (PIPE) (FORK) (SOCKET))


/* ============================= memcpy =============================== */

extern long copy_user_nocache(void *dst, const void *src, unsigned size, int zerorest);

static inline int copy_from_user_inatomic_nocache(void *dst, const void *src, unsigned size) {
	return copy_user_nocache(dst, src, size, 0);
}

static inline void* my_memcpy_nocache(void* dst, const void* src, unsigned size) {
	if(copy_from_user_inatomic_nocache(dst, src, size)) {
		return dst;
	} else { 
		return 0;
	}
}

static inline void *intel_memcpy(void * __restrict__ b, const void * __restrict__ a, size_t n){
	char *s1 = b;
	const char *s2 = a;
	for(; 0<n; --n)*s1++ = *s2++;
	return b;
}

void *(*import_memcpy)(void * __restrict__ b, const void * __restrict__ a, size_t n);

extern void * __memcpy(void * __restrict__ to, const void * __restrict__ from, size_t len);

#define MMX2_MEMCPY_MIN_LEN 0x40
#define MMX_MMREG_SIZE 8

// ftp://ftp.work.acer-euro.com/gpl/AS1800/xine-lib/src/xine-utils/memcpy.c
static void * mmx2_memcpy(void * __restrict__ to, const void * __restrict__ from, size_t len)
{
  void *retval;
  size_t i;
  retval = to;

  /* PREFETCH has effect even for MOVSB instruction ;) */
  __asm__ __volatile__ (
    "   prefetchnta (%0)\n"
    "   prefetchnta 32(%0)\n"
    "   prefetchnta 64(%0)\n"
    "   prefetchnta 96(%0)\n"
    "   prefetchnta 128(%0)\n"
    "   prefetchnta 160(%0)\n"
    "   prefetchnta 192(%0)\n"
    "   prefetchnta 224(%0)\n"
    "   prefetchnta 256(%0)\n"
    "   prefetchnta 288(%0)\n"
    : : "r" (from) );

  if(len >= MMX2_MEMCPY_MIN_LEN)
  {
    register unsigned long int delta;
    /* Align destinition to MMREG_SIZE -boundary */
    delta = ((unsigned long int)to)&(MMX_MMREG_SIZE-1);
    if(delta)
    {
      delta=MMX_MMREG_SIZE-delta;
      len -= delta;
      memcpy(to, from, delta);
    }
    i = len >> 6; /* len/64 */
    len&=63;
    for(; i>0; i--)
    {
      __asm__ __volatile__ (
      "prefetchnta 320(%0)\n"
      "prefetchnta 352(%0)\n"
      "movq (%0), %%mm0\n"
      "movq 8(%0), %%mm1\n"
      "movq 16(%0), %%mm2\n"
      "movq 24(%0), %%mm3\n"
      "movq 32(%0), %%mm4\n"
      "movq 40(%0), %%mm5\n"
      "movq 48(%0), %%mm6\n"
      "movq 56(%0), %%mm7\n"
      "movntq %%mm0, (%1)\n"
      "movntq %%mm1, 8(%1)\n"
      "movntq %%mm2, 16(%1)\n"
      "movntq %%mm3, 24(%1)\n"
      "movntq %%mm4, 32(%1)\n"
      "movntq %%mm5, 40(%1)\n"
      "movntq %%mm6, 48(%1)\n"
      "movntq %%mm7, 56(%1)\n"
      :: "r" (from), "r" (to) : "memory");
      //((const unsigned char *)from)+=64;
      //((unsigned char *)to)+=64;
      from = (void*)(((const unsigned char *)from) + 64);
      to = (void*)(((unsigned char *)to) + 64);
    }
     /* since movntq is weakly-ordered, a "sfence"
     * is needed to become ordered again. */
    __asm__ __volatile__ ("sfence":::"memory");
    __asm__ __volatile__ ("emms":::"memory");
  }
  /*
   *	Now do the tail of the block
   */
  if(len) memcpy(to, from, len);
  return retval;
}

static char* memcpy1(char *to, char *from, size_t n)
{
	long esi, edi;
	int ecx;
	esi = (long)from;
	edi = (long)to;
	asm volatile("rep ; movsl"
		: "=&c" (ecx), "=&D" (edi), "=&S" (esi)
		: "0" (n / 4), "1" (edi), "2" (esi)
		: "memory"
		);
	return to;
}



/* ====================== Memory operation policy ======================= */

// modifications to support different FSYNC policies
//#define MEMCPY memcpy
//define MEMCPY intel_memcpy
//#define MEMCPY (void*)copy_from_user_inatomic_nocache
//#define MEMCPY my_memcpy_nocache
#define MEMCPY mmx2_memcpy
//#define MEMCPY memcpy1
#define MMAP mmap
//#define FSYNC fsync

#define FSYNC_POLICY_NONE 0
#define FSYNC_POLICY_FLUSH_ON_FSYNC 1
#define FSYNC_POLICY_UNCACHEABLE_MAP 2
#define FSYNC_POLICY_NONTEMPORAL_WRITES 3
#define FSYNC_POLICY_FLUSH_ON_WRITE 4

#define FSYNC_POLICY FSYNC_POLICY_NONE

#if FSYNC_POLICY == FSYNC_POLICY_NONE
	#define FSYNC_MEMCPY MEMCPY
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC 
#elif FSYNC_POLICY == FSYNC_POLICY_FLUSH_ON_FSYNC
	#define FSYNC_MEMCPY MEMCPY
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC fsync_flush_on_fsync(nvf)
#elif FSYNC_POLICY == FSYNC_POLICY_UNCACHEABLE_MAP
	#define FSYNC_MEMCPY MEMCPY
	#define FSYNC_MMAP mmap_fsync_uncacheable_map
	#define FSYNC_FSYNC 
#elif FSYNC_POLICY == FSYNC_POLICY_NONTEMPORAL_WRITES
	#define FSYNC_MEMCPY memcpy_fsync_nontemporal_writes
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC _mm_mfence()
#elif FSYNC_POLICY == FSYNC_POLICY_FLUSH_ON_WRITE
	#define FSYNC_MEMCPY memcpy_fsync_flush_on_write
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC _mm_mfence()
#endif


/* ============================= Fsync =============================== */

static inline void fsync_flush_on_fsync(struct NVFile* nvf)
{
	_mm_mfence();
	do_cflush_len(nvf->node->data, nvf->node->length);
	_mm_mfence();
}

void *mmap_fsync_uncacheable_map(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
	void* result = MMAP( start, length, prot, flags, fd, offset );

	// mark the result as uncacheable // TODO
//	not_implemented++;

	return result;
}

void *memcpy_fsync_nontemporal_writes(void *dest, const void *src, size_t n)
{
	// TODO: an asm version of memcpy, with movdqa replaced by movntdq
//	void* result; = FSYNC_MEMCPY( not_implemented );

	_mm_mfence();

	return NULL;
//	return result;
}

void *memcpy_fsync_flush_on_write(void *dest, const void *src, size_t n)
{
	// first, perform the memcpy as usual
	void* result = MEMCPY(dest, src, n);

	// then, flush all the pages which were just modified.
	_mm_mfence();
	do_cflush_len(dest, n);
	_mm_mfence();

	return result;
}


/* ============================= Timing =============================== */

enum timing_category {
	do_pread_t = 0,
	do_pwrite_t,
	memcpyr_t,
	memcpyw_t,
	lookup_t,
	insert_t,
	read_t,
	write_t,
	pread_t,
	pwrite_t,
	open_t,
	close_t,
	posix_open_t,
	posix_close_t,
	get_node_t,
	alloc_node_t,
	fsync_t,
	fdsync_t,
	mmap_t,
	get_mmap_t,
	TIMING_NUM,	// Keep as last entry
};

unsigned long long Countstats[TIMING_NUM];
unsigned long long Timingstats[TIMING_NUM];
const char *Timingstring[TIMING_NUM] = 
{
	"do_pread",
	"do_pwrite",
	"memcpy_read",
	"memcpy_write",
	"Tree_lookup",
	"Tree_insert",
	"READ",
	"WRITE",
	"PREAD",
	"PWRITE",
	"OPEN",
	"CLOSE",
	"Posix OPEN",
	"Posix CLOSE",
	"get_node",
	"alloc_node",
	"Fsync",
	"Fdsync",
	"mmap",
	"get_mmap_addr",
};

typedef struct timespec timing_type;

#if MEASURE_TIMING

#define	NVP_START_TIMING(name, start) \
	clock_gettime(CLOCK_MONOTONIC, &start)

#define	NVP_END_TIMING(name, start) \
	{timing_type end; \
	 clock_gettime(CLOCK_MONOTONIC, &end); \
	 Countstats[name]++; \
	 Timingstats[name] += (end.tv_sec - start.tv_sec) * 1e9 \
				+ (end.tv_nsec - start.tv_nsec); \
	}

void nvp_print_time_stats(void)
{
	int i;

	printf("==================== NVP timing stats: ====================\n");
	for (i = 0; i < TIMING_NUM; i++)
		printf("%s: count %llu, timing %llu, average %llu\n",
			Timingstring[i], Countstats[i], Timingstats[i],
			Countstats[i] ? Timingstats[i] / Countstats[i] : 0);
}

#else

#define NVP_START_TIMING(name, start) {}

#define  NVP_END_TIMING(name, start) \
	{Countstats[name]++;}

void nvp_print_time_stats(void)
{
	int i;

	printf("==================== NVP timing stats: ====================\n");
	for (i = 0; i < TIMING_NUM; i++)
		printf("%s: count %llu\n", Timingstring[i], Countstats[i]);
}

#endif

/* ============================= IO stats =============================== */

unsigned int num_open;
unsigned int num_close;
unsigned int num_read;
unsigned int num_write;
unsigned int num_mmap;
unsigned int num_memcpy_read;
unsigned int num_memcpy_write;
unsigned int num_posix_read;
unsigned int num_posix_write;
unsigned long long read_size;
unsigned long long write_size;
unsigned long long memcpy_read_size;
unsigned long long memcpy_write_size;
unsigned long long posix_read_size;
unsigned long long posix_write_size;
volatile size_t _nvp_wr_extended;
volatile size_t _nvp_wr_total;

void nvp_print_io_stats(void)
{
	printf("====================== NVP IO stats: ======================\n");
	printf("open %u, close %u\n", num_open, num_close);
	printf("READ: count %u, size %llu, average %llu\n", num_read,
		read_size, num_read ? read_size / num_read : 0);
	printf("WRITE: count %u, size %llu, average %llu\n", num_write,
		write_size, num_write ? write_size / num_write : 0);
	printf("memcpy READ: count %u, size %llu, average %llu\n",
		num_memcpy_read, memcpy_read_size,
		num_memcpy_read ? memcpy_read_size / num_memcpy_read : 0);
	printf("memcpy WRITE: count %u, size %llu, average %llu\n",
		num_memcpy_write, memcpy_write_size,
		num_memcpy_write ? memcpy_write_size / num_memcpy_write : 0);
	printf("posix READ: count %u, size %llu, average %llu\n",
		num_posix_read, posix_read_size,
		num_posix_read ? posix_read_size / num_posix_read : 0);
	printf("posix WRITE: count %u, size %llu, average %llu\n",
		num_posix_write, posix_write_size,
		num_posix_write ? posix_write_size / num_posix_write : 0);
	printf("write extends %lu, total %lu\n", _nvp_wr_extended,
		_nvp_wr_total);
}

/* ========================== Internal methods =========================== */

void nvp_cleanup_node(struct NVNode *node, int free_root);

void nvp_cleanup(void)
{
	int i;

	free(_nvp_fd_lookup);

	pthread_spin_lock(&node_lookup_lock);

	for (i = 0; i< OPEN_MAX; i++)
		nvp_cleanup_node(&_nvp_node_lookup[i], 1);

	pthread_spin_unlock(&node_lookup_lock);

	free(_nvp_node_lookup);
}

void nvp_exit_handler(void)
{
	MSG("Exit: print stats\n");
	nvp_print_time_stats();
	nvp_print_io_stats();
	nvp_cleanup();
}

void _nvp_SIGUSR1_handler(int sig)
{
	MSG("SIGUSR1: print stats\n");
	nvp_print_time_stats();
	nvp_print_io_stats();
}

void _nvp_SIGBUS_handler(int sig)
{
	ERROR("We got a SIGBUS (sig %i)! "
		"This almost certainly means someone tried to access an area "
		"inside an mmaped region but past the length of the mmapped "
		"file.\n", sig);
	assert(0);
}

void _nvp_init2(void)
{
	int i;

	assert(!posix_memalign(((void**)&_nvp_zbuf), 4096, 4096));

	_nvp_fd_lookup = (struct NVFile*)calloc(OPEN_MAX,
						sizeof(struct NVFile));
	if (!_nvp_fd_lookup)
		assert(0);

	for(i = 0; i < OPEN_MAX; i++) {
		_nvp_fd_lookup[i].valid = 0;
		NVP_LOCK_INIT(_nvp_fd_lookup[i].lock);
	}

	_nvp_node_lookup = (struct NVNode*)calloc(OPEN_MAX,
						sizeof(struct NVNode));
	if (!_nvp_node_lookup)
		assert(0);

	memset(_nvp_node_lookup, 0, OPEN_MAX * sizeof(struct NVNode));

	for(i = 0; i < OPEN_MAX; i++) {
		NVP_LOCK_INIT(_nvp_node_lookup[i].lock);
	}

	pthread_spin_init(&node_lookup_lock, PTHREAD_PROCESS_SHARED);

	MMAP_PAGE_SIZE = getpagesize();
	SANITYCHECK(MMAP_PAGE_SIZE > 100);

	DEBUG("Installing signal handler.\n");
	signal(SIGBUS, _nvp_SIGBUS_handler);
	/* For filebench */
	signal(SIGUSR1, _nvp_SIGUSR1_handler);

	atexit(nvp_exit_handler);
}

void nvp_free_btree(unsigned long *root, unsigned long height)
{
	int i;

	if (height == 0) {
#if UNMAP_ON_CLOSE
		for (i = 0; i < 1024; i++) {
			if (root && root[i]) {
				DEBUG("munmap: %d, addr 0x%lx\n",
					i, root[i]);
				munmap(root[i], MAX_MMAP_SIZE);
				root[i] = 0;
			}
		}
#endif
		return;
	}

	for (i = 0; i < 1024; i++) {
		if (root[i]) {
			nvp_free_btree((unsigned long *)root[i],
					height - 1);
			root[i] = 0;
		}
	}

	free(root);
}

void nvp_cleanup_node(struct NVNode *node, int free_root)
{
	unsigned int height = node->height;
	unsigned long *root = node->root;

	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);
	nvp_free_btree(root, height);

	node->height = 0;

	if (node->root && free_root) {
		free(node->root);
		node->root = NULL;
		return;
	}

	if (node->root)
		memset(node->root, 0, 1024 * sizeof(unsigned long));
}

void nvp_init_node(struct NVNode *node)
{
	if (!node->root)
		node->root = malloc(1024 * sizeof(unsigned long));

	memset(node->root, 0, 1024 * sizeof(unsigned long));
}

struct NVNode * nvp_allocate_node(void)
{
	struct NVNode *node = NULL;
	int i, candidate = -1;
	timing_type alloc_node_time;
	NVP_START_TIMING(alloc_node_t, alloc_node_time);

	for (i = 0; i < OPEN_MAX; i++) {
		if (_nvp_node_lookup[i].serialno == 0) {
			DEBUG("Allocate unused node %d\n", i);
			node = &_nvp_node_lookup[i];
			break;
		}

		if (candidate == -1 && _nvp_node_lookup[i].reference == 0)
			candidate = i;
	}

	if (node) {
		NVP_END_TIMING(alloc_node_t, alloc_node_time);
		return node;
	}

	if (candidate != -1) {
		node = &_nvp_node_lookup[candidate];
		DEBUG("Allocate unreferenced node %d\n", candidate);
		NVP_END_TIMING(alloc_node_t, alloc_node_time);
		return node;
	}

	NVP_END_TIMING(alloc_node_t, alloc_node_time);
	return NULL;
	
}

struct NVNode * nvp_get_node(const char *path, struct stat *file_st)
{
	int i, index;
	struct NVNode *node = NULL;
	timing_type get_node_time;
	NVP_START_TIMING(get_node_t, get_node_time);

	pthread_spin_lock(&node_lookup_lock);

	index = file_st->st_ino % 1024;
	if (_nvp_ino_lookup[index]) {
		i = _nvp_ino_lookup[index];
		if ( _nvp_fd_lookup[i].node &&
				_nvp_fd_lookup[i].node->serialno ==
					file_st->st_ino) {
			DEBUG("File %s is (or was) already open in fd %i "
				"(this fd hasn't been __open'ed yet)! "
				"Sharing nodes.\n", path, i);
			node = _nvp_fd_lookup[i].node;
			SANITYCHECK(node != NULL);
			node->reference++;
			pthread_spin_unlock(&node_lookup_lock);
			goto out;
		}
	}

	if(node == NULL) {
		DEBUG("File %s is not already open. "
			"Allocating new NVNode.\n", path);
		node = nvp_allocate_node();
		assert(node);
		node->serialno = file_st->st_ino;
		node->data = NULL;
		node->reference++;
	}

	pthread_spin_unlock(&node_lookup_lock);

	NVP_LOCK_WR(node->lock);
	if (node->height)
		nvp_cleanup_node(node, 0);
	node->height = 0;
	node->length = file_st->st_size;
	node->maplength = 0;
	nvp_init_node(node);
	NVP_LOCK_UNLOCK_WR(node->lock);

out:
	NVP_END_TIMING(get_node_t, get_node_time);
	return node;
}

static unsigned long calculate_capacity(unsigned int height)
{
	unsigned long capacity = MAX_MMAP_SIZE;

	while (height) {
		capacity *= 1024;
		height--;
	}

	return capacity;
}

static unsigned int calculate_new_height(off_t offset)
{
	unsigned int height = 0;
	off_t temp_offset = offset / ((unsigned long)1024 * MAX_MMAP_SIZE);

	while (temp_offset) {
		temp_offset /= 1024;
		height++;
	}

	return height;
}

static int nvp_get_mmap_address(struct NVFile *nvf, off_t offset, size_t count,
		unsigned long *mmap_addr, size_t *extent_length, int wr_lock,
		int cpuid)
{
	int i;
	int index;
	unsigned int height = nvf->node->height;
	unsigned int new_height;
	unsigned long capacity = MAX_MMAP_SIZE;
	unsigned long *root = nvf->node->root;
	unsigned long start_addr;
	off_t start_offset = offset;
	timing_type mmap_time, lookup_time, insert_time;

	DEBUG("Get mmap address: offset 0x%lx, height %u\n", offset, height);
	DEBUG("root @ %p\n", root);

	NVP_START_TIMING(lookup_t, lookup_time);
	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		DEBUG("index %d\n", index);
		if (index >= 1024 || root[index] == 0) {
			NVP_END_TIMING(lookup_t, lookup_time);
			goto not_found;
		}
		if (height) {
			root = (unsigned long *)root[index];
			DEBUG("%p\n", root);
		} else {
			start_addr = root[index];
			DEBUG("addr 0x%lx\n", start_addr);
		}
		start_offset = start_offset % capacity;
	} while(height--);
	NVP_END_TIMING(lookup_t, lookup_time);

	if (IS_ERR(start_addr) || start_addr == 0) {
		MSG("ERROR!\n");
		assert(0);
	}

	*mmap_addr = start_addr + offset % MAX_MMAP_SIZE;
	*extent_length = MAX_MMAP_SIZE - (offset % MAX_MMAP_SIZE);

	DEBUG("Found: mmap addr 0x%lx, extent length %lu\n",
			*mmap_addr, *extent_length);

	return 0;

not_found:
	DEBUG("Not found, perform mmap\n");

	if (offset >= ALIGN_MMAP_DOWN(nvf->node->length)) {
		DEBUG("File length smaller than offset: "
			"length 0x%lx, offset 0x%lx\n",
			nvf->node->length, offset);
		return 1;
	}

	if (!wr_lock) {
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		NVP_LOCK_NODE_WR(nvf);
	}

	start_offset = ALIGN_MMAP_DOWN(offset);	
	if (start_offset + MAX_MMAP_SIZE > nvf->node->length) {
		ERROR("File length smaller than offset: "
			"length 0x%lx, offset 0x%lx\n",
			nvf->node->length, offset);
		return 1;
	}

	NVP_START_TIMING(mmap_t, mmap_time);

	int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);
	start_addr = (unsigned long) FSYNC_MMAP
	(
		NULL,
		MAX_MMAP_SIZE,
		max_perms, //max_perms,
		MAP_SHARED | MAP_POPULATE,
//		MAP_SHARED,
		nvf->fd, //fd_with_max_perms,
		start_offset
	);
	NVP_END_TIMING(mmap_t, mmap_time);
	num_mmap++;

	if (IS_ERR(start_addr) || start_addr == 0)
	{
		MSG("mmap failed for fd %i: %s, mmap count %d, addr %lu\n",
				nvf->fd, strerror(errno), num_mmap, start_addr);
		MSG("Open count %d, close count %d\n", num_open, num_close);
		MSG("Use posix operations for fd %i instead.\n", nvf->fd);
		nvf->posix = 1;
		assert(0);
	}

	DEBUG("mmap offset 0x%lx, start_offset 0x%lx\n", offset, start_offset);

	height = nvf->node->height;
	new_height = calculate_new_height(offset);

	NVP_START_TIMING(insert_t, insert_time);
	if (height < new_height) {
		MSG("Increase height from %u to %u\n", height, new_height);

		while (height < new_height) {
			unsigned long old_root = (unsigned long)nvf->node->root;
			nvf->node->root = malloc(1024 * sizeof(unsigned long));
			DEBUG("Malloc new root @ %p\n", nvf->node->root);
			for (i = 0; i < 1024; i++)
				nvf->node->root[i] = 0;
			nvf->node->root[0] = (unsigned long)old_root;
			DEBUG("Old root 0x%lx\n", nvf->node->root[0]);
			height++;
		}

		nvf->node->height = new_height;
		height = new_height;
	}

	root = nvf->node->root;
	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		DEBUG("index %d\n", index);
		if (height) {
			if (root[index] == 0) {
				root[index] = (unsigned long)malloc(1024 *
						sizeof(unsigned long));
				DEBUG("Malloc new leaf @%p, height %u, "
					"index %u\n",
					root[index], height, index);
				root = (unsigned long *)root[index];
				for (i = 0; i < 1024; i++)
					root[i] = 0;
			} else
				root = (unsigned long *)root[index];
		} else {
			root[index] = start_addr;
			DEBUG("mmap for fd %i: %d, addr 0x%lx\n",
				nvf->fd, index, start_addr);
		}
		start_offset = start_offset % capacity;
	} while(height--);
	NVP_END_TIMING(insert_t, insert_time);

	*mmap_addr = start_addr + offset % MAX_MMAP_SIZE;
	*extent_length = MAX_MMAP_SIZE - (offset % MAX_MMAP_SIZE);

	if (!wr_lock) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
	}

	DEBUG("mmap addr 0x%lx, extent length %lu\n",
			*mmap_addr, *extent_length);
	return 0;
}


RETT_PREAD _nvp_do_pread(INTF_PREAD, int wr_lock, int cpuid)
{
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	SANITYCHECKNVF(nvf);
	timing_type do_pread_time, memcpyr_time, get_mmap_time;
	NVP_START_TIMING(do_pread_t, do_pread_time);
	int ret;
	off_t read_offset;
	size_t read_count, extent_length;
	size_t posix_read;
	unsigned long mmap_addr = 0;

	ssize_t available_length = (nvf->node->length) - offset;

	if (UNLIKELY(!nvf->canRead)) {
		DEBUG("FD not open for reading: %i\n", file);
		errno = EBADF;
		return -1;
	}

	else if (UNLIKELY(offset < 0))
	{
		DEBUG("Requested read at negative offset (%li)\n", offset);
		errno = EINVAL;
		return -1;
	}

	if(nvf->aligned)
	{
		DEBUG("This read must be aligned.  Checking alignment.\n");

		if(UNLIKELY(available_length <= 0))
		{
			DEBUG("Actually there weren't any bytes available "
				"to read.  Bye! (length %li, offset %li, "
				"available_length %li)\n", nvf->node->length,
				offset, available_length);
			return 0;
		}

		if(UNLIKELY(count % 512))
		{
			DEBUG("cout is not aligned to 512 (count was %i)\n",
				count);
			errno = EINVAL;
			return -1;
		}
		if(UNLIKELY(offset % 512))
		{
			DEBUG("offset was not aligned to 512 (offset was %i)\n",
				offset);
			errno = EINVAL;
			return -1;
		}
		if(UNLIKELY(((long long int)buf & (512-1)) != 0))
		{
			DEBUG("buffer was not aligned to 512 (buffer was %p, "
				"mod 512=%i)\n", buf, (long long int)buf % 512);
			errno = EINVAL;
			return -1;
		}
	}

	ssize_t len_to_read = count;

	DEBUG("time for a Pread.  file length %li, offset %li, "
		"length-offset %li, count %li, count>offset %s\n",
		nvf->node->length, offset, available_length, count,
		(count>available_length) ? "true" : "false");

	if (count > available_length)
	{
		len_to_read = available_length;
		DEBUG("Request read length was %li, but only %li bytes "
			"available. (filelen = %li, offset = %li, "
			"requested %li)\n", count, len_to_read,
			nvf->node->length, offset, count);
	}

	if(UNLIKELY( (len_to_read <= 0) || (available_length <= 0) ))
	{
		NVP_END_TIMING(do_pread_t, do_pread_time);
		return 0; // reading 0 bytes is easy!
	}

	DEBUG("mmap is length %li, len_to_read is %li\n", nvf->node->maplength,
		len_to_read);

	SANITYCHECK(len_to_read + offset <= nvf->node->length);
//	SANITYCHECK(nvf->node->length < nvf->node->maplength);

	read_count = 0;
	read_offset = offset;

	while (len_to_read > 0) {
		NVP_START_TIMING(get_mmap_t, get_mmap_time);
		ret = nvp_get_mmap_address(nvf, read_offset, read_count,
					&mmap_addr, &extent_length, wr_lock,
					cpuid);
		NVP_END_TIMING(get_mmap_t, get_mmap_time);

		DEBUG("Pread: get_mmap_address returned %d, length %llu\n",
			ret, extent_length);

		switch (ret) {
 		case 0: // Mmaped. Do memcpy.
			break;
		case 1: // Not mmaped. Calling Posix pread.
			posix_read = _nvp_fileops->PREAD(file, buf,
					len_to_read, read_offset);
			if (read_offset + posix_read > nvf->node->length)
				nvf->node->length = read_offset + posix_read;
			read_count += posix_read;
			num_posix_read++;
			posix_read_size += posix_read;
			goto out;
		default:
			break;
		}

		if (extent_length > len_to_read)
			extent_length = len_to_read;

		NVP_START_TIMING(memcpyr_t, memcpyr_time);
		memcpy1(buf, (char *)mmap_addr, extent_length);
		NVP_END_TIMING(memcpyr_t, memcpyr_time);

		num_memcpy_read++;
		memcpy_read_size += extent_length;

		len_to_read -= extent_length;
		read_offset += extent_length;
		read_count  += extent_length;
		buf += extent_length;
	}

	DO_MSYNC(nvf);
out:
	NVP_END_TIMING(do_pread_t, do_pread_time);
	return read_count;
}


RETT_PWRITE _nvp_do_pwrite(INTF_PWRITE, int wr_lock, int cpuid)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	timing_type do_pwrite_time, memcpyw_time, get_mmap_time;
	NVP_START_TIMING(do_pwrite_t, do_pwrite_time);
	int ret;
	off_t write_offset;
	size_t write_count, extent_length;
	size_t posix_write;
	unsigned long mmap_addr = 0;

	DEBUG("_nvp_do_pwrite\n");

	_nvp_wr_total++;

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	SANITYCHECKNVF(nvf);
	
	if(UNLIKELY(!nvf->canWrite)) {
		DEBUG("FD not open for writing: %i\n", file);
		errno = EBADF;
		return -1;
	}

	if(nvf->aligned)
	{
		DEBUG("This write must be aligned.  Checking alignment.\n");
		if(UNLIKELY(count % 512))
		{
			DEBUG("count is not aligned to 512 (count was %li)\n",
				count);
			errno = EINVAL;
			return -1;
		}
		if(UNLIKELY(offset % 512))
		{
			DEBUG("offset was not aligned to 512 "
				"(offset was %li)\n", offset);
			errno = EINVAL;
			return -1;
		}
	//	if((long long int)buf % 512)
		if(UNLIKELY(((long long int)buf & (512-1)) != 0))
		{
			DEBUG("buffer was not aligned to 512 (buffer was %p, "
				"mod 512 = %li)\n", buf,
				(long long int)buf % 512);
			errno = EINVAL;
			return -1;
		}
	}

	if(nvf->append)
	{
		DEBUG("this fd (%i) is O_APPEND; setting offset from the "
			"passed value (%li) to the end of the file (%li) "
			"prior to writing anything\n", nvf->fd, offset,
			nvf->node->length);
		offset = nvf->node->length;
	}

	ssize_t extension = count + offset - (nvf->node->length) ;

	DEBUG("time for a Pwrite. file length %li, offset %li, extension %li, count %li\n", nvf->node->length, offset, extension, count);
	
	if(extension > 0)
	{
		_nvp_wr_extended++;

		DEBUG("Request write length %li will extend file. "
			"(filelen=%li, offset=%li, count=%li, extension=%li)\n",
			count, nvf->node->length, offset, count, extension);
		
		ssize_t temp_result;
		if(nvf->aligned) {
			DEBUG_P("(aligned): %s->PWRITE(%i, %p, %li, %li)\n",
				_nvp_fileops->name, nvf->fd, _nvp_zbuf, 512,
				count+offset-512);
		} else {
			DEBUG_P("(unaligned)\n");
		}	

		temp_result = _nvp_fileops->PWRITE(nvf->fd, buf, count, offset);
		num_posix_write++;
		posix_write_size += temp_result;
		DEBUG("Done extending NVFile, now let's extend mapping.\n");

		if( offset+count >= nvf->node->maplength )
		{
//			DEBUG("Request will also extend map.\n");
//			_nvp_extend_map(file, offset+count );
		} else {
			DEBUG("However, map is already large enough: "
				"%li > %li\n", nvf->node->maplength,
				offset + count);
			SANITYCHECK(nvf->node->maplength > (offset+count));
		}

		nvf->node->length += extension;
		return temp_result;
	}
	else
	{
		DEBUG("File will NOT be extended: count + offset < length "
			"(%li < %li)\n", count+offset, nvf->node->length);
	}

	DEBUG("Preforming "MK_STR(FSYNC_MEMCPY)"(%p (%p+%li), %p, %li)\n",
		nvf->node->data+offset, nvf->node->data, offset, buf, count);
	
	if(extension > 0)
	{
		DEBUG("maplen = %li > filelen after write (%li)\n",
			nvf->node->maplength, (nvf->node->length+extension));
		SANITYCHECK((nvf->node->length+extension)
				< nvf->node->maplength);
	}
	else
	{
		DEBUG("maplen = %li > filelen after write (%li)\n",
			nvf->node->maplength, nvf->node->length);
		SANITYCHECK((nvf->node->length) < nvf->node->maplength);
	}

	SANITYCHECK(nvf->valid);
	SANITYCHECK(nvf->node != NULL);
	SANITYCHECK(nvf->node->maplength > nvf->node->length + ((extension>0)?extension:0));
	SANITYCHECK(buf > 0);
	SANITYCHECK(count >= 0);

	ssize_t len_to_write = count;

	write_count = 0;
	write_offset = offset;

	while (len_to_write > 0) {
		NVP_START_TIMING(get_mmap_t, get_mmap_time);
		ret = nvp_get_mmap_address(nvf, write_offset, write_count,
				&mmap_addr, &extent_length, wr_lock, cpuid);
		NVP_END_TIMING(get_mmap_t, get_mmap_time);

		DEBUG("Pwrite: get_mmap_address returned %d, length %llu\n",
					ret, extent_length);

		switch (ret) {
 		case 0: // Mmaped. Do memcpy.
			break;
		case 1: // Not mmaped. Calling Posix pread.
			posix_write = _nvp_fileops->PWRITE(file, buf,
					len_to_write, write_offset);
			if (write_offset + posix_write > nvf->node->length)
				nvf->node->length = write_offset + posix_write;
			write_count += posix_write;
			num_posix_write++;
			posix_write_size += posix_write;
			goto out;
		default:
			break;
		}

		if (extent_length > len_to_write)
			extent_length = len_to_write;

		NVP_START_TIMING(memcpyw_t, memcpyw_time);
		FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length);
		NVP_END_TIMING(memcpyw_t, memcpyw_time);

		num_memcpy_write++;
		memcpy_write_size += extent_length;

		len_to_write -= extent_length;
		write_offset += extent_length;
		write_count  += extent_length;
		buf += extent_length;
	}

	//nvf->offset += count; // NOT IN PWRITE (this happens in write)

	DEBUG("About to return from _nvp_PWRITE with ret val %li.  file len: "
		"%li, file off: %li, map len: %li, node %p\n",
		count, nvf->node->length, nvf->offset,
		nvf->node->maplength, nvf->node);

	DO_MSYNC(nvf);

out:
	NVP_END_TIMING(do_pwrite_t, do_pwrite_time);
	return write_count;
}

void _nvp_test_invalidate_node(struct NVFile* nvf)
{
	struct NVNode* node = nvf->node;

	DEBUG("munmapping temporarily diabled...\n"); // TODO

	return;

	SANITYCHECK(node!=NULL);

	int do_munmap = 1;

	int i;
	for(i = 0; i < OPEN_MAX; i++)
	{
		if ((_nvp_fd_lookup[i].valid) && (node==_nvp_fd_lookup[i].node))
		{
			do_munmap = 0;
			break;
		}
	}

	if(do_munmap)
	{
		DEBUG("Node appears not to be in use anymore.  munmapping.\n");

		if(munmap(node->data, node->maplength))
		{
			ERROR("Coudln't munmap file! %s\n", strerror(errno));
			assert(0);
		}
		
//		pthread_rwlock_destroy(&node->lock);

		free(node);

		nvf->node = NULL; // we don't want anyone using this again
		
		DEBUG("munmap successful.\n");
	}
	else
	{
		DEBUG("Node appears to still be in use.  Not munmapping.\n");
	}
}

RETT_SEEK64 _nvp_do_seek64(INTF_SEEK64)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_do_seek64\n");

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	
	DEBUG("_nvp_do_seek64: file len %li, map len %li, current offset %li, "
		"requested offset %li with whence %li\n", 
		nvf->node->length, nvf->node->maplength, *nvf->offset,
		offset, whence);

	switch(whence)
	{
		case SEEK_SET:
			if(offset < 0)
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) = offset ;
			return *(nvf->offset);

		case SEEK_CUR:
			if((*(nvf->offset) + offset) < 0)
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) += offset ;
			return *(nvf->offset);

		case SEEK_END:
			if( nvf->node->length + offset < 0 )
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) = nvf->node->length + offset;
			return *(nvf->offset);

		default:
			DEBUG("Invalid whence parameter.\n");
			errno = EINVAL;
			return -1;
	}

	assert(0); // unreachable
	return -1;
}

/* ========================== POSIX API methods =========================== */

RETT_OPEN _nvp_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	timing_type open_time, posix_open_time;
	NVP_START_TIMING(open_t, open_time);

	if(path==NULL) {
		DEBUG("Invalid path.\n");
		errno = EINVAL;
		NVP_END_TIMING(open_t, open_time);
		return -1;
	}
	
	DEBUG("_nvp_OPEN(%s)\n", path);
	num_open++;
	
	DEBUG("Attempting to _nvp_OPEN the file \"%s\" with the following flags (0x%X): ", path, oflag);

	if((oflag&O_RDWR)||((oflag&O_RDONLY)&&(oflag&O_WRONLY))) {
		DEBUG_P("O_RDWR ");
	} else if(FLAGS_INCLUDE(oflag,O_WRONLY)) {
		DEBUG_P("O_WRONLY ");
	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG_P("O_RDONLY ");
	}
	DUMP_FLAGS(oflag,O_APPEND);
	DUMP_FLAGS(oflag,O_CREAT);
	DUMP_FLAGS(oflag,O_TRUNC);
	DUMP_FLAGS(oflag,O_EXCL);
	DUMP_FLAGS(oflag,O_SYNC);
	DUMP_FLAGS(oflag,O_ASYNC);
	DUMP_FLAGS(oflag,O_DSYNC);
	DUMP_FLAGS(oflag,O_FSYNC);
	DUMP_FLAGS(oflag,O_RSYNC);
	DUMP_FLAGS(oflag,O_NOCTTY);
	DUMP_FLAGS(oflag,O_NDELAY);
	DUMP_FLAGS(oflag,O_NONBLOCK);
	DUMP_FLAGS(oflag,O_DIRECTORY);
	DUMP_FLAGS(oflag,O_LARGEFILE);
	DUMP_FLAGS(oflag,O_NOATIME);
	DUMP_FLAGS(oflag,O_DIRECT);
	DUMP_FLAGS(oflag,O_NOFOLLOW);
	//DUMP_FLAGS(oflag,O_SHLOCK);
	//DUMP_FLAGS(oflag,O_EXLOCK);

	DEBUG_P("\n");

	struct stat file_st;

	if(access(path, F_OK)) // file doesn't exist
	{
		if(FLAGS_INCLUDE(oflag, O_CREAT))
		{
			DEBUG("File does not exist and is set to be created.\n");
		}
		else
		{
			DEBUG("File does not exist and is not set to be created.  returning\n");
			errno = ENOENT;
			NVP_END_TIMING(open_t, open_time);
			return -1;
		}
	}
	else
	{
		if(stat(path, &file_st))
		{
			DEBUG("File exists but failed to get file stats!\n");
			errno = EACCES;
			NVP_END_TIMING(open_t, open_time);
			return -1;
		}

		if(S_ISREG(file_st.st_mode))
		{
			DEBUG("File at path %s is a regular file, all is well.\n", path);
		}
		else
		{
			DEBUG("File at path %s is NOT a regular file!  INCONCEIVABLE\n", path);
			assert(S_ISREG(file_st.st_mode));
		}
	}

	int result;

	struct NVNode* node = NULL;

	if(stat(path, &file_st))
	{
		DEBUG("File didn't exist before calling open.\n");
	}
	else
	{
		DEBUG("File exists before we open it.  Let's get the lock first.\n");

		// Find or allocate a NVNode
		node = nvp_get_node(path, &file_st);
		
		NVP_LOCK_WR(node->lock);
	}

	NVP_START_TIMING(posix_open_t, posix_open_time);
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		result = _nvp_fileops->OPEN(path, oflag & (~O_APPEND), mode);
	} else {
		result = _nvp_fileops->OPEN(path, oflag & (~O_APPEND));
	}
	NVP_END_TIMING(posix_open_t, posix_open_time);

	if(result<0)
	{
		DEBUG("_nvp_OPEN->%s_OPEN failed: %s\n", _nvp_fileops->name, strerror(errno));
		NVP_END_TIMING(open_t, open_time);
		return result;
	}	

	SANITYCHECK(&_nvp_fd_lookup[result] != NULL);
	
	struct NVFile* nvf = &_nvp_fd_lookup[result];
	NVP_LOCK_FD_WR(nvf);

	DEBUG("_nvp_OPEN succeeded for path %s: fd %i returned.  filling in file info\n", path, result);

	if(_nvp_fd_lookup[result].valid)
	{
		ERROR("There is already a file open with that FD (%i)!\n", result);
		assert(0);
	}
	else
	{
		DEBUG("There was not already an NVFile for fd %i (that's good)\n", result);
	}

	SANITYCHECK(!access(path, F_OK)); // file exists
//	if(FLAGS_INCLUDE(oflag, O_RDONLY) || FLAGS_INCLUDE(oflag, O_RDWR)) { SANITYCHECK(!access(path, R_OK)); } else { DEBUG("Read not requested\n"); }
//	if(FLAGS_INCLUDE(oflag, O_WRONLY) || FLAGS_INCLUDE(oflag, O_RDWR)) { SANITYCHECK(!access(path, W_OK)); } else { DEBUG("Write not requested\n"); }

	if(stat(path, &file_st))
	{
		ERROR("Failed to stat opened file %s: %s\n", path, strerror(errno));
		assert(0);
	}
	else 
	{
		DEBUG("Stat successful for newly opened file %s (fd %i)\n", path, result);
	}

	if(node == NULL)
	{
		DEBUG("We created the file.  Let's check and make sure someone else hasn't already created the node.\n");

		// Find or allocate a NVNode
		node = nvp_get_node(path, &file_st);
		
		NVP_LOCK_WR(node->lock);
	}
	if(FLAGS_INCLUDE(oflag, O_TRUNC))
	{
		if(file_st.st_size != 0)
		{
			WARNING("O_TRUNC was set, but after %s->OPEN, file length was not 0!\n", _nvp_fileops->name);
			WARNING("This is probably the result of another thread modifying the underlying node before we could get a lock on it.\n");
			//assert(0);
		}
		else
		{
			DEBUG("O_TRUNC was set, and after %s->OPEN file length was 0 (as it should be).\n", _nvp_fileops->name);
		}
	}

	nvf->fd = result;
	
	nvf->serialno = file_st.st_ino;

	nvf->node = node;
	nvf->posix = 0;

	int index = nvf->serialno % 1024;
	if (_nvp_ino_lookup[index] == 0)
		_nvp_ino_lookup[index] = result;

	// Set FD permissions
	if((oflag&O_RDWR)||((oflag&O_RDONLY)&&(oflag&O_WRONLY))) {
		DEBUG("oflag (%i) specifies O_RDWR for fd %i\n", oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 1;
	} else if(oflag&O_WRONLY) {
		DEBUG("oflag (%i) specifies O_WRONLY for fd %i\n", oflag, result);
		#if 0
		oflag |= O_RDWR;
		nvf->canRead = 1;
		nvf->canWrite = 1;
		#else
		MSG("File %s is opened O_WRONLY.\n", path);
		MSG("Does not support mmap, use posix instead.\n");
		nvf->posix = 1;
		nvf->canRead = 0;
		nvf->canWrite = 1;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		NVP_END_TIMING(open_t, open_time);
		return nvf->fd;
		#endif
	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG("oflag (%i) specifies O_RDONLY for fd %i\n", oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 0;
	} else {
		DEBUG("File permissions don't include read or write!\n");
		nvf->canRead = 0;
		nvf->canWrite = 0;
		assert(0);
	}
	
	if(FLAGS_INCLUDE(oflag, O_APPEND)) {
		nvf->append = 1;
	} else {
		nvf->append = 0;
	}

	SANITYCHECK(nvf->node != NULL);
	SANITYCHECK(nvf->node->length >= 0);

	if(FLAGS_INCLUDE(oflag, O_TRUNC) && nvf->node->length)
	{
		DEBUG("We just opened a file with O_TRUNC that was already open with nonzero length %li.  Updating length.\n", nvf->node->length);
		nvf->node->length = 0;
	}
/*
	if(stat(path, &file_st)) // in case of multithreading it's good to update.  // TODO this is obviously a race condition...
	{
		ERROR("Failed to stat opened file %s: %s\n", path, strerror(errno));
		assert(0);
	}
*/
	SANITYCHECK(nvf->node->length == file_st.st_size);

	DEBUG("Meh, why not allocate a new map every time\n");
	//nvf->node->maplength = -1;
	nvf->posix = 0;
	nvf->debug = 0;

	/* This is a nasty workaround for FIO */
	if (path[0] == '/' && path[1] == 's'
			&& path[2] == 'y' && path[3] == 's') {
		nvf->posix = 1;
		MSG("A Posix Path: %s\n", path);
	}

	/* For BDB log file, workaround the fdsync issue */
	if (path[29] == 'l' && path[30] == 'o' && path[31] == 'g') {
		nvf->debug = 1;
//		MSG("A Posix Path: %s\n", path);
	}

	SANITYCHECK(nvf->node->length >= 0);
//	SANITYCHECK(nvf->node->maplength >= nvf->node->length);

	nvf->offset = (size_t*)calloc(1, sizeof(int));
	*nvf->offset = 0;

	if(FLAGS_INCLUDE(oflag, O_DIRECT) && (DO_ALIGNMENT_CHECKS)) {
		nvf->aligned = 1;
	} else {
		nvf->aligned = 0;
	}

	nvf->valid = 1;
	//nvf->node->valid = 1;
	
	DO_MSYNC(nvf);

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);

	errno = 0;
	NVP_END_TIMING(open_t, open_time);
	return nvf->fd;
}

RETT_CLOSE _nvp_CLOSE(INTF_CLOSE)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	timing_type close_time, posix_close_time;
	RETT_CLOSE result;
	NVP_START_TIMING(close_t, close_time);

	DEBUG("_nvp_CLOSE(%i)\n", file);
	num_close++;

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		nvf->valid = 0;
		nvf->posix = 0;
		nvf->node->reference--;
		if (nvf->node->reference == 0) {
			nvf->node->serialno = 0;
			int index = nvf->serialno % 1024;
			_nvp_ino_lookup[index] = 0;
		}
		nvf->serialno = 0;
//		nvf->node = NULL;
		DEBUG("Call posix CLOSE for fd %d\n", nvf->fd);
		NVP_START_TIMING(posix_close_t, posix_close_time);
		result = _nvp_fileops->CLOSE(CALL_CLOSE);
		NVP_END_TIMING(posix_close_t, posix_close_time);
		NVP_END_TIMING(close_t, close_time);
		return result;
	}

	pthread_spin_lock(&node_lookup_lock);
	//int iter;
	nvf->node->reference--;
	if (nvf->node->reference == 0)
		nvf->node->serialno = 0;
	pthread_spin_unlock(&node_lookup_lock);
//	nvf->node = NULL;

	//_nvp_test_invalidate_node(nvf);

	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_WR(nvf);

	nvf->valid = 0;
	if (nvf->node->reference == 0) {
		int index = nvf->serialno % 1024;
		_nvp_ino_lookup[index] = 0;
		DEBUG("Close Cleanup node for %d\n", file);
		nvp_cleanup_node(nvf->node, 0);
	}
	nvf->serialno = 0;

	NVP_START_TIMING(posix_close_t, posix_close_time);
	result = _nvp_fileops->CLOSE(CALL_CLOSE);
	NVP_END_TIMING(posix_close_t, posix_close_time);

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);
	NVP_END_TIMING(close_t, close_time);

	return result;
}

static ssize_t _nvp_check_read_size_valid(size_t count)
{ 
	if(count == 0)
	{
		DEBUG("Requested a read of 0 length.  No problem\n");
		return 0;
	}
	else if(count < 0)
	{
		DEBUG("Requested read of negative bytes (%li)\n", count);
		errno = EINVAL;
		return -1;
	}

	return count;
}

static ssize_t _nvp_check_write_size_valid(size_t count)
{
	if(count == 0)
	{
		DEBUG("Requested a write of 0 bytes.  No problem\n");
		return 0;
	}

	if(((signed long long int)count) < 0)
	{
		DEBUG("Requested a write of %li < 0 bytes.\n", (signed long long int)count);
		errno = EINVAL;
		return -1;
	}

	return count;
}

RETT_READ _nvp_READ(INTF_READ)
{
	DEBUG("_nvp_READ %d\n", file);
	timing_type read_time;
	RETT_READ result;
	NVP_START_TIMING(read_t, read_time);

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix READ for fd %d\n", nvf->fd);
		result = _nvp_fileops->READ(CALL_READ);
		NVP_END_TIMING(read_t, read_time);
		read_size += result;
		num_posix_read++;
		posix_read_size += result;
		return result;
	}

	result = _nvp_check_read_size_valid(length);
	if (result <= 0)
		return result;

	int cpuid = GET_CPUID();

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);

	NVP_LOCK_NODE_RD(nvf, cpuid);

	result = _nvp_do_pread(CALL_READ,
			__sync_fetch_and_add(nvf->offset, length), 0, cpuid);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	
	if(result == length)	{
		DEBUG("PREAD succeeded: extending offset from %li to %li\n", *nvf->offset - result, *nvf->offset);
	}
	else if (result <= 0){
		DEBUG("_nvp_READ: PREAD failed; not changing offset. (returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length);
	} else {
		DEBUG("_nvp_READ: PREAD failed; Not fully read. (returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length - result);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	NVP_END_TIMING(read_t, read_time);
	num_read++;
	read_size += result;

	return result;
}

RETT_WRITE _nvp_WRITE(INTF_WRITE)
{
	DEBUG("_nvp_WRITE %d\n", file);
	num_write++;
	timing_type write_time;
	RETT_WRITE result;
	NVP_START_TIMING(write_t, write_time);

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix WRITE for fd %d\n", nvf->fd);
		result = _nvp_fileops->WRITE(CALL_WRITE);
		NVP_END_TIMING(write_t, write_time);
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		return result;
	}

	//int iter;
	int cpuid = GET_CPUID();
	result = _nvp_check_write_size_valid(length);
	if (result <= 0)
		return result;

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid); //TODO

	result = _nvp_do_pwrite(CALL_WRITE,
			__sync_fetch_and_add(nvf->offset, length), 0, cpuid);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);

	if(result >= 0)
	{
		if(nvf->append)
		{
			size_t temp_offset = __sync_fetch_and_add(nvf->offset, 0);
			DEBUG("PWRITE succeeded and append == true.  Setting offset to end...\n"); 
			assert(_nvp_do_seek64(nvf->fd, 0, SEEK_END) != (RETT_SEEK64)-1);
			DEBUG("PWRITE: offset changed from %li to %li\n", temp_offset, *nvf->offset);
			temp_offset = 4; // touch temp_offset
		}
		else
		{
			DEBUG("PWRITE succeeded: extending offset from %li to %li\n", *nvf->offset - result, *nvf->offset);
//			*nvf->offset += result;
		}
	}
	else {
		DEBUG("_nvp_WRITE: PWRITE failed; not changing offset. (returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
	}

	DEBUG("About to return from _nvp_WRITE with ret val %i (errno %i).  file len: %li, file off: %li, map len: %li\n", result, errno, nvf->node->length, nvf->offset, nvf->node->maplength);

	DO_MSYNC(nvf);

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	NVP_END_TIMING(write_t, write_time);
	write_size += result;

	return result;
}

RETT_PREAD _nvp_PREAD(INTF_PREAD)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_PREAD %d\n", file);
	num_read++;
	timing_type read_time;
	RETT_PREAD result;
	NVP_START_TIMING(pread_t, read_time);

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix PREAD for fd %d\n", nvf->fd);
		result = _nvp_fileops->PREAD(CALL_PREAD);
		NVP_END_TIMING(pread_t, read_time);
		read_size += result;
		num_posix_read++;
		posix_read_size += result;
		return result;
	}

	result = _nvp_check_read_size_valid(count);
	if (result <= 0)
		return result;

	int cpuid = GET_CPUID();
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	result = _nvp_do_pread(CALL_PREAD, 0, cpuid);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

	NVP_END_TIMING(pread_t, read_time);
	read_size += result;

	return result;
}

RETT_PWRITE _nvp_PWRITE(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_PWRITE %d\n", file);
	num_write++;
	timing_type write_time;
	RETT_PWRITE result;
	NVP_START_TIMING(pwrite_t, write_time);

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix PWRITE for fd %d\n", nvf->fd);
		result = _nvp_fileops->PWRITE(CALL_PWRITE);
		NVP_END_TIMING(pwrite_t, write_time);
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		return result;
	}

	result = _nvp_check_write_size_valid(count);
	if (result <= 0)
		return result;
	
	int cpuid = GET_CPUID();
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);
	
	ssize_t available_length = (nvf->node->length) - offset;

	if(count > available_length) {
		DEBUG("Promoting PWRITE lock to WRLOCK\n");
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		NVP_LOCK_NODE_WR(nvf);
		
		result = _nvp_do_pwrite(CALL_PWRITE, 1, cpuid);

		NVP_UNLOCK_NODE_WR(nvf);
	}
	else {
		result = _nvp_do_pwrite(CALL_PWRITE, 0, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	NVP_END_TIMING(pwrite_t, write_time);
	write_size += result;

	return result;
}


RETT_SEEK _nvp_SEEK(INTF_SEEK)
{
	DEBUG("_nvp_SEEK\n");
	return _nvp_SEEK64(CALL_SEEK);
}

RETT_SEEK64 _nvp_SEEK64(INTF_SEEK64)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_SEEK64 %d\n", file);

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix SEEK64 for fd %d\n", nvf->fd);
		return _nvp_fileops->SEEK64(CALL_SEEK64);
	}

	int cpuid = GET_CPUID();
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	RETT_SEEK64 result =  _nvp_do_seek64(CALL_SEEK64);	

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_WR(nvf);

	return result;
}

RETT_TRUNC _nvp_TRUNC(INTF_TRUNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_TRUNC\n");

	return _nvp_TRUNC64(CALL_TRUNC);
}

RETT_TRUNC64 _nvp_TRUNC64(INTF_TRUNC64)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_TRUNC64\n");

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix TRUNC64 for fd %d\n", nvf->fd);
		return _nvp_fileops->TRUNC64(CALL_TRUNC64);
	}

	int cpuid = GET_CPUID();
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_WR(nvf);

	if(!nvf->canWrite) {
		DEBUG("FD not open for writing: %i\n", file);
	//	errno = EBADF;
		errno = EINVAL;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return -1;
	}

	if(length == nvf->node->length)
	{
		DEBUG("_nvp_TRUNC64: requested length was the same as old length (%li).\n",
			nvf->node->length);
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return 0;
	}

	DO_MSYNC(nvf);

//	assert(!munmap(nvf->node->data, nvf->node->maplength));

	int result = _nvp_fileops->TRUNC64(CALL_TRUNC64);

	if(result != 0)
	{
		ERROR("%s->TRUNC64 failed (returned %li, requested %li): %s\n", _nvp_fileops->name, result, length, strerror(errno));
		assert(0);
	}

	if(length > nvf->node->length)
	{
		MSG("TRUNC64 extended file from %li to %li\n", nvf->node->length, length);
	}
	else 
	{
		MSG("TRUNC64 shortened file from %li to %li\n", nvf->node->length, length);
	}

	nvf->node->length = length;

	DO_MSYNC(nvf);

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

RETT_READV _nvp_READV(INTF_READV)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_READV\n");

	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _nvp_READ(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_nvp_READV failed on iov %i\n", i);
		return -1;
	}

	return 0;
}

RETT_WRITEV _nvp_WRITEV(INTF_WRITEV)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_WRITEV\n");

	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _nvp_WRITE(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_nvp_WRITEV failed on iov %i\n", i);
		return -1;
	}

	return 0;
}

RETT_DUP _nvp_DUP(INTF_DUP)
{
	DEBUG("_nvp_DUP(" PFFS_DUP ")\n", CALL_DUP);

	//CHECK_RESOLVE_FILEOPS(_nvp_);
	if(file < 0) {
		return _nvp_fileops->DUP(CALL_DUP);
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	 
	//int iter;
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);	
	NVP_LOCK_NODE_WR(nvf); // TODO

	int result = _nvp_fileops->DUP(CALL_DUP);

	if(result < 0) 
	{
		DEBUG("Call to _nvp_DUP->%s->DUP failed: %s\n",
			_nvp_fileops->name, strerror(errno));
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		return result;
	}

	struct NVFile* nvf2 = &_nvp_fd_lookup[result];

	if (nvf->posix) {
		DEBUG("Call posix DUP for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		return result;
	}


	NVP_LOCK_FD_WR(nvf2);
	
	if(nvf2->valid) {
		ERROR("fd %i was already in use!\n", result);
		assert(!nvf2->valid);
	}
	else
	{
		//free(nvf2->offset); // TODO: free this iff it's not in use anymore to avoid memory leaks
	}
	
	nvf2->fd 	= result;
	nvf2->offset 	= nvf->offset;
	nvf2->canRead 	= nvf->canRead;
	nvf2->canWrite 	= nvf->canWrite;
	nvf2->append 	= nvf->append;
	nvf2->aligned   = nvf->aligned;
	nvf2->serialno 	= nvf->serialno;
	nvf2->node 	= nvf->node;
	nvf2->posix 	= nvf->posix;

	SANITYCHECK(nvf2->node != NULL);

	nvf2->valid 	= 1;

	DO_MSYNC(nvf);
	DO_MSYNC(nvf2);

	NVP_UNLOCK_NODE_WR(nvf); // nvf2->node->lock == nvf->node->lock since nvf and nvf2 share a node
	NVP_UNLOCK_FD_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf2);

	return nvf2->fd;
}

RETT_DUP2 _nvp_DUP2(INTF_DUP2)
{
	//CHECK_RESOLVE_FILEOPS(_nvp_);
	DEBUG("_nvp_DUP2(" PFFS_DUP2 ")\n", CALL_DUP2);
	
	if(file<0) {
		return _nvp_fileops->DUP(CALL_DUP);
	}

	if(fd2<0) {
		DEBUG("Invalid fd2\n");
		errno = EBADF;
		return -1;
	}

	if(file == fd2)
	{
		DEBUG("Input and output files were the same (%i)\n", file);
		return file;
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVFile* nvf2 = &_nvp_fd_lookup[fd2];

	if (nvf->posix) {
		DEBUG("Call posix DUP2 for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		int result = _nvp_fileops->DUP2(CALL_DUP2);
		nvf2->fd = result;
		return result;
	}

	//int iter;

	if(file > fd2)
	{
		NVP_LOCK_FD_WR(nvf);
		NVP_LOCK_FD_WR(nvf2);
	} else {
		NVP_LOCK_FD_WR(nvf2);
		NVP_LOCK_FD_WR(nvf);
	}

	if( (!nvf->valid)||(!nvf2->valid) ) {
		errno = EBADF;
		DEBUG("Invalid FD1 %i or FD2 %i\n", file, fd2);
//		NVP_UNLOCK_FD_WR(nvf);
//		NVP_UNLOCK_FD_WR(nvf2);
	}

	if(nvf->node == nvf2->node || !nvf2->node) {
		NVP_LOCK_NODE_WR(nvf);
	} else {
		if(nvf->node > nvf2->node) {
			NVP_LOCK_NODE_WR(nvf);
			NVP_LOCK_NODE_WR(nvf2);
		} else {
			NVP_LOCK_NODE_WR(nvf2);
			NVP_LOCK_NODE_WR(nvf);
		}
	}

	int result = _nvp_fileops->DUP2(CALL_DUP2);

	if(result < 0)
	{
		DEBUG("_nvp_DUP2 failed to %s->DUP2(%i, %i) (returned %i): %s\n", _nvp_fileops->name, file, fd2, result, strerror(errno));
		NVP_UNLOCK_NODE_WR(nvf);
		if(nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }
		NVP_UNLOCK_FD_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf2);
		return result;
	}
	else
	{
		//free(nvf2->offset); // TODO: free this iff it's not in use anymore to avoid memory leaks
	}

	nvf2->valid = 0;
	
	if(nvf2->node && nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }

	_nvp_test_invalidate_node(nvf2);

	if(result != fd2)
	{
		WARNING("result of _nvp_DUP2(%i, %i) didn't return the fd2 that was just closed.  Technically this doesn't violate POSIX, but I DON'T LIKE IT.  (Got %i, expected %i)\n",
			file, fd2, result, fd2);
		assert(0);

		NVP_UNLOCK_FD_WR(nvf2);

		nvf2 = &_nvp_fd_lookup[result];

		NVP_LOCK_FD_WR(nvf2);

		if(nvf2->valid)
		{
			DEBUG("%s->DUP2 returned a result which corresponds to an already open NVFile! dup2(%i, %i) returned %i\n", _nvp_fileops->name, file, fd2, result);
			assert(0);
		}
	}

	nvf2->fd = result;
	nvf2->offset = nvf->offset;
	nvf2->canRead = nvf->canRead;
	nvf2->canWrite = nvf->canWrite;
	nvf2->append = nvf->append;
	nvf2->aligned = nvf->aligned;
	nvf2->serialno = nvf->serialno;
	nvf2->node = nvf->node;
	nvf2->valid = nvf->valid;
	nvf2->posix = nvf->posix;

	SANITYCHECK(nvf2->node != NULL);
	SANITYCHECK(nvf2->valid);

	DEBUG("fd2 should now match fd1.  Testing to make sure this is true.\n");

	NVP_CHECK_NVF_VALID_WR(nvf2);

	DO_MSYNC(nvf);
	DO_MSYNC(nvf2);

	NVP_UNLOCK_NODE_WR(nvf); // nvf2 was already unlocked.  old nvf2 was not the same node, but new nvf2 shares a node with nvf1
	NVP_UNLOCK_FD_WR(nvf2);
	NVP_UNLOCK_FD_WR(nvf);
	
	return nvf2->fd;
}

RETT_IOCTL _nvp_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_IOCTL\n");

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _nvp_fileops->IOCTL(file, request, third);

	return result;
}

RETT_UNLINK _nvp_UNLINK(INTF_UNLINK)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_UNLINK\n");

	RETT_UNLINK result = _nvp_fileops->UNLINK(CALL_UNLINK);

	return result;
}

RETT_UNLINKAT _nvp_UNLINKAT(INTF_UNLINKAT)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_UNLINKAT\n");

	RETT_UNLINKAT result = _nvp_fileops->UNLINKAT(CALL_UNLINKAT);

	return result;
}

RETT_FSYNC _nvp_FSYNC(INTF_FSYNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_FSYNC result;
	timing_type fsync_time;

	NVP_START_TIMING(fsync_t, fsync_time);
	result = _nvp_fileops->FSYNC(CALL_FSYNC);
	NVP_END_TIMING(fsync_t, fsync_time);

	return result;
}

RETT_FDSYNC _nvp_FDSYNC(INTF_FDSYNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	RETT_FDSYNC result;
	timing_type fdsync_time;

	NVP_START_TIMING(fdsync_t, fdsync_time);
	if (nvf->debug)
		result = 0;
	else
		result = _nvp_fileops->FDSYNC(CALL_FDSYNC);
	NVP_END_TIMING(fdsync_t, fdsync_time);

	return result;
}

