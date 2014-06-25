// a module which repalces the standart POSIX functions with memory mapped equivalents

#include "fileops_bankshot2.h"
#include "../../bankshot2/kernel/bankshot2_cache.h"

//#include "my_memcpy_nocache.h"

// TODO: manual prefaulting sometimes segfaults
#define MANUAL_PREFAULT 0
#define MMAP_PREFAULT 1

#define DO_ALIGNMENT_CHECKS 0

struct timezone;
struct timeval;
int gettimeofday(struct timeval *tv, struct timezone *tz);

char *cache_path;
const char *bankshot2_ctrl_dev = "/dev/bankshot2Ctrl0";
int bankshot2_ctrl_fd;

#define NOSANITYCHECK 1
#if NOSANITYCHECK
	#define SANITYCHECK(x)
#else
	#define SANITYCHECK(x) if(UNLIKELY(!(x))) { ERROR("NVP_SANITY("#x") failed!\n"); exit(101); }
#endif


#define COUNT_EXTENDS 0
#if COUNT_EXTENDS
	volatile size_t _bankshot2_wr_extended;
	volatile size_t _bankshot2_wr_total;
#endif


#define NVP_DO_LOCKING 1

#define NVP_LOCK_FD_RD(nvf, cpuid)	NVP_LOCK_RD(	   nvf->lock, cpuid)
#define NVP_UNLOCK_FD_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->lock, cpuid)
#define NVP_LOCK_FD_WR(nvf)		NVP_LOCK_WR(	   nvf->lock)
#define NVP_UNLOCK_FD_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->lock)

#define NVP_LOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_RD(	   nvf->node->lock, cpuid)
#define NVP_UNLOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->node->lock, cpuid)
#define NVP_LOCK_NODE_WR(nvf)		NVP_LOCK_WR(	   nvf->node->lock)
#define NVP_UNLOCK_NODE_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->node->lock)


BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _bankshot2_, ALLOPS_WPAREN)

RETT_OPEN _bankshot2_OPEN(INTF_OPEN);
RETT_IOCTL _bankshot2_IOCTL(INTF_IOCTL);

int MMAP_PAGE_SIZE;

void* _bankshot2_zbuf; // holds all zeroes.  used for aligned file extending. TODO: does sharing this hurt performance?

pthread_spinlock_t node_lookup_lock;

struct NVFile* _bankshot2_fd_lookup;
struct NVNode* _bankshot2_node_lookup;

void _bankshot2_init2(void);
int _bankshot2_extend_map(struct NVFile *nvf, size_t newlen);
int _bankshot2_test_mmap(int file, size_t newlen);
void _bankshot2_SIGBUS_handler(int sig);
void _bankshot2_SIGSEGV_handler(int sig);
void _bankshot2_test_invalidate_node(struct NVFile* nvf);
void cache_write_back(struct NVFile *nvf);

RETT_PWRITE _bankshot2_do_pwrite(INTF_PWRITE, int wr_lock, int cpuid); // like PWRITE, but without locks (called by _bankshot2_WRITE)
RETT_PWRITE _bankshot2_do_pread (INTF_PREAD, int cpuid); // like PREAD , but without locks (called by _bankshot2_READ )
RETT_SEEK64 _bankshot2_do_seek64(INTF_SEEK64); // called by nvp_seek, nvp_seek64, nvp_write

#define DO_MSYNC(nvf) do{}while(0)
//	DEBUG("NOT doing a msync\n"); }while(0)
/*
	DEBUG("Doing a msync on fd %i (node %p)\n", nvf->fd, nvf->node); \
	if(msync(nvf->node->data, nvf->node->maplength, MS_SYNC|MS_INVALIDATE)) { \
		ERROR("Failed to msync for fd %i\n", nvf->fd); \
		assert(0); \
	} }while(0)
*/

int _bankshot2_lock_return_val;
int _bankshot2_write_pwrite_lock_handoff;

static sigjmp_buf pread_jumper;
static sigjmp_buf pwrite_jumper;

static int do_pread_memcpy;
static int do_pwrite_memcpy;

//void * mremap(void *old_address, size_t old_size, size_t new_size, int flags);

MODULE_REGISTRATION_F("bankshot2", _bankshot2_, _bankshot2_init2(); );

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


