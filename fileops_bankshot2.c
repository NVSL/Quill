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

//#include "my_memcpy_nocache.h"

// TODO: manual prefaulting sometimes segfaults
#define MANUAL_PREFAULT 0
#define MMAP_PREFAULT 1

#define DO_ALIGNMENT_CHECKS 0

struct timezone;
struct timeval;
int gettimeofday(struct timeval *tv, struct timezone *tz);


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
};

struct NVFile* _bankshot2_fd_lookup;

void _bankshot2_init2(void);
int _bankshot2_extend_map(int file, size_t newlen);
void _bankshot2_SIGBUS_handler(int sig);
void _bankshot2_test_invalidate_node(struct NVFile* nvf);

RETT_PWRITE _bankshot2_do_pwrite(INTF_PWRITE); // like PWRITE, but without locks (called by _bankshot2_WRITE)
RETT_PWRITE _bankshot2_do_pread (INTF_PREAD ); // like PREAD , but without locks (called by _bankshot2_READ )
RETT_SEEK64 _bankshot2_do_seek64(INTF_SEEK64); // called by nvp_seek, nvp_seek64, nvp_write

#define DO_MSYNC(nvf) do{ \
	DEBUG("NOT doing a msync\n"); }while(0)
/*
	DEBUG("Doing a msync on fd %i (node %p)\n", nvf->fd, nvf->node); \
	if(msync(nvf->node->data, nvf->node->maplength, MS_SYNC|MS_INVALIDATE)) { \
		ERROR("Failed to msync for fd %i\n", nvf->fd); \
		assert(0); \
	} }while(0)
*/

int _bankshot2_lock_return_val;
int _bankshot2_write_pwrite_lock_handoff;


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

void _bankshot2_init2(void)
{
	#if TIME_READ_MEMCPY
	atexit(report_memcpy_usec);
	#endif

	_bankshot2_write_pwrite_lock_handoff = 0;

	assert(!posix_memalign(((void**)&_bankshot2_zbuf), 4096, 4096));

	_bankshot2_fd_lookup = (struct NVFile*) calloc(OPEN_MAX, sizeof(struct NVFile));

	int i;
	for(i=0; i<OPEN_MAX; i++) {
		_bankshot2_fd_lookup[i].valid = 0;
		NVP_LOCK_INIT(_bankshot2_fd_lookup[i].lock);
	}

	MMAP_PAGE_SIZE = getpagesize();
	SANITYCHECK(MMAP_PAGE_SIZE > 100);

	#if COUNT_EXTENDS
	_bankshot2_wr_extended = 0;
	_bankshot2_wr_total    = 0;
	#endif

	DEBUG("Installing SIGBUS handler.\n");
	signal(SIGBUS, _bankshot2_SIGBUS_handler);

	//TODO
	/*
	#define GLIBC_LOC ""
	MSG("Importing memcpy from %s\n", GLIBC_LOC);
	*/

//	_bankshot2_debug_handoff();
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

RETT_OPEN _bankshot2_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	if(path==NULL) {
		DEBUG("Invalid path.\n");
		errno = EINVAL;
		return -1;
	}
	
	DEBUG("_bankshot2_OPEN(%s)\n", path);
	
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
			ERROR("File at path %s is NOT a regular file!  INCONCEIVABLE\n", path);
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
		int i;
		for(i=0; i<OPEN_MAX; i++)
		{
			if( _bankshot2_fd_lookup[i].node && _bankshot2_fd_lookup[i].node->serialno == file_st.st_ino) {
				DEBUG("File %s is (or was) already open in fd %i (this fd hasn't been __open'ed yet)!  Sharing nodes.\n", path, i);
				node = _bankshot2_fd_lookup[i].node;
				SANITYCHECK(node != NULL);
				// when sharing nodes it's good to msync in case of multithreading // TODO is this true?
				if(msync(node->data, node->maplength, MS_SYNC|MS_INVALIDATE)) {
					ERROR("Failed to msync for path %s\n", path);
					assert(0);
				}
				break;
			}
		}
		if(node==NULL) {
			DEBUG("File %s is not already open.  Allocating new NVNode.\n", path);
			node = (struct NVNode*) calloc(1, sizeof(struct NVNode));
//			int asdf=1; while(asdf){};
			NVP_LOCK_INIT(node->lock);
			node->length = file_st.st_size;
			node->maplength = 0;
			node->serialno = file_st.st_ino;
		}
		
		NVP_LOCK_WR(node->lock);
	}

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
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
		int i;
		for(i=0; i<OPEN_MAX; i++)
		{
			if( _bankshot2_fd_lookup[i].node && _bankshot2_fd_lookup[i].node->serialno == file_st.st_ino) {
				DEBUG("File %s is (or was) already open in fd %i (this fd is fd %i)!  Sharing nodes.\n", path, i, result);
				node = _bankshot2_fd_lookup[i].node;
				SANITYCHECK(node != NULL);
				// when sharing nodes it's good to msync in case of multithreading // TODO is this true?
				if(msync(node->data, node->maplength, MS_SYNC|MS_INVALIDATE)) {
					ERROR("Failed to msync for fd %i\n", result);
					assert(0);
				}
				break;
			}
		}
		if(node==NULL) {
			DEBUG("File %s is not already open.  Allocating new NVNode.\n", path);
			node = (struct NVNode*) calloc(1, sizeof(struct NVNode));
			NVP_LOCK_INIT(_bankshot2_fd_lookup[i].lock);
			node->length = file_st.st_size;
			node->maplength = 0;
			node->serialno = file_st.st_ino;
		}
		
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
		ERROR("mmap doesn't support O_WRONLY!  goodbye.\n");
		assert(0);
		nvf->canRead = 0;
		nvf->canWrite = 1;
		#endif
	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG("oflag (%i) specifies O_RDONLY for fd %i\n", oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 0;
	} else {
		ERROR("File permissions don't include read or write!\n");
		nvf->canRead = 0;
		nvf->canWrite = 0;
		assert(0);
	}
	
	if(FLAGS_INCLUDE(oflag, O_APPEND)) {
		nvf->append = 1;
	} else {
		nvf->append = 0;
	}

	nvf->node = node;

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

//	if(nvf->node->maplength < nvf->node->length)
//	{
//		DEBUG("map was not already allocated (was %li).  Let's allocate one.\n", nvf->node->maplength);
//		_bankshot2_extend_map(nvf->fd, nvf->node->length);
	
		//if(_bankshot2_extend_map(nvf->fd, MAX(1, MAX(nvf->node->maplength+1, nvf->node->length))))
		if(_bankshot2_extend_map(nvf->fd, MAX(1, nvf->node->length)))
		{
			DEBUG("Failed to _bankshot2_extend_map, passing it up the chain\n");
			NVP_UNLOCK_NODE_WR(nvf);
			NVP_UNLOCK_FD_WR(nvf);
			return -1;
		}

		SANITYCHECK(nvf->node->maplength > 0);
		SANITYCHECK(nvf->node->maplength > nvf->node->length);
		
		if(nvf->node->data < 0) {
			ERROR("Failed to mmap path %s: %s\n", path, strerror(errno));
			assert(0);
		}

		DEBUG("mmap successful.  result: %p\n", nvf->node->data);
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
	return nvf->fd;
}

RETT_CLOSE _bankshot2_CLOSE(INTF_CLOSE)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_CLOSE(%i)\n", file);

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	//int iter;
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_WR(nvf);

	nvf->valid = 0;

	//_bankshot2_test_invalidate_node(nvf);

	RETT_CLOSE result = _bankshot2_fileops->CLOSE(CALL_CLOSE);

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);

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