#define NVP_WRAP_HAS_FD(op) \
	RETT_##op _bankshot2_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_bankshot2_);\
		DEBUG("_bankshot2_"#op" is just wrapping %s->"#op"\n", _bankshot2_fileops->name); \
		if(UNLIKELY(file>=OPEN_MAX)) { DEBUG("file descriptor too large (%i > %i)\n", file, OPEN_MAX-1); errno = EBADF; return (RETT_##op) -1; } \
		if(UNLIKELY(file<0)) { DEBUG("file < 0 (file = %i).  return -1;\n", file); errno = EBADF; return (RETT_##op) -1; } \
		if(UNLIKELY(!_bankshot2_fd_lookup[file].valid)) { DEBUG("That file descriptor (%i) is invalid\n", file); errno = EBADF; return -1; } \
		DEBUG("_bankshot2_" #op " is calling %s->" #op "\n", _bankshot2_fileops->name); \
		return (RETT_##op) _bankshot2_fileops->op( CALL_##op ); \
	}

#define NVP_WRAP_NO_FD(op) \
	RETT_##op _bankshot2_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_bankshot2_);\
		DEBUG("_bankshot2_"#op" is just wrapping %s->"#op"\n", _bankshot2_fileops->name); \
		return _bankshot2_fileops->op( CALL_##op ); \
	}

#define NVP_WRAP_HAS_FD_IWRAP(r, data, elem) NVP_WRAP_HAS_FD(elem)
#define NVP_WRAP_NO_FD_IWRAP(r, data, elem) NVP_WRAP_NO_FD(elem)

BOOST_PP_SEQ_FOR_EACH(NVP_WRAP_HAS_FD_IWRAP, placeholder, (FSYNC) (FDSYNC) (ACCEPT))
BOOST_PP_SEQ_FOR_EACH(NVP_WRAP_NO_FD_IWRAP, placeholder, (PIPE) (FORK) (SOCKET))


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


//////////////////////////

// modifications to support different FSYNC policies
//#define MEMCPY memcpy
//define MEMCPY intel_memcpy
//#define MEMCPY (void*)copy_from_user_inatomic_nocache
//#define MEMCPY my_memcpy_nocache
#define MEMCPY mmx2_memcpy
//#define MEMCPY memcpy1
#define MMAP mmap
#define FSYNC fsync

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
	#define FSYNC_FSYNC fsync_fsync_flush_on_fsync(nvf)
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

// void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
// void *memcpy(void *dest, const void *src, size_t n);
// int fsync(int fd);

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

//////////////////////////

#define TIME_READ_MEMCPY 0
#if TIME_READ_MEMCPY
long long unsigned int total_memcpy_cycles = 0;
void report_memcpy_usec(void) { printf("Total memcpy time: %llu cycles: %f seconds\n", total_memcpy_cycles, ((float)(total_memcpy_cycles))/(2.27f*1024*1024*1024) ); }
#endif
void bankshot2_clear_mappings(void);

void bankshot2_setup_signal_handler(void)
{
	struct sigaction act, oact;
	act.sa_handler = _bankshot2_SIGSEGV_handler;
	act.sa_flags = SA_NODEFER;

	sigaction(SIGSEGV, &act, &oact);
}

void _bankshot2_init2(void)
{
	#if TIME_READ_MEMCPY
//	atexit(report_memcpy_usec);
	#endif
#if 0
	cache_path = getenv("CACHE_PATH");      
	if (!cache_path) {
		ERROR("Invalid cache path.\n");
		assert(0);
	}
#endif
	_bankshot2_write_pwrite_lock_handoff = 0;

	assert(!posix_memalign(((void**)&_bankshot2_zbuf), 4096, 4096));

	_bankshot2_fd_lookup = (struct NVFile *)
				calloc(OPEN_MAX, sizeof(struct NVFile));

	int i;
	for(i = 0; i < OPEN_MAX; i++) {
		_bankshot2_fd_lookup[i].valid = 0;
		NVP_LOCK_INIT(_bankshot2_fd_lookup[i].lock);
	}

	_bankshot2_node_lookup = (struct NVNode *)
				calloc(OPEN_MAX, sizeof(struct NVNode));

	for(i = 0; i < OPEN_MAX; i++) {
		_bankshot2_node_lookup[i].reference = 0;
		_bankshot2_node_lookup[i].num_extents = 0;
		NVP_LOCK_INIT(_bankshot2_node_lookup[i].lock);
	}

	pthread_spin_init(&node_lookup_lock, PTHREAD_PROCESS_SHARED);

	MMAP_PAGE_SIZE = getpagesize();
	SANITYCHECK(MMAP_PAGE_SIZE > 100);

	#if COUNT_EXTENDS
	_bankshot2_wr_extended = 0;
	_bankshot2_wr_total    = 0;
	#endif

	DEBUG("Installing SIGBUS handler.\n");
	signal(SIGBUS, _bankshot2_SIGBUS_handler);

	bankshot2_setup_signal_handler();
	//TODO
	/*
	#define GLIBC_LOC ""
	MSG("Importing memcpy from %s\n", GLIBC_LOC);
	*/

//	_bankshot2_debug_handoff();
#if 0
	bankshot2_ctrl_fd = _bankshot2_fileops->OPEN(bankshot2_ctrl_dev, O_RDWR);
	if (!bankshot2_ctrl_fd) {
		ERROR("Failed to open bankshot2 ctrl dev.\n");
		assert(0);
	}
#endif
	atexit(bankshot2_clear_mappings);
}

#if COUNT_EXTENDS
void _bankshot2_print_extend_stats(void) __attribute__ ((destructor));
void _bankshot2_print_extend_stats(void)
{
	FILE * pFile;
	pFile = fopen ("results.txt","w");
	fprintf(pFile, "extended: %li\ntotal: %li\nratio: %f\n", _bankshot2_wr_extended, _bankshot2_wr_total, ((float)_bankshot2_wr_extended)/(_bankshot2_wr_extended+_bankshot2_wr_total));
	fclose (pFile);
	DEBUG("NVP: writes which extended: %li\n", _bankshot2_wr_extended);
	DEBUG("NVP: total writes         : %li\n", _bankshot2_wr_total);
	//DEBUG("NVP: extended/total       : %f\n", ((float)_bankshot2_wr_extended)/(_bankshot2_wr_extended+_bankshot2_wr_total));
}
#endif

#if 0
static char* _bankshot2_get_cachefile_path(const char *path)
{
	char *path_to_cachefile;
	char *filename;
	int size = strlen(cache_path);

	filename = basename(path);
	path_to_cachefile = malloc(size + strlen(filename) + 2);
	if (!path_to_cachefile)
	{
		DEBUG("Failed to allocate cache file name\n");
		assert(0);
	}

	strcpy(path_to_cachefile, cache_path);
	if (path_to_cachefile[size - 1] == '/') {
		strcpy(path_to_cachefile + size, filename);
		path_to_cachefile[size + strlen(filename)] = '\0';
	} else {
		path_to_cachefile[size] = '/';
		strcpy(path_to_cachefile + size + 1, filename);
		path_to_cachefile[size + strlen(filename) + 1] = '\0';
	}

	return path_to_cachefile;
}
#endif

/* Get the cache file inode. We're holding NVF lock and node lock. */
static int _bankshot2_get_cache_inode(const char *path, int oflag, int mode,
					struct NVFile *nvf)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	if(path == NULL) {
		DEBUG("Invalid path.\n");
		errno = EINVAL;
		return -1;
	}
	
	DEBUG("_bankshot2_get_cache_inode for %s\n", path);

	int result;
	struct bankshot2_cache_data data;

	struct NVNode* node = nvf->node;

	if(!node) {
		ERROR("Node does not exist!\n");
		assert(0);
	}

	data.file = nvf->fd;
	data.cache_ino = 0;
	data.read = nvf->canRead ? 1 : 0;
	data.write = nvf->canWrite ? 1 : 0;

	DEBUG("Send IOCTL_GET_INODE request\n");
	result = _bankshot2_fileops->IOCTL(bankshot2_ctrl_fd,
					BANKSHOT2_IOCTL_GET_INODE, &data);

	if(result<0)
	{
		DEBUG("IOCTL_GET_INODE failed: %d\n", result);
		return result;
	}	

	DEBUG("_bankshot2_get_cache_inode succeeded for path %s: fd %llu returned.\n", path, data.cache_ino);

	node->maplength = 0;
	node->cache_serialno = data.cache_ino;
	node->cache_length = data.cache_file_size;

	if (node->num_extents == 0) {
		node->extent_tree = RB_ROOT;
		node->mmap_extent_tree = RB_ROOT;
	}

//	bankshot2_print_extent_tree(node);

	nvf->cache_serialno = data.cache_ino;
	DEBUG("Assign cache ino %d\n", nvf->cache_serialno);

/*
	nvf->node->maxPerms |= (nvf->canRead)?PROT_READ:0;

	if( (!FLAGS_INCLUDE(nvf->node->maxPerms, PROT_WRITE)) && nvf->canWrite)
	{
		DEBUG("Map didn't have write perms, but it's about to.  We're going to need a new map.\n");
		nvf->node->maxPerms |= PROT_WRITE;
		//_bankshot2_extend_map(nvf->fd, nvf->node->maplength+1); // currently set to get a new map every time
	}
*/
	SANITYCHECK(nvf->node->cache_length >= 0);

	if(FLAGS_INCLUDE(oflag, O_TRUNC) && nvf->node->cache_length)
	{
		DEBUG("We just opened a file with O_TRUNC that was already open with nonzero length %li.  Updating length.\n", nvf->node->length);
		nvf->node->cache_length = 0;
	}
/*
	if(stat(path, &file_st)) // in case of multithreading it's good to update.  // TODO this is obviously a race condition...
	{
		ERROR("Failed to stat opened file %s: %s\n", path, strerror(errno));
		assert(0);
	}
*/
//	SANITYCHECK(nvf->node->cache_length == data.cache_file_size);

//	DEBUG("Meh, why not allocate a new map every time\n");
	//nvf->node->maplength = -1;

//	if(nvf->node->maplength < nvf->node->length)
//	{
//		DEBUG("map was not already allocated (was %li).  Let's allocate one.\n", nvf->node->maplength);
//		_bankshot2_extend_map(nvf->fd, nvf->node->length);
	
		//if(_bankshot2_extend_map(nvf->fd, MAX(1, MAX(nvf->node->maplength+1, nvf->node->length))))
# if 0
	MSG("Try to mmap file %s, fd %d, cache inode %lu\n", path, nvf->fd, nvf->cache_serialno);
	if(_bankshot2_extend_map(nvf, MAX(1, nvf->node->cache_length)))
	{
		MSG("Failed to mmap cache fd %d. Don't cache it, just use Posix.\n");
		return -1;
	}

	SANITYCHECK(nvf->node->maplength > 0);
	SANITYCHECK(nvf->node->maplength > nvf->node->cache_length);

	if(nvf->node->data < 0) {
		ERROR("Failed to mmap path %s: %s\n", path, strerror(errno));
		assert(0);
	}

	MSG("mmap successful.  result: %p\n", nvf->node->data);
//	}
#endif

//	SANITYCHECK(nvf->node->cache_length >= 0);
//	SANITYCHECK(nvf->node->maplength > nvf->node->cache_length);

	DO_MSYNC(nvf);

	return nvf->cache_serialno;
}

void bankshot2_clear_mappings(void)
{
	struct NVFile *nvf;
	struct NVNode *node;
	int i;
	uint64_t cache_ino;

	for (i = 0; i < OPEN_MAX; i++) {
		nvf = &_bankshot2_fd_lookup[i];
		cache_ino = nvf->cache_serialno;
		if (cache_ino) {
			DEBUG("Clear cache fd %d mappings\n", cache_ino);
			_bankshot2_fileops->IOCTL(bankshot2_ctrl_fd,
				BANKSHOT2_IOCTL_REMOVE_MAPPING, &cache_ino);
		}
	}

	_bankshot2_fileops->CLOSE(bankshot2_ctrl_fd);

	free(_bankshot2_fd_lookup);

	pthread_spin_lock(&node_lookup_lock);

	for (i = 0; i < OPEN_MAX; i++) {
		node = &_bankshot2_node_lookup[i];
		bankshot2_cleanup_extent_tree(node);
	}

	pthread_spin_unlock(&node_lookup_lock);
	pthread_spin_destroy(&node_lookup_lock);

	free(_bankshot2_node_lookup);
}

struct NVNode * bankshot2_allocate_node(void)
{
	struct NVNode *node = NULL;
	int i;

	// Find a NVNode without backing file inode
	for (i = 0; i < OPEN_MAX; i++)
	{
		if(_bankshot2_node_lookup[i].serialno == 0) {
			node = &_bankshot2_node_lookup[i];
			DEBUG("Allocating unused NVNode %d\n", i);
			bankshot2_cleanup_extent_tree(node);
			break;
		}
	}

	if (node)
		return node;

	// Find a NVNode without references
	for (i = 0; i < OPEN_MAX; i++)
	{
		if(_bankshot2_node_lookup[i].reference == 0) {
			node = &_bankshot2_node_lookup[i];
			DEBUG("Allocating unreferenced NVNode %d\n", i);
			bankshot2_cleanup_extent_tree(node);
			break;
		}
	}

	return node;
}

struct NVNode * bankshot2_get_node(const char *path, struct stat *file_st)
{
	struct NVNode *node = NULL;
	int i;

	pthread_spin_lock(&node_lookup_lock);
	for (i = 0; i < OPEN_MAX; i++)
	{
		if(_bankshot2_node_lookup[i].serialno == file_st->st_ino) {
			DEBUG("File %s is (or was) already open in fd %i "
				"(this fd hasn't been __open'ed yet)! "
				"Sharing nodes.\n", path, i);
			node = &_bankshot2_node_lookup[i];
			break;
		}
	}

	if(!node) {
		DEBUG("File %s is not already open.  Allocating new NVNode.\n",
			path);
		node = bankshot2_allocate_node();
		assert(node);
		node->length = file_st->st_size;
		node->maplength = 0;
		node->serialno = file_st->st_ino;
		DEBUG("File %s st_ino %llu\n", path, node->serialno);
		node->num_extents = 0;
	}

	node->reference++;

	pthread_spin_unlock(&node_lookup_lock);
	return node;
}

RETT_OPEN _bankshot2_OPEN(INTF_OPEN)
{
	int mode = 0;

	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	if (path == NULL) {
		DEBUG("Invalid path.\n");
		errno = EINVAL;
		return -1;
	}
	
	DEBUG("\n_bankshot2_OPEN(%s)\n", path);

	if (!bankshot2_ctrl_fd) {
		bankshot2_ctrl_fd = _bankshot2_fileops->OPEN(bankshot2_ctrl_dev,
								O_RDWR);
		if (!bankshot2_ctrl_fd) {
			ERROR("Failed to open bankshot2 ctrl dev.\n");
			assert(0);
		}
	}
	DEBUG("Attempting to _bankshot2_OPEN the file \"%s\" with the following flags (0x%X): ", path, oflag);

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
			return -1;
		}
	}
	else
	{
		if(stat(path, &file_st))
		{
			DEBUG("File exists but failed to get file stats!\n");
			errno = EACCES;
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
		node = bankshot2_get_node(path, &file_st);
		NVP_LOCK_WR(node->lock);
	}

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		mode = va_arg(arg, int);
		va_end(arg);
		result = _bankshot2_fileops->OPEN(path, oflag & (~O_APPEND), mode);
	} else {
		result = _bankshot2_fileops->OPEN(path, oflag & (~O_APPEND));
	}

	if(result<0)
	{
		DEBUG("_bankshot2_OPEN->%s_OPEN failed: %s\n", _bankshot2_fileops->name, strerror(errno));
		return result;
	}	

	SANITYCHECK(&_bankshot2_fd_lookup[result] != NULL);
	
	struct NVFile* nvf = &_bankshot2_fd_lookup[result];
	NVP_LOCK_FD_WR(nvf);

	DEBUG("_bankshot2_OPEN succeeded for path %s: fd %i returned.  filling in file info\n", path, result);

	if(_bankshot2_fd_lookup[result].valid)
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
		node = bankshot2_get_node(path, &file_st);
		NVP_LOCK_WR(node->lock);
	}
	if(FLAGS_INCLUDE(oflag, O_TRUNC))
	{
		if(file_st.st_size != 0)
		{
			WARNING("O_TRUNC was set, but after %s->OPEN, file length was not 0!\n", _bankshot2_fileops->name);
			WARNING("This is probably the result of another thread modifying the underlying node before we could get a lock on it.\n");
			//assert(0);
		}
		else
		{
			DEBUG("O_TRUNC was set, and after %s->OPEN file length was 0 (as it should be).\n", _bankshot2_fileops->name);
		}
	}

	nvf->fd = result;
	
	nvf->serialno = file_st.st_ino;

	nvf->node = node;

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
		MSG("Does not support mmap, we will try a cache file with O_RDWR.\n");
//		nvf->posix = 1;
		nvf->canRead = 0;
		nvf->canWrite = 1;
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

/*
	nvf->node->maxPerms |= (nvf->canRead)?PROT_READ:0;

	if( (!FLAGS_INCLUDE(nvf->node->maxPerms, PROT_WRITE)) && nvf->canWrite)
	{
		DEBUG("Map didn't have write perms, but it's about to.  We're going to need a new map.\n");
		nvf->node->maxPerms |= PROT_WRITE;
		//_bankshot2_extend_map(nvf->fd, nvf->node->maplength+1); // currently set to get a new map every time
	}
*/
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

//	path_to_cachefile = _bankshot2_get_cachefile_path(path);
//	MSG("Cache filename: %s\n", path_to_cachefile);

//	if(nvf->node->maplength < nvf->node->length)
//	{
//		DEBUG("map was not already allocated (was %li).  Let's allocate one.\n", nvf->node->maplength);
//		_bankshot2_extend_map(nvf->fd, nvf->node->length);
	
		//if(_bankshot2_extend_map(nvf->fd, MAX(1, MAX(nvf->node->maplength+1, nvf->node->length))))
//	if(_bankshot2_extend_map(nvf->fd, MAX(1, nvf->node->length)))
#if 0
	if(skip_mmap_test == 0 && _bankshot2_test_mmap(nvf->fd, MAX(1, nvf->node->length)))
	{
		MSG("Failed to mmap fd %d. Don't cache it, just use Posix.\n");
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		free(path_to_cachefile);
		nvf->posix = 1;
		return nvf->fd;
	}
#endif

	/* This is a nasty workaround for FIO */
	if (path[0] == '/' && path[1] == 's'
			&& path[2] == 'y' && path[3] == 's') {
		nvf->posix = 1;
		MSG("A Posix Path: %s\n", path);
	}

	if ((nvf->posix == 0) && (_bankshot2_get_cache_inode(path, oflag,
						mode, nvf) < 0)) {
		ERROR("Get Cache Inode for %s failed!\n", path);
		assert(0);
	}

	SANITYCHECK(nvf->node->maplength > 0);
	SANITYCHECK(nvf->node->maplength > nvf->node->length);

	if(nvf->node->data < 0) {
		ERROR("Failed to mmap path %s: %s\n", path, strerror(errno));
		assert(0);
	}

//	DEBUG("mmap successful.  result: %p\n", nvf->node->data);
//	}

	SANITYCHECK(nvf->node->length >= 0);
	SANITYCHECK(nvf->node->maplength > nvf->node->length);

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
//	free(path_to_cachefile);
	return nvf->fd;
}

RETT_CLOSE _bankshot2_CLOSE(INTF_CLOSE)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_CLOSE(%i)\n", file);

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	if (nvf->posix) {
		nvf->valid = 0;
		nvf->posix = 0;
		nvf->node->reference--;
		DEBUG("Call posix CLOSE for fd %d\n", nvf->fd);
		return _bankshot2_fileops->CLOSE(CALL_CLOSE);
	}

	//int iter;
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_WR(nvf);

//	cache_write_back(nvf);
	nvf->valid = 0;
	nvf->node->reference--;
	DEBUG("fd %d, cache ino %d\n", nvf->fd, nvf->cache_serialno);

	//_bankshot2_test_invalidate_node(nvf);

//	_bankshot2_fileops->CLOSE(nvf->cache_fd);
	RETT_CLOSE result = _bankshot2_fileops->CLOSE(CALL_CLOSE);

//	DEBUG("Send IOCTL_REMOVE_MAPPING request\n");
//	uint64_t cache_ino = nvf->cache_serialno;
//	result = _bankshot2_fileops->IOCTL(bankshot2_ctrl_fd,
//				BANKSHOT2_IOCTL_REMOVE_MAPPING, &cache_ino);

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);
	DEBUG("_bankshot2_CLOSE(%i) finished\n\n", file);

	return result;
}

static ssize_t _bankshot2_check_read_size_valid(size_t count)
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

static ssize_t _bankshot2_check_write_size_valid(size_t count)
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

int integrity_check(const char *buf, char *buf1, size_t length)
{
	int i = 0;
	int count = 0;
	int start = -1;
	int end = 0;

	while (i < length) {
		if (buf[i] != buf1[i]) {
			count++;
			if (start == -1)
				start = i;
			end = i;
		}
		i++;
	}

	if (count)
		DEBUG("ERROR: %d errors in length %lu, from %d to %d\n",
			count, length, start, end);
	else
		DEBUG("Correct: %d %lu\n", i, length);

	return count;
}

void integrity_test_extent(struct NVFile *nvf, uint64_t mmap_offset,
		size_t length, unsigned long mmap_addr)
{
	int i = 0;
	int count = 0;
	int start = -1;
	int end = 0;
	char * buf1, *buf;

	buf = malloc(length);
	memset(buf, '0', length);

	buf1 = malloc(length);
	memset(buf1, '0', length);

	_bankshot2_fileops->PREAD(nvf->fd, buf, length, mmap_offset);

	memcpy(buf1, (char *)mmap_addr, length);

	while (i < length) {
		if (buf[i] != buf1[i]) {
			count++;
			if (start == -1)
				start = i;
			end = i;
		}
		i++;
	}

	if (count)
		MSG("Extent ERROR: %d errors in length %lu, from %d to %d\n",
			count, length, start, end);
	else
		DEBUG("Extent Correct: offset 0x%llx(%llu), %d %lu\n",
			mmap_offset, mmap_offset, i, length);
}

RETT_READ _bankshot2_READ(INTF_READ)
{
	DEBUG("_bankshot2_READ %d\n", file);

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	
	if (nvf->posix) {
		DEBUG("Call posix READ for fd %d\n", nvf->fd);
		return _bankshot2_fileops->READ(CALL_READ);
	}

	int cpuid = GET_CPUID();

	RETT_READ result = _bankshot2_check_read_size_valid(length);
	if (result <= 0)
		return result;

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);

	NVP_LOCK_NODE_RD(nvf, cpuid);

	result = _bankshot2_do_pread(CALL_READ, __sync_fetch_and_add(nvf->offset, length), cpuid);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	
	if(result == length) {
		DEBUG("PREAD succeeded: extending offset from %li to %li\n", *nvf->offset - result, *nvf->offset);
	}
	else if (result <= 0){
		DEBUG("_bankshot2_READ: PREAD failed; not changing offset. (returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length);
	} else {
		DEBUG("_bankshot2_READ: PREAD failed; Not fully read. (returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length - result);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

#if INTEGRITY_CHECK
	char * buf1;
	int error;

	buf1 = malloc(length);
	memset(buf1, '0', length);
	_bankshot2_fileops->READ(file, buf1, length);
	error = integrity_check(buf, buf1, result);
	if (error)
		MSG("Read: fd %lu, cache ino %llu, offset %llu, length %lu, "
			"%d errors\n", nvf->fd, nvf->cache_serialno,
			*nvf->offset - result, length, error);
	DEBUG("offset: %lu\n", *nvf->offset);
	free(buf1);
#endif

	return result;
}

RETT_WRITE _bankshot2_WRITE(INTF_WRITE)
{
	DEBUG("_bankshot2_WRITE %d\n", file);

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix WRITE for fd %d\n", nvf->fd);
		return _bankshot2_fileops->WRITE(CALL_WRITE);
	}

	//int iter;
	int cpuid = GET_CPUID();

	RETT_WRITE result = _bankshot2_check_write_size_valid(length);
	if (result <= 0)
		return result;

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid); //TODO

	result = _bankshot2_do_pwrite(CALL_WRITE, __sync_fetch_and_add(nvf->offset, length), 0, cpuid);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);

	if(result >= 0)
	{
		if(nvf->append)
		{
			size_t temp_offset = __sync_fetch_and_add(nvf->offset, 0);
			DEBUG("PWRITE succeeded and append == true.  Setting offset to end...\n"); 
			assert(_bankshot2_do_seek64(nvf->fd, 0, SEEK_END) != (RETT_SEEK64)-1);
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
		DEBUG("_bankshot2_WRITE: PWRITE failed; not changing offset. (returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
	}

	DEBUG("About to return from _bankshot2_WRITE with ret val %i (errno %i).  file len: %li, file off: %li, map len: %li\n", result, errno, nvf->node->length, nvf->offset, nvf->node->maplength);

	DO_MSYNC(nvf);

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

RETT_PREAD _bankshot2_PREAD(INTF_PREAD)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_PREAD %d\n", file);

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix PREAD for fd %d\n", nvf->fd);
		return _bankshot2_fileops->PREAD(CALL_PREAD);
	}

	RETT_PREAD result = _bankshot2_check_read_size_valid(count);
	if (result <= 0)
		return result;

	int cpuid = GET_CPUID();

	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	result = _bankshot2_do_pread(CALL_PREAD, cpuid);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

#if INTEGRITY_CHECK
	char * buf1;
	int error;

	buf1 = malloc(count);
	memset(buf1, '0', count);
	_bankshot2_fileops->PREAD(file, buf1, count, offset);
	error = integrity_check(buf, buf1, result);
	if (error)
		MSG("Pread: fd %lu, cache ino %llu, offset %llu, length %lu, "
			"%d errors\n", nvf->fd, nvf->cache_serialno, offset,
			count, error);
	free(buf1);
#endif

	return result;
}

RETT_PWRITE _bankshot2_PWRITE(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_PWRITE %d\n", file);

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix PWRITE for fd %d\n", nvf->fd);
		return _bankshot2_fileops->PWRITE(CALL_PWRITE);
	}

	RETT_PWRITE result = _bankshot2_check_write_size_valid(count);
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
		
		result = _bankshot2_do_pwrite(CALL_PWRITE, 1, cpuid);

		NVP_UNLOCK_NODE_WR(nvf);
	}
	else {
		result = _bankshot2_do_pwrite(CALL_PWRITE, 0, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

int copy_to_cache(struct NVFile *nvf, struct bankshot2_cache_data *data)
{
//	ssize_t extension = count + offset - (nvf->node->cache_length) ;
	int result;

	DEBUG("copy_to_cache: %s, cache inode %d, offset %li, size %li\n",
			(data->rnw == READ_EXTENT) ? "read" : "write",
			data->cache_ino, data->offset, data->size);

	result = _bankshot2_fileops->IOCTL(bankshot2_ctrl_fd,
					BANKSHOT2_IOCTL_CACHE_DATA, data);
	if (result < 0) {
		ERROR("ioctl cache data read failed %d\n", result);
		assert(0);
	}

	// Doesn't need memcpy; it's done in kernel space

//	_bankshot2_fileops->PREAD(nvf->fd, buf, count, offset);
//	FSYNC_MEMCPY(nvf->node->data + offset, buf, count);
//	*mmap_addr = data.mmap_addr;

	return result;
}

void copy_from_cache(struct NVFile *nvf, off_t offset, size_t count,
				unsigned long mmap_addr)
{
	struct bankshot2_cache_data data;
	int result;

	MSG("%s: cache inode %lu, offset %li, size %li\n", __func__, nvf->cache_serialno, offset, count);
	
	data.file = nvf->fd;
	data.offset = offset;
	data.size = count;
//	data.buf = buf; FIXME
	data.cache_ino = nvf->cache_serialno;
	data.rnw = WRITE_EXTENT;
	data.read = (data.rnw == READ_EXTENT);
	data.write = (data.rnw == WRITE_EXTENT);

	result = _bankshot2_fileops->IOCTL(bankshot2_ctrl_fd,
					BANKSHOT2_IOCTL_CACHE_DATA, &data);
	if (result < 0) {
		ERROR("ioctl cache data write failed %d\n", result);
		assert(0);
	}

//	_bankshot2_fileops->PWRITE(nvf->fd, nvf->node->data, count, offset);

}

inline void cache_write_back_extent(struct NVFile *nvf, off_t offset, size_t count,
				unsigned long mmap_addr)
{
	_bankshot2_fileops->PWRITE(nvf->fd, (char *)mmap_addr, count, offset);
}

/* Write back the cache file to original file. */
/* Write locks of NVFile and Node must be held. */
void cache_write_back(struct NVFile *nvf)
{
	off_t write_offset;
	size_t write_count;
	int dirty;
	unsigned long mmap_addr;

	MSG("%s: write back cache fd %d to fd %d\n", __func__,
						nvf->fd, nvf->fd);

	while (first_extent(nvf, &write_offset, &write_count, &dirty,
			&mmap_addr) == 1)
	{
		DEBUG("extent: dirty %d, offset %lu, count %d, mmap addr %lx\n",
			dirty, write_offset, write_count, mmap_addr);
//			*(char *)mmap_addr);
		if (dirty)
			cache_write_back_extent(nvf, write_offset, write_count,
						mmap_addr);
		remove_extent(nvf, write_offset);
	}
}

inline void bankshot2_update_file_length(struct NVFile *nvf, size_t file_length)
{
	//FIXME: need write lock here
	DEBUG("Update file %d length to %lu\n", nvf->fd, file_length);
	nvf->node->length = file_length;
}
/*
 * Locates an extent in the file which contains the given offset
 * nvf and node read lock must be held
 */
int bankshot2_get_extent(struct NVFile *nvf, off_t offset,
				size_t *extent_length, unsigned long *mmap_addr,
				size_t *file_length, int rnw, char *buf,
				int wr_lock, int cpuid)
{
	int ret, feret;
	off_t cached_extent_offset;
	size_t cached_extent_length;
	struct bankshot2_cache_data data;
	size_t request_len = *file_length;
//	struct timespec start, end;
	
	unsigned long cached_extent_start;

	cached_extent_offset = offset;

	feret = find_extent(nvf, &cached_extent_offset, &cached_extent_length,
					&cached_extent_start);
	*file_length = nvf->node->length;

	if (feret == 1) {
		*mmap_addr = cached_extent_start +
				(offset - cached_extent_offset);
		*extent_length = cached_extent_length -
				(offset - cached_extent_offset);
		return 0;
	}

	memset(&data, 0, sizeof(struct bankshot2_cache_data));
	data.file = nvf->fd;
	data.offset = offset;
	data.file_length = *file_length;
	data.size = request_len;
	data.buf = buf;
	data.cache_ino = nvf->cache_serialno;
	data.rnw = rnw;
	data.read = (data.rnw == READ_EXTENT);
	data.write = (data.rnw == WRITE_EXTENT);

	DEBUG("Send ioctl request to kernel: fd %d, cache fd %llu, "
		"offset %llu, size %llu, length %llu\n",
		data.file, data.cache_ino, data.offset, data.size,
		data.file_length);

//	data.map_length = request_len > MAP_UNIT ? request_len : MAP_UNIT;

# if 0
		if (ret == 2) {
			// Copy the covered part first
			covered_size = read_count - (offset - read_offset);
			memcpy1(buf, (char *)(mmap_addr + offset - read_offset), covered_size);
			buf += covered_size;
			read_offset += read_count;
			read_count = len_to_read - covered_size;
		}
#endif

//	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = copy_to_cache(nvf, &data);
//	clock_gettime(CLOCK_MONOTONIC, &end);
//	printf("copy to cache time: %lu\n", end.tv_nsec - start.tv_nsec);
	DEBUG("copy_to_cache return %d, offset %llu, start %llu, length %llu\n",
		ret, data.extent_start_file_offset, data.extent_start,
		data.extent_length);

	if (ret == 0) {
		if (data.mmap_length) {
			// Acquire node write lock for add_extent
//			clock_gettime(CLOCK_MONOTONIC, &start);
			if (!wr_lock) {
				NVP_UNLOCK_NODE_RD(nvf, cpuid);
				NVP_LOCK_NODE_WR(nvf);
			}
			DEBUG("Add extent: cache fd %llu, start offset 0x%llx, "
				"mmap_addr 0x%llx, length %llu, required %lu\n",
				nvf->cache_serialno, data.mmap_offset,
				data.mmap_addr,	data.mmap_length,
				data.required);
			add_extent(nvf, data.mmap_offset,
				data.mmap_length, data.write, data.mmap_addr);
#if INTEGRITY_CHECK
			integrity_test_extent(nvf, data.mmap_offset,
				data.mmap_length, data.mmap_addr);
#endif
			if (!wr_lock) {
				NVP_UNLOCK_NODE_WR(nvf);
				NVP_LOCK_NODE_RD(nvf, cpuid);
			}
//			clock_gettime(CLOCK_MONOTONIC, &end);
//			printf("Add extent time: %lu\n", end.tv_nsec - start.tv_nsec);
		}
		bankshot2_update_file_length(nvf, data.file_length);
		ret = 5;
	} else if (ret == EOF_OR_HOLE) {
		if ((data.extent_start == (uint64_t)(-512) && data.extent_length == (uint64_t)(-512))
				|| data.file_length == 0) {
			*file_length = data.file_length;
			DEBUG("Found EOF\n");
			return 3;
		} else if (data.extent_start == (uint64_t)(-512)) {
			*extent_length = data.extent_length - (data.extent_start_file_offset - offset);
			*file_length = data.file_length;
			DEBUG("Found Hole @ EOF\n");
			return 4;
		} else if (data.extent_start_file_offset > offset) {
			*extent_length = data.extent_start_file_offset - offset;
			*file_length = data.file_length;
			DEBUG("Found Hole\n");
			return 2;
		}
	} else {
		ERROR("copy_to_cache returned %d\n", ret);
		assert(0);
	}

//	*mmap_addr = data.mmap_addr + (offset - data.mmap_offset);
	*extent_length = data.actual_length - (offset - data.actual_offset);
	/* Check if the actual transferred extent covers the required extent */
	DEBUG("Transferred extent: require offset %llu, actual_offset %llu, "
		"actual_length %llu, extent length %llu\n",
		offset, data.actual_offset, data.actual_length,
		*extent_length);
	if ((data.actual_offset > offset) || (data.actual_length +
			data.actual_offset) <= offset) {
		ERROR("Transferred extent does not cover request extent:\n"
			"Request offset 0x%llx, length %lu;\n"
			"actual offset 0x%llx, length %lu\n",
			offset, request_len,
			data.actual_offset, data.actual_length);
	}

	*file_length = data.file_length;

	if (*extent_length <= 0) {
		ERROR("Return extent_length <= 0\n");
		assert(0);
	}

	return ret;
}
/* Read lock of nvf and node are held */
RETT_PREAD _bankshot2_do_pread(INTF_PREAD, int cpuid)
{
	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	SANITYCHECKNVF(nvf);
	int ret = 0;
	int segfault;
	off_t read_offset;
	size_t read_count, extent_length;
	size_t file_length;
	unsigned long mmap_addr = 0;
	
//	struct timespec start, end;

	ssize_t available_length = (nvf->node->length) - offset;

	if(UNLIKELY(!nvf->canRead)) {
		DEBUG("FD not open for reading: %i\n", file);
		errno = EBADF;
		return -1;
	}

	else if(UNLIKELY(offset < 0))
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
			DEBUG("Actually there weren't any bytes available to read.  Bye! (length %li, offset %li, available_length %li)\n", nvf->node->length, offset, available_length);
			return 0;
		}

		if(UNLIKELY(count % 512))
		{
			DEBUG("cout is not aligned to 512 (count was %i)\n", count);
			errno = EINVAL;
			return -1;
		}
		if(UNLIKELY(offset % 512))
		{
			DEBUG("offset was not aligned to 512 (offset was %i)\n", offset);
			errno = EINVAL;
			return -1;
		}
	//	if((long long int)buf % 512)
		if(UNLIKELY(((long long int)buf & (512-1)) != 0))
		{
			DEBUG("buffer was not aligned to 512 (buffer was %p, mod 512=%i)\n", buf, (long long int)buf % 512);
			errno = EINVAL;
			return -1;
		}
	}

	ssize_t len_to_read = count;

	DEBUG("time for a Pread.  file length %li, offset %li, length-offset %li, count %li, count>offset %s\n", nvf->node->length, offset, available_length, count, (count>available_length)?"true":"false");

	if(count > available_length )
	{
		len_to_read = available_length;
		DEBUG("Request read length was %li, but only %li bytes available. (filelen=%li, offset=%li, requested %li)\n", count, len_to_read, nvf->node->length, offset, count);
	}

	if(UNLIKELY( (len_to_read <= 0) || (available_length <= 0) ))
	{
		return 0; // reading 0 bytes is easy!
	}

	SANITYCHECK(len_to_read + offset <= nvf->node->length);

	/* If request extent not in cache, we need to read it from backing store and copy to cache */
	read_count = 0;
	read_offset = offset;

//	if (len_to_read + read_offset > nvf->node->length)
//		len_to_read = nvf->node->length - read_offset;

	while(len_to_read > 0) {
		DEBUG("Pread: looking for extent offset %d, size %d\n",
			read_offset, len_to_read);
		file_length = len_to_read;
//try_again:
//		clock_gettime(CLOCK_MONOTONIC, &start);
		ret = bankshot2_get_extent(nvf, read_offset, &extent_length,
			&mmap_addr, &file_length, READ_EXTENT, buf, 0, cpuid);
//		clock_gettime(CLOCK_MONOTONIC, &end);
//		printf("get extent time: %lu\n", end.tv_nsec - start.tv_nsec);
		DEBUG("Pread: get_extent returned %d, length %llu\n",
			ret, extent_length);

		switch (ret) {
		case 0:	// It's cached. Do memcpy.
			break;
		case 1:	// We have some big troubles.
			return read_count;
		case 2:
		case 4: // File hole, return zeros.
			if (extent_length > len_to_read)
				extent_length = len_to_read;
			DEBUG("File hole. fill with memset. Offset: %.16llx, len: %llu\n",
					read_offset, extent_length);
			memset(buf, 0, extent_length);
			goto update_length;
		case 3: // EOF. read only correct amount.
			if (read_offset + len_to_read > file_length) {
				DEBUG("Near file end. Using read call\n");
				ssize_t posix_read;

				posix_read = _bankshot2_fileops->PREAD(file,
					buf, len_to_read, read_offset);
				if (read_offset + posix_read > file_length)
					bankshot2_update_file_length(nvf,
						read_offset + posix_read);

				read_count += posix_read;
				return read_count;
			}
			break;
		case 5: // Done by kernel.
			if (extent_length > len_to_read)
				extent_length = len_to_read;
			goto update_length;
//			goto try_again;
		default:
			break;
		}

		DEBUG("Pread: get extent return: mmap_addr %llx, length %llu\n",
					mmap_addr, extent_length);

		if (extent_length > len_to_read)
			extent_length = len_to_read;
		// File extent in cache. Just copy it to buf.
#if TIME_READ_MEMCPY
//		int cpu = get_cpuid();
		uint64_t start_time = getcycles();
#endif

		segfault = sigsetjmp(pread_jumper, 0);
		do_pread_memcpy = 1;

		if (segfault == 0) {
#if NOSANITYCHECK
#else
			void* result =
#endif
//				memcpy(buf, (char *)mmap_addr, extent_length);
//				clock_gettime(CLOCK_MONOTONIC, &start);
				memcpy1(buf, (char *)mmap_addr, extent_length);
//				clock_gettime(CLOCK_MONOTONIC, &end);
//				printf("memcpy time: %lu\n", end.tv_nsec - start.tv_nsec);
		} else if (segfault == 1) {
			segfault = 0;
			bankshot2_setup_signal_handler();
			MSG("Pread caught seg fault. Remove extent 0x%llx "
				"and try again.\n", read_offset);
			NVP_UNLOCK_NODE_RD(nvf, cpuid);
			NVP_LOCK_NODE_WR(nvf);
			remove_extent(nvf, read_offset);
			NVP_UNLOCK_NODE_WR(nvf);
			NVP_LOCK_NODE_RD(nvf, cpuid);
			do_pread_memcpy = 0;
			continue;
		}

		do_pread_memcpy = 0;
#if TIME_READ_MEMCPY
		uint64_t end_time = getcycles();
		total_memcpy_cycles += end_time - start_time;
//		if(cpu != get_cpuid()) {
//			printf("cpuid changed\n");
//			exit(1);
//		}
#endif

		SANITYCHECK(result == buf);
		SANITYCHECK(result > 0);
update_length:
		len_to_read -= extent_length;
		read_offset += extent_length;
		read_count  += extent_length;
		buf += extent_length; 

	}
	// nvf->offset += len_to_read; // NOT IN PREAD (this happens in read)

	DO_MSYNC(nvf);

	DEBUG("Return read_count %lu\n", read_count);
	return read_count;
}


RETT_PWRITE _bankshot2_do_pwrite(INTF_PWRITE, int wr_lock, int cpuid)
{
	int ret = 0;
	int segfault;
	off_t write_offset;
//	off_t origin_offset;
	size_t write_count, extent_length;
	size_t posix_write;
//	size_t origin_len, fix_len;
	unsigned long mmap_addr = 0;
	size_t file_length;
	char *new_buf;
//	int fix_mode;
//	char temp_buf[PAGE_SIZE];

	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	#if COUNT_EXTENDS
	_bankshot2_wr_total++;
	#endif

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
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
			DEBUG("count is not aligned to 512 (count was %li)\n", count);
			errno = EINVAL;
			return -1;
		}
		if(UNLIKELY(offset % 512))
		{
			DEBUG("offset was not aligned to 512 (offset was %li)\n", offset);
			errno = EINVAL;
			return -1;
		}
	//	if((long long int)buf % 512)
		if(UNLIKELY(((long long int)buf & (512-1)) != 0))
		{
			DEBUG("buffer was not aligned to 512 (buffer was %p, mod 512 = %li)\n", buf, (long long int)buf % 512);
			errno = EINVAL;
			return -1;
		}
	}

#if 0
	int intersects = 0;

	if( buf == (void*)nvf->node->data ) { intersects = 1; }
	if( (buf > (void*)nvf->node->data) && (buf <= (void*)nvf->node->data + nvf->node->maplength) ) { intersects = 1; }
	if( (buf < (void*)nvf->node->data) && (buf+count >= (void*)nvf->node->data) ) { intersects = 1; }

	if(UNLIKELY(intersects))
	{
		DEBUG("Buffer intersects with map (buffer %p len %p, map %p to %p)\n", buf, count, nvf->node->data, nvf->node->data-nvf->node->maplength);
		assert(0);
		return -1;
	}