RETT_READ _bankshot2_READ(INTF_READ)
{
	DEBUG("_bankshot2_READ\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	
	int cpuid = -1;

	RETT_READ result = _bankshot2_check_read_size_valid(length);
	if (result <= 0)
		return result;

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);

	NVP_LOCK_NODE_RD(nvf, cpuid);

	result = _bankshot2_do_pread(CALL_READ, __sync_fetch_and_add(nvf->offset, length));

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	
	if(result == length)	{
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

	return result;
}

RETT_WRITE _bankshot2_WRITE(INTF_WRITE)
{
	DEBUG("_bankshot2_WRITE\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	//int iter;
	int cpuid = -1;
	RETT_WRITE result = _bankshot2_check_write_size_valid(length);
	if (result <= 0)
		return result;

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid); //TODO

	result = _bankshot2_do_pwrite(CALL_WRITE, __sync_fetch_and_add(nvf->offset, length));

	NVP_UNLOCK_NODE_RD(nvf, cpuid);

	if(result >= 0)
	{
		if(nvf->append)
		{
			size_t temp_offset = __sync_fetch_and_add(nvf->offset, 0);
			ERROR("PWRITE succeeded and append == true.  Setting offset to end...\n"); 
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

	DEBUG("_bankshot2_PREAD\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	RETT_PREAD result = _bankshot2_check_read_size_valid(count);
	if (result <= 0)
		return result;

	int cpuid = -1;
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	result = _bankshot2_do_pread(CALL_PREAD);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

RETT_PWRITE _bankshot2_PWRITE(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_PWRITE\n");

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	
	RETT_PWRITE result = _bankshot2_check_write_size_valid(count);
	if (result <= 0)
		return result;
	
	int cpuid = -1;
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);
	
	ssize_t available_length = (nvf->node->length) - offset;

	if(count > available_length) {
		DEBUG("Promoting PWRITE lock to WRLOCK\n");
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		NVP_LOCK_NODE_WR(nvf);
		
		result = _bankshot2_do_pwrite(CALL_PWRITE);

		NVP_UNLOCK_NODE_WR(nvf);
	}
	else {
		result = _bankshot2_do_pwrite(CALL_PWRITE);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

RETT_PREAD _bankshot2_do_pread(INTF_PREAD)
{
	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	SANITYCHECKNVF(nvf);

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

	int intersects = 0;

	if(UNLIKELY( buf == (void*)nvf->node->data )) { intersects = 1; }
	if(UNLIKELY( (buf > (void*)nvf->node->data) && (buf <= (void*)nvf->node->data + nvf->node->maplength) )) { intersects = 1; }
	if(UNLIKELY( (buf < (void*)nvf->node->data) && (buf+count >= (void*)nvf->node->data) )) { intersects = 1; }

	if(UNLIKELY(intersects))
	{
		ERROR("Buffer intersects with map (buffer %p map %p)\n", buf, nvf->node->data);
		assert(0);
		return -1;
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

	DEBUG("Preforming "MK_STR(FSYNC_MEMCPY)"(%p, %p (%p+%li), %li (call was %li))\n", buf, nvf->node->data+offset, nvf->node->data, offset, len_to_read, count);

	DEBUG("mmap is length %li, len_to_read is %li\n", nvf->node->maplength, len_to_read);

	SANITYCHECK(len_to_read + offset <= nvf->node->length);
	SANITYCHECK(nvf->node->length < nvf->node->maplength);

#if TIME_READ_MEMCPY
//	int cpu = get_cpuid();
	uint64_t start_time = getcycles();
#endif

	#if NOSANITYCHECK
	#else
	void* result =
	#endif
//		FSYNC_MEMCPY(buf, nvf->node->data+offset, len_to_read);
		memcpy1(buf, nvf->node->data+offset, len_to_read);


#if TIME_READ_MEMCPY
	uint64_t end_time = getcycles();
	total_memcpy_cycles += end_time - start_time;
//	if(cpu != get_cpuid()) {
//		printf("cpuid changed\n");
//		exit(1);
//	}
#endif

	SANITYCHECK(result == buf);
	SANITYCHECK(result > 0);

	// nvf->offset += len_to_read; // NOT IN PREAD (this happens in read)

	DO_MSYNC(nvf);

	return len_to_read;
}


RETT_PWRITE _bankshot2_do_pwrite(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_bankshot2_);

	DEBUG("_bankshot2_do_pwrite\n");

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

	int intersects = 0;

	if( buf == (void*)nvf->node->data ) { intersects = 1; }
	if( (buf > (void*)nvf->node->data) && (buf <= (void*)nvf->node->data + nvf->node->maplength) ) { intersects = 1; }
	if( (buf < (void*)nvf->node->data) && (buf+count >= (void*)nvf->node->data) ) { intersects = 1; }

	if(UNLIKELY(intersects))
	{
		ERROR("Buffer intersects with map (buffer %p len %p, map %p to %p)\n", buf, count, nvf->node->data, nvf->node->data-nvf->node->maplength);
		assert(0);
		return -1;
	}
	
	if(nvf->append)
	{
		DEBUG("this fd (%i) is O_APPEND; setting offset from the passed value (%li) to the end of the file (%li) prior to writing anything\n", nvf->fd, offset, nvf->node->length);
		offset = nvf->node->length;
	}

	ssize_t extension = count + offset - (nvf->node->length) ;

	DEBUG("time for a Pwrite. file length %li, offset %li, extension %li, count %li\n", nvf->node->length, offset, extension, count);
	
	if(extension > 0)
	{
		#if COUNT_EXTENDS
		_bankshot2_wr_extended++;
		#endif

		DEBUG("Request write length %li will extend file. (filelen=%li, offset=%li, count=%li, extension=%li)\n",
			count, nvf->node->length, offset, count, extension);
		
		if( offset+count >= nvf->node->maplength )
		{
			DEBUG("Request will also extend map; doing that before extending file.\n");
			_bankshot2_extend_map(file, offset+count );
		} else {
			DEBUG("However, map is already large enough: %li > %li\n", nvf->node->maplength, offset+count);
			SANITYCHECK(nvf->node->maplength > (offset+count));
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

	DEBUG("Preforming "MK_STR(FSYNC_MEMCPY)"(%p (%p+%li), %p, %li)\n", nvf->node->data+offset, nvf->node->data, offset, buf, count);
	
	if(extension > 0)
	{
		DEBUG("maplen = %li > filelen after write (%li)\n", nvf->node->maplength, (nvf->node->length+extension));
		SANITYCHECK( (nvf->node->length+extension) < nvf->node->maplength);
	}
	else
	{
		DEBUG("maplen = %li > filelen after write (%li)\n", nvf->node->maplength, nvf->node->length);
		SANITYCHECK( (nvf->node->length) < nvf->node->maplength);
	}

	SANITYCHECK(nvf->valid);
	SANITYCHECK(nvf->node != NULL);
	SANITYCHECK(nvf->node->data != NULL);
	SANITYCHECK(nvf->node->maplength > nvf->node->length + ((extension>0)?extension:0));
	SANITYCHECK(nvf->node->data+offset > 0);
	SANITYCHECK(buf > 0);
	SANITYCHECK(count >= 0);

	FSYNC_MEMCPY(nvf->node->data+offset, buf, count);

	if(extension > 0) {
		DEBUG("Extending file length by %li from %li to %li\n", extension, nvf->node->length, nvf->node->length + extension);
		nvf->node->length += extension;
	}

	//nvf->offset += count; // NOT IN PWRITE (this happens in write)

	DEBUG("About to return from _bankshot2_PWRITE with ret val %li.  file len: %li, file off: %li, map len: %li, node %p\n", count, nvf->node->length, nvf->offset, nvf->node->maplength, nvf->node);

	DO_MSYNC(nvf);

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
	
	int cpuid = -1;
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	RETT_SEEK64 result =  _bankshot2_do_seek64(CALL_SEEK64);	

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_WR(nvf);

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
			if((*(nvf->offset) + offset) < 0)
			{
				DEBUG("offset out of range (would result in negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) += offset ;
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

	int cpuid = -1;
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

	assert(!munmap(nvf->node->data, nvf->node->maplength));

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

	_bankshot2_extend_map(nvf->fd, length);

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
	if(file<0) {
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

	if(file == fd2)
	{
		DEBUG("Input and output files were the same (%i)\n", file);
		return file;
	}

	struct NVFile* nvf = &_bankshot2_fd_lookup[file];
	struct NVFile* nvf2 = &_bankshot2_fd_lookup[fd2];

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
		NVP_UNLOCK_FD_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf2);
	}

	if(nvf->node == nvf2->node) {
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
	
	if(nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }

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
			ERROR("%s->DUP2 returned a result which corresponds to an already open NVFile! dup2(%i, %i) returned %i\n", _bankshot2_fileops->name, file, fd2, result);
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

int _bankshot2_extend_map(int file, size_t newcharlen)
{
	struct NVFile* nvf = &_bankshot2_fd_lookup[file];

	size_t newmaplen = (newcharlen/MMAP_PAGE_SIZE + 1)*MMAP_PAGE_SIZE;
	
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

	if( (!FLAGS_INCLUDE(max_perms, PROT_READ)) || (!FLAGS_INCLUDE(max_perms, PROT_WRITE)) )
	{
		int i;
		for(i=0; i<OPEN_MAX; i++)
		{
			if( (_bankshot2_fd_lookup[i].valid) && (nvf->node==_bankshot2_fd_lookup[i].node) )
			{
				if( (!FLAGS_INCLUDE(max_perms, PROT_READ)) && (_bankshot2_fd_lookup[i].canRead) )
				{
					DEBUG("FD %i is adding read perms and is the new fd_with_max_perms (was %i, called with %i)\n", i, fd_with_max_perms, file);
					max_perms = PROT_READ;
					fd_with_max_perms = i;
				}
				if( (!FLAGS_INCLUDE(max_perms, PROT_WRITE)) && (_bankshot2_fd_lookup[i].canWrite) )
				{
					DEBUG("FD %i is adding write perms and is the new fd_with_max_perms, but may include O_APPEND (was %i, called with %i)\n", i, fd_with_max_perms, file);
					max_perms = PROT_READ|PROT_WRITE;
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

	SANITYCHECK(FLAGS_INCLUDE(max_perms, PROT_READ));
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
		ERROR("mmap FAILED for fd %i: %s\n", nvf->fd, strerror(errno));
		assert(0);
		return -1;
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

		free(node);

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