#endif
	
	if(nvf->append)
	{
		DEBUG("this fd (%i) is O_APPEND; setting offset from the passed value (%li) to the end of the file (%li) prior to writing anything\n", nvf->fd, offset, nvf->node->length);
		offset = nvf->node->length;
	}

	ssize_t extension = count + offset - (nvf->node->length) ;

	DEBUG("time for a Pwrite. file length %li, offset %li, extension %li, count %li\n", nvf->node->length, offset, extension, count);

//	extension = 0;
#if 0
	if(extension > 0)
	{
		#if COUNT_EXTENDS
		_bankshot2_wr_extended++;
		#endif

		DEBUG("Request write length %li will extend file. (filelen=%li, offset=%li, count=%li, extension=%li)\n",
			count, nvf->node->length, offset, count, extension);
		
		if( offset+count >= nvf->node->cache_length )
		{
			DEBUG("Request will also extend map; doing that before extending file.\n");
//			_bankshot2_extend_map(nvf, offset+count );
		} else {
			DEBUG("However, map is already large enough: %li > %li\n", nvf->node->cache_length, offset+count);
			SANITYCHECK(nvf->node->cache_length > (offset+count));
		}

		DEBUG("Done extending map(s), now let's exend the file with PWRITE ");

//volatile int asdf=1; while(asdf){};

		ssize_t temp_result;
		if(nvf->aligned) {
			DEBUG_P("(aligned): %s->PWRITE(%i, %p, %li, %li)\n", _bankshot2_fileops->name, nvf->fd, _bankshot2_zbuf, 512, count+offset-512);
			temp_result = _bankshot2_fileops->PWRITE(nvf->fd, _bankshot2_zbuf, 512, count + offset - 512);
		} else {
			DEBUG_P("(unaligned)\n");
			temp_result = _bankshot2_fileops->PWRITE(nvf->fd, "\0", 1, count + offset - 1);
		}	

		if(temp_result != ((nvf->aligned)?512:1))
		{
			ERROR("Failed to use posix->pwrite to extend the file to the required length: returned %li, expected %li: %s\n", temp_result, ((nvf->aligned)?512:1), strerror(errno));
			if(nvf->aligned) {
				ERROR("Perhaps it's because this write needed to be aligned?\n");
			}
			PRINT_ERROR_NAME(errno);
			assert(0);
		}

		DEBUG("Done extending NVFile.\n");
	}
	else
	{
		DEBUG("File will NOT be extended: count + offset < length (%li < %li)\n", count+offset, nvf->node->length);
	}
#endif

//	DEBUG("Preforming "MK_STR(FSYNC_MEMCPY)"(%p (%p+%li), %p, %li)\n", nvf->node->data+offset, nvf->node->data, offset, buf, count);
	
	if(extension > 0)
	{
//		DEBUG("maplen = %li > filelen after write (%li)\n", nvf->node->cache_length, (nvf->node->length+extension));
//		SANITYCHECK( (nvf->node->length+extension) < nvf->node->cache_length);
	}
	else
	{
		DEBUG("maplen = %li > filelen after write (%li)\n", nvf->node->cache_length, nvf->node->length);
		SANITYCHECK( (nvf->node->length) < nvf->node->cache_length);
	}

	SANITYCHECK(nvf->valid);
	SANITYCHECK(nvf->node != NULL);
//	SANITYCHECK(nvf->node->data != NULL);
//	SANITYCHECK(nvf->node->maplength > nvf->node->cache_length + ((extension>0)?extension:0));
//	SANITYCHECK(nvf->node->data+offset > 0);
	SANITYCHECK(buf > 0);
	SANITYCHECK(count >= 0);

	ssize_t len_to_write = count;

	/* If request extent not in cache, we need to write to cache and add extent */
	write_count = 0;
	write_offset = offset;
	new_buf = (char *)buf;

	while(len_to_write > 0) {
		// If offset is unaligned to page or the length is smaller
		// than PAGE_SIZE, we need to read the data first
#if 0
		if ((write_offset % PAGE_SIZE) || (len_to_write < PAGE_SIZE)) {
			origin_buf = new_buf;
			if (write_offset % PAGE_SIZE == 0) {
				// len_to_write < PAGE_SIZE
				fix_mode = 1;

				_bankshot2_do_pread(file, temp_buf, PAGE_SIZE, write_offset);
				memcpy(temp_buf, buf, len_to_write);
			} else {
				fix_mode = 2;

				origin_offset = write_offset;
				origin_len = len_to_write;
				fix_len = len_to_write;

				len_to_write = PAGE_SIZE;
				write_offset -= (write_offset % PAGE_SIZE);

				_bankshot2_do_pread(file, temp_buf, PAGE_SIZE, write_offset);
				if (fix_len >= (PAGE_SIZE - (origin_offset - write_offset)))
					fix_len = PAGE_SIZE - (origin_offset - write_offset);

				memcpy(temp_buf + (origin_offset - write_offset),
						buf, fix_len);
				len_to_write = origin_offset - write_offset + fix_len;
			}
			buf = temp_buf;
		}
#endif
		DEBUG("Pwrite: looking for extent offset %d, size %d\n", write_offset, len_to_write);
		file_length = len_to_write;
		ret = bankshot2_get_extent(nvf, write_offset, &extent_length, &mmap_addr,
						&file_length, WRITE_EXTENT, new_buf, wr_lock, cpuid);
		DEBUG("Pwrite: get_extent returned %d, length %llu\n", ret, extent_length);
		switch (ret) {
		case 0:	// It's cached. Do memcpy.
//			if ((write_offset % 2097152) == 0)
//				ERROR("Pwrite: write_offset %u, mmap addr 0x%llx, length %u\n",
//					write_offset, mmap_addr, len_to_write);
			break;
		case 1:	// We have some big troubles.
			return write_count;
		case 2:
		case 4: // File hole, return zeros.
			if (extent_length > len_to_write)
				extent_length = len_to_write;
			DEBUG("File hole. fill with posix write. Offset: %.16llx, len: %llu\n",
					write_offset, extent_length);
			posix_write = _bankshot2_fileops->PWRITE(file, buf,
					extent_length, write_offset);
#if 0
			temp_extent_length = extent_length;
			temp_write_offset = write_offset;
			while (temp_extent_length > 0) {
				posix_write = _bankshot2_fileops->PWRITE(file, _bankshot2_zbuf,
						temp_extent_length >= 512 ? 512 : temp_extent_length,
						temp_write_offset);
				temp_extent_length -= posix_write;
				temp_write_offset += posix_write;
			}
#endif
			goto update_length;
		case 3: // EOF. write only correct amount.
			if (write_offset + len_to_write > file_length) {
				DEBUG("Near file end. Extending file\n");

				posix_write = _bankshot2_fileops->PWRITE(file, buf,
						len_to_write, write_offset);
				if (write_offset + posix_write > file_length)
					bankshot2_update_file_length(nvf, write_offset + posix_write);

				write_count += posix_write;
				return write_count;
			}
			break;
		case 5: // Done by kernel.
			if (extent_length > len_to_write)
				extent_length = len_to_write;
			goto update_length;
		default:
			break;
		}

		DEBUG("Pwrite: get extent return: mmap_addr %llx, length %llu\n",
					mmap_addr, extent_length);

		if (extent_length > len_to_write)
			extent_length = len_to_write;
		// File extent in cache. Just copy it to buf.
#if TIME_READ_MEMCPY
//		int cpu = get_cpuid();
		uint64_t start_time = getcycles();
#endif

		segfault = sigsetjmp(pwrite_jumper, 0);
		do_pwrite_memcpy = 1;

		if (segfault == 0) {
#if NOSANITYCHECK
#else
			void* result =
#endif
//			FSYNC_MEMCPY(buf, nvf->node->data+offset, len_to_read);
				FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length);
		} else if (segfault == 1) {
			segfault = 0;
			bankshot2_setup_signal_handler();
			MSG("Pwrite caught seg fault. Remove extent 0x%llx "
				"and try again.\n", write_offset);
			if (!wr_lock) {
				NVP_UNLOCK_NODE_RD(nvf, cpuid);
				NVP_LOCK_NODE_WR(nvf);
			}
			remove_extent(nvf, write_offset);
			if (!wr_lock) {
				NVP_UNLOCK_NODE_WR(nvf);
				NVP_LOCK_NODE_RD(nvf, cpuid);
			}
			do_pwrite_memcpy = 0;
			continue;
		}

		do_pwrite_memcpy = 0;


#if TIME_READ_MEMCPY
		uint64_t end_time = getcycles();
		total_memcpy_cycles += end_time - start_time;
//		if(cpu != get_cpuid()) {
//			printf("cpuid changed\n");
//			exit(1);
//		}
#endif

		SANITYCHECK(result == buf);
		SANITYCHECK(result > 0);
update_length:
//		if (fix_mode == 2) {
//			len_to_write = origin_len - fix_len;
//			write_offset = origin_offset + fix_len;
//			buf = origin_buf + fix_len; 
//			write_count += fix_len;
//		} else {
		len_to_write -= extent_length;
		write_offset += extent_length;
		write_count  += extent_length;
		buf += extent_length;
//		}
//		fix_mode = 0;
	}
	DO_MSYNC(nvf);
	DEBUG("_bankshot2_do_pwrite returned %lu\n", count);

	return count;
}

RETT_SEEK _bankshot2_SEEK(INTF_SEEK)
{
	DEBUG("_bankshot2_SEEK\n");
	return _bankshot2_SEEK64(CALL_SEEK);
}

RETT_SEEK64 _bankshot2_SEEK64(INTF_SEEK64)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_SEEK64\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix SEEK64 for fd %d\n", nvf->fd);
		return _bankshot2_fileops->SEEK64(CALL_SEEK64);
	}

	/* lseek() needs to populated to file system */
	_bankshot2_fileops->SEEK64(CALL_SEEK64);

	/* Use atomic operation to update offset */
//	int cpuid = GET_CPUID();

//	NVP_LOCK_FD_WR(nvf);
//	NVP_CHECK_NVF_VALID_WR(nvf);
//	NVP_LOCK_NODE_RD(nvf, cpuid);

	RETT_SEEK64 result =  _bankshot2_do_seek64(CALL_SEEK64);	

//	NVP_UNLOCK_NODE_RD(nvf, cpuid);
//	NVP_UNLOCK_FD_WR(nvf);

	return result;
}

RETT_SEEK64 _bankshot2_do_seek64(INTF_SEEK64)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_do_seek64\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	
	DEBUG("_bankshot2_do_seek64: file len %li, map len %li, current offset %li, requested offset %li with whence %li\n", 
		nvf->node->length, nvf->node->maplength, *nvf->offset, offset, whence);

	switch(whence)
	{
		case SEEK_SET:
			if(offset < 0)
			{
				DEBUG("offset out of range (would result in negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) = offset ;
			return *(nvf->offset);

		case SEEK_CUR:
			if((__sync_fetch_and_add(nvf->offset, 0) + offset) < 0)
			{
				DEBUG("offset out of range (would result in negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			__sync_fetch_and_add(nvf->offset, offset);
			return *(nvf->offset);

		case SEEK_END:
			if( nvf->node->length + offset < 0 )
			{
				DEBUG("offset out of range (would result in negative offset).\n");
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

RETT_TRUNC _bankshot2_TRUNC(INTF_TRUNC)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_TRUNC\n");

	return _bankshot2_TRUNC64(CALL_TRUNC);
}

RETT_TRUNC64 _bankshot2_TRUNC64(INTF_TRUNC64)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_TRUNC64\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix TRUNC64 for fd %d\n", nvf->fd);
		return _bankshot2_fileops->TRUNC64(CALL_TRUNC64);
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
		DEBUG("_bankshot2_TRUNC64: requested length was the same as old length (%li).\n",
			nvf->node->length);
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return 0;
	}

	DO_MSYNC(nvf);

//	assert(!munmap(nvf->node->data, nvf->node->maplength));

	int result = _bankshot2_fileops->TRUNC64(CALL_TRUNC64);

	if(result != 0)
	{
		ERROR("%s->TRUNC64 failed (returned %li, requested %li): %s\n", _bankshot2_fileops->name, result, length, strerror(errno));
		assert(0);
	}

	if(length > nvf->node->length)
	{
		DEBUG("TRUNC64 extended file from %li to %li\n", nvf->node->length, length);
	}
	else 
	{
		DEBUG("TRUNC64 shortened file from %li to %li\n", nvf->node->length, length);
	}

	DEBUG("Done with trunc, we better update map!\n");

//	_bankshot2_extend_map(nvf, length);

	nvf->node->length = length;

	DO_MSYNC(nvf);

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

RETT_READV _bankshot2_READV(INTF_READV)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("CALL: _bankshot2_READV\n");

	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _bankshot2_READ(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_bankshot2_READV failed on iov %i\n", i);
		return -1;
	}

	return 0;
}

RETT_WRITEV _bankshot2_WRITEV(INTF_WRITEV)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("CALL: _bankshot2_WRITEV\n");

	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _bankshot2_WRITE(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_bankshot2_WRITEV failed on iov %i\n", i);
		return -1;
	}

	return 0;
}

RETT_DUP _bankshot2_DUP(INTF_DUP)
{
	DEBUG("_bankshot2_DUP(" PFFS_DUP ")\n", CALL_DUP);

	//CHECK_RESOLVE_FILEOPS(_bankshot2_);
	if(file < 0) {
		return _bankshot2_fileops->DUP(CALL_DUP);
	}

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	 
	//int iter;
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);	
	NVP_LOCK_NODE_WR(nvf); // TODO

	int result = _bankshot2_fileops->DUP(CALL_DUP);

	if(result < 0) 
	{
		DEBUG("Call to _bankshot2_DUP->%s->DUP failed: %s\n",
			_bankshot2_fileops->name, strerror(errno));
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		return result;
	}

	struct NVFile* nvf2 = &_bankshot2_fd_lookup[result];

	if (nvf->posix) {
		DEBUG("Call posix DUP for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
//		nvf2->cache_fd = nvf->cache_fd;
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
//	nvf2->cache_fd	= nvf->cache_fd;

	SANITYCHECK(nvf2->node != NULL);

	nvf2->valid 	= 1;

	DO_MSYNC(nvf);
	DO_MSYNC(nvf2);

	NVP_UNLOCK_NODE_WR(nvf); // nvf2->node->lock == nvf->node->lock since nvf and nvf2 share a node
	NVP_UNLOCK_FD_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf2);

	return nvf2->fd;
}

RETT_DUP2 _bankshot2_DUP2(INTF_DUP2)
{
	//CHECK_RESOLVE_FILEOPS(_bankshot2_);
	DEBUG("_bankshot2_DUP2(" PFFS_DUP2 ")\n", CALL_DUP2);
	
	if(file<0) {
		return _bankshot2_fileops->DUP(CALL_DUP);
	}

	if(fd2<0) {
		DEBUG("Invalid fd2\n");
		errno = EBADF;
		return -1;
	}

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	struct NVFile* nvf2 = &_bankshot2_fd_lookup[fd2];

#if 0
	/* We want to DUP the cache fd actually */
	file = nvf->cache_fd;

	if(file == fd2)
	{
		DEBUG("Input and output files were the same (%i)\n", file);
		return file;
	}
#endif

	if (nvf->posix) {
		DEBUG("Call posix DUP2 for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		int result = _bankshot2_fileops->DUP2(CALL_DUP2);
		nvf2->fd = result;
//		nvf2->cache_fd = nvf->cache_fd;
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

	int result = _bankshot2_fileops->DUP2(CALL_DUP2);

	if(result < 0)
	{
		DEBUG("_bankshot2_DUP2 failed to %s->DUP2(%i, %i) (returned %i): %s\n", _bankshot2_fileops->name, file, fd2, result, strerror(errno));
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

	_bankshot2_test_invalidate_node(nvf2);

	if(result != fd2)
	{
		WARNING("result of _bankshot2_DUP2(%i, %i) didn't return the fd2 that was just closed.  Technically this doesn't violate POSIX, but I DON'T LIKE IT.  (Got %i, expected %i)\n",
			file, fd2, result, fd2);
		assert(0);

		NVP_UNLOCK_FD_WR(nvf2);

		nvf2 = &_bankshot2_fd_lookup[result];

		NVP_LOCK_FD_WR(nvf2);

		if(nvf2->valid)
		{
			DEBUG("%s->DUP2 returned a result which corresponds to an already open NVFile! dup2(%i, %i) returned %i\n", _bankshot2_fileops->name, file, fd2, result);
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
//	nvf2->cache_fd = nvf->cache_fd;

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


RETT_IOCTL _bankshot2_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("CALL: _bankshot2_IOCTL\n");

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _bankshot2_fileops->IOCTL(file, request, third);

	return result;
}

#define TIME_EXTEND 0

static int _bankshot2_get_fd_with_max_perms(struct NVFile *nvf, int file, int *max_perms)
{
	int fd_with_max_perms = file; // may not be marked as valid

	if( (!FLAGS_INCLUDE(*max_perms, PROT_READ)) || (!FLAGS_INCLUDE(*max_perms, PROT_WRITE)) )
	{
		int i;
		for(i=0; i<OPEN_MAX; i++)
		{
			if( (_bankshot2_fd_lookup[i].valid) && (nvf->node==_bankshot2_fd_lookup[i].node) )
			{
				if( (!FLAGS_INCLUDE(*max_perms, PROT_READ)) && (_bankshot2_fd_lookup[i].canRead) )
				{
					DEBUG("FD %i is adding read perms and is the new fd_with_max_perms (was %i, called with %i)\n", i, fd_with_max_perms, file);
					*max_perms = PROT_READ;
					fd_with_max_perms = i;
				}
				if( (!FLAGS_INCLUDE(*max_perms, PROT_WRITE)) && (_bankshot2_fd_lookup[i].canWrite) )
				{
					DEBUG("FD %i is adding write perms and is the new fd_with_max_perms, but may include O_APPEND (was %i, called with %i)\n", i, fd_with_max_perms, file);
					*max_perms = PROT_READ|PROT_WRITE;
					fd_with_max_perms = i;
				}
				if( (_bankshot2_fd_lookup[i].canWrite) && (!_bankshot2_fd_lookup[i].append) )
				{
					DEBUG("FD %i is adding write perms and is the new fd_with_max_perms (was %i, called with %i)\n", i, fd_with_max_perms, file);
					fd_with_max_perms = i;
					break;
				}
			}
		}
	}

	DEBUG("FD with max perms %i has read? %s   has write? %s\n", fd_with_max_perms, (_bankshot2_fd_lookup[fd_with_max_perms].canRead)?"yes":"no", (_bankshot2_fd_lookup[fd_with_max_perms].canWrite)?"yes":"no");

	SANITYCHECK(FLAGS_INCLUDE(*max_perms, PROT_READ));

	return fd_with_max_perms;
}

int _bankshot2_extend_map(struct NVFile *nvf, size_t newcharlen)
{
	int file = nvf->fd;

	size_t newmaplen = (newcharlen/MMAP_PAGE_SIZE + 1)*MMAP_PAGE_SIZE;

	DEBUG("Extend mapping for fd %d, cache fd %d\n", nvf->fd, nvf->fd);

#if TIME_EXTEND
	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);
#endif
	if(nvf->node->maplength > 0) {
		newmaplen *= 2;
	}
	
	//int newmaplen = newcharlen + 1;

	SANITYCHECK(nvf->node != NULL);
	//SANITYCHECK(nvf->node->valid);
	//SANITYCHECK(newmaplen > nvf->node->maplength);
	SANITYCHECK(newmaplen > newcharlen);

	if(newmaplen < nvf->node->maplength)
	{
		DEBUG("Just kidding, _bankshot2_extend_map is actually going to SHRINK the map from %li to %li\n", nvf->node->maplength, newmaplen);
	}
	else
	{
		DEBUG("_bankshot2_extend_map increasing map length from %li to %li\n", nvf->node->maplength, newmaplen);
	}

/*	// munmap first
	if(nvf->node->data != NULL) {
		DEBUG("Let's try munmapping every time.\n");

		if(munmap(nvf->node->data, nvf->node->maplength))
		{
			ERROR("Couldn't munmap: %s\n", strerror(errno));
		}
	}
*/
	

	int fd_with_max_perms = file; // may not be marked as valid
	int max_perms = ((nvf->canRead)?PROT_READ:0)|((nvf->canWrite)?PROT_WRITE:0);

	SANITYCHECK(max_perms);
//	SANITYCHECK(FLAGS_INCLUDE(max_perms, PROT_READ));

//	DEBUG("newcharlen is %li bytes (%li MB) (%li GB)\n", newcharlen, newcharlen/1024/1024, newcharlen/1024/1024/1024);

	fd_with_max_perms = _bankshot2_get_fd_with_max_perms(nvf, file, &max_perms);
/*
	if(nvf->node->maxPerms != max_perms)
	{
		DEBUG("Max perms for node %p are changing (to %i).\n", nvf->node, max_perms);
	}
*/
	if(file != fd_with_max_perms) {
		DEBUG("Was going to extend map with fd %i, but changed to %i because it has higher perms\n", file, fd_with_max_perms);
	} else {
		DEBUG("Going to extend map with the same fd that was called (%i)\n", file);
	}

	DEBUG("Requesting read perms? %s   Requesting write perms? %s\n", ((FLAGS_INCLUDE(max_perms, PROT_READ)?"yes":"no")), ((FLAGS_INCLUDE(max_perms, PROT_WRITE)?"yes":"no")));

//	nvf->node->maxPerms = max_perms;

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000 /* arch specific */
#endif


	DEBUG("mmap fd %d\n", fd_with_max_perms);
	// mmap replaces old maps where they intersect
	char* result = (char*) FSYNC_MMAP
	(
		nvf->node->data,
		newmaplen,
		max_perms, //max_perms,
		MAP_SHARED //|  MAP_HUGETLB
		#if MMAP_PREFAULT
			|MAP_POPULATE
		#endif
		,
		fd_with_max_perms, //fd_with_max_perms,
		0
	);

/*
	// mmap replaces old maps where they intersect
	char* result = (char*) mmap
	(
		nvf->node->data,
		newmaplen,
		nvf->node->maxPerms, //max_perms,
		MAP_SHARED,
		nvf->fd, //fd_with_max_perms,
		0
	);
*/

	if( result == MAP_FAILED || result == NULL )
	{
		MSG("mmap failed for fd %i: %s\n", nvf->fd, strerror(errno));
		MSG("Use posix operations for fd %i instead.\n", nvf->fd);
		nvf->posix = 1;
		return 0;
	}

	if( nvf->node->data != result )
	{
		DEBUG("mmap address changed from %p to %p\n", nvf->node->data, result);
		nvf->node->data = result;
	}
	else
	{
		DEBUG("mmap address stayed the same (%p)\n", result);
	}
/*
	MSG("Using MADV_HUGEPAGE\n");
	madvise(result, newmaplen, MADV_HUGEPAGE);
*/

	#if MANUAL_PREFAULT
	MSG("Walking the pages to make extra double sure they're not going to fault (from %p walking from %p to %p; map goes to %p)\n", nvf->node->data, 0, newcharlen, newmaplen);
	DEBUG("NOTE: this has no way to check for file holes!  If there is a hole, and it tries to read it, it will SIGBUS.\n");
	
	char temp_rd_buf;

	//size_t j;
	uint64_t j;
	for(j=0; j<newcharlen; j+=4096)
	{
		//_bankshot2_fileops->PREAD(nvf->fd, temp_rd_buf, 4096, j);
	//	if(!(j%0x80000000)) {
		//	DEBUG("Reading from base %p offset %p (address %p), going from 0x0 to %p\n", nvf->node->data, j, nvf->node->data+j, newcharlen);
		//	DEBUG("distance from end of file: %li (%li MB)\n", (((int64_t)newcharlen) - ((int64_t)j)), (((int64_t)newcharlen) - ((int64_t)j))/1024/1024 );
		//	DEBUG("distance from end of file: %li\n", (((int64_t)nvf->node->data)+((int64_t)newcharlen)) - (((int64_t)nvf->node->data)+j) );
	//	}
		temp_rd_buf = ((volatile char*)nvf->node->data)[j];
	}
	#endif

	nvf->node->maplength = newmaplen;

	DO_MSYNC(nvf);

#if TIME_EXTEND
	gettimeofday(&end, NULL);
	long long unsigned int time_us;
	time_us  = end.tv_sec-start.tv_sec;
	time_us *= 1000000;
	time_us += end.tv_usec-start.tv_usec;
	MSG("Time to extend map for %p was %ius\n", nvf->node, time_us);
#endif
	return 0;
}

int _bankshot2_test_mmap(int file, size_t newcharlen)
{
	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	int ret = 0;

	size_t newmaplen = (newcharlen/MMAP_PAGE_SIZE + 1)*MMAP_PAGE_SIZE;
	
	if(nvf->node->maplength > 0) {
		newmaplen *= 2;
	}
	
	//int newmaplen = newcharlen + 1;

	SANITYCHECK(nvf->node != NULL);
	//SANITYCHECK(nvf->node->valid);
	//SANITYCHECK(newmaplen > nvf->node->maplength);
	SANITYCHECK(newmaplen > newcharlen);

	if(newmaplen < nvf->node->maplength)
	{
		DEBUG("Just kidding, _bankshot2_extend_map is actually going to SHRINK the map from %li to %li\n", nvf->node->maplength, newmaplen);
	}
	else
	{
		DEBUG("_bankshot2_extend_map increasing map length from %li to %li\n", nvf->node->maplength, newmaplen);
	}

/*	// munmap first
	if(nvf->node->data != NULL) {
		DEBUG("Let's try munmapping every time.\n");

		if(munmap(nvf->node->data, nvf->node->maplength))
		{
			ERROR("Couldn't munmap: %s\n", strerror(errno));
		}
	}
*/
	
	int fd_with_max_perms = file; // may not be marked as valid
	int max_perms = ((nvf->canRead)?PROT_READ:0)|((nvf->canWrite)?PROT_WRITE:0);

	SANITYCHECK(max_perms);
//	SANITYCHECK(FLAGS_INCLUDE(max_perms, PROT_READ));

//	DEBUG("newcharlen is %li bytes (%li MB) (%li GB)\n", newcharlen, newcharlen/1024/1024, newcharlen/1024/1024/1024);

	fd_with_max_perms = _bankshot2_get_fd_with_max_perms(nvf, file, &max_perms);
/*
	if(nvf->node->maxPerms != max_perms)
	{
		DEBUG("Max perms for node %p are changing (to %i).\n", nvf->node, max_perms);
	}
*/
	if(file != fd_with_max_perms) {
		DEBUG("Was going to extend map with fd %i, but changed to %i because it has higher perms\n", file, fd_with_max_perms);
	} else {
		DEBUG("Going to extend map with the same fd that was called (%i)\n", file);
	}

	DEBUG("Requesting read perms? %s   Requesting write perms? %s\n", ((FLAGS_INCLUDE(max_perms, PROT_READ)?"yes":"no")), ((FLAGS_INCLUDE(max_perms, PROT_WRITE)?"yes":"no")));

//	nvf->node->maxPerms = max_perms;

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000 /* arch specific */
#endif


	// mmap replaces old maps where they intersect
	char* result = (char*) FSYNC_MMAP
	(
		nvf->node->data,
		newmaplen,
		max_perms, //max_perms,
		MAP_SHARED //|  MAP_HUGETLB
		#if MMAP_PREFAULT
			|MAP_POPULATE
		#endif
		,
		fd_with_max_perms, //fd_with_max_perms,
		0
	);

	if( result == MAP_FAILED || result == NULL )
	{
		MSG("mmap failed for fd %i: %s\n", nvf->fd, strerror(errno));
		MSG("Use posix operations for fd %i instead.\n", nvf->fd);
		nvf->posix = 1;
		ret = -1;
	}
	else
	{
		DEBUG("mmap succeed. Unmap it and create a cache file.\n");
		munmap(result, newmaplen);
	}

	return ret;
}

void _bankshot2_test_invalidate_node(struct NVFile* nvf)
{
	struct NVNode* node = nvf->node;

	DEBUG("munmapping temporarily diabled...\n"); // TODO

	return;

	SANITYCHECK(node!=NULL);

	int do_munmap = 1;

	int i;
	for(i=0; i<OPEN_MAX; i++)
	{
		if( (_bankshot2_fd_lookup[i].valid) && (node==_bankshot2_fd_lookup[i].node) )
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

//		free(node);

		nvf->node = NULL; // we don't want anyone using this again
		
		DEBUG("munmap successful.\n");
	}
	else
	{
		DEBUG("Node appears to still be in use.  Not munmapping.\n");
	}
}

void _bankshot2_SIGBUS_handler(int sig)
{
	ERROR("We got a SIGBUS (sig %i)!  This almost certainly means someone tried to access an area inside an mmaped region but past the length of the mmapped file.\n", sig);
	#if MANUAL_PREFAULT
	ERROR("   OR if this happened during prefault, we probably just tried to prefault a hole in the file, which isn't going to work.\n");
	#endif
//	_bankshot2_debug_handoff();
	assert(0);
}

void _bankshot2_SIGSEGV_handler(int sig)
{
	DEBUG("We got a SIGSEGV (sig %i)!  This almost certainly means someone tried to access an area inside an mmaped region but past the length of the mmapped file.\n", sig);
	#if MANUAL_PREFAULT
//	ERROR("   OR if this happened during prefault, we probably just tried to prefault a hole in the file, which isn't going to work.\n");
	#endif
//	_bankshot2_debug_handoff();
	if (do_pread_memcpy) {
		siglongjmp(pread_jumper, 1);
	} else if (do_pwrite_memcpy) {
		siglongjmp(pwrite_jumper, 1);
	} else {
		ERROR("Seg fault when neither doing pread nor pwrite!\n");
		assert(0);
	}
}
