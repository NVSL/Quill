// Header file shared by nvmfileops.c, fileops_compareharness.c

#ifndef __NV_COMMON_H_
#define __NV_COMMON_H_

#ifndef __cplusplus
//typedef long long off64_t;
#endif

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <sched.h>
#include <ctype.h>
#include "debug.h"

#include "boost/preprocessor/seq/for_each.hpp"
//#include "boost/preprocessor/cat.hpp"

#define MIN(X,Y) (((X)<(Y))?(X):(Y))
#define MAX(X,Y) (((X)>(Y))?(X):(Y))

// tell the compiler a branch is/is not likely to be followed
#define LIKELY(x)       __builtin_expect((x),1)
#define UNLIKELY(x)     __builtin_expect((x),0)

#define assert(x) if(UNLIKELY(!(x))) { ERROR("NVP_ASSERT("#x") failed!\n"); exit(100); }

// places quotation marks around arg (eg, MK_STR(stuff) becomes "stuff")
#define MK_STR(arg) #arg
#define MK_STR2(x) MK_STR(x)
#define MK_STR3(x) MK_STR2(x)

#define MACRO_WRAP(a) a
#define MACRO_CAT(a, b) MACRO_WRAP(a##b)


//#define MONETA_DEVICE_PATH "/dev/bbda" 
#define MONETA_CHAR_DEVICE_PATH "/dev/monetaCtrla"
#define SDSSD_CHAR_DEVICE_PATH "/dev/monetaCtrla"
#define MONETA_BLOCK_DEVICE_PATH "/dev/monetaa"
#define SDSSD_BLOCK_DEVICE_PATH "/dev/monetaa"

#define ST_MONETA_DEVICE_ID 252
#define ST_SDSSD_DEVICE_ID 252
#define ST_SDSSD_BANKSHOT_DEVICE_ID 251

#ifndef __cplusplus
typedef int bool;
#define false 0
#define true 1
#endif

extern FILE* _nvp_print_fd;

// maximum number of file operations to support simultaneously
#define MAX_FILEOPS 32


// functions to use when invoking system calls (since the normal ones may be aliased)
//#define ALLOPS_FINITEPARAMS READ WRITE CLOSE SEEK TRUNC DUP DUP2 FORK MMAP READV WRITEV PIPE MUNMAP MSYNC
//#define ALLOPS OPEN IOCTL ALLOPS_FINITEPARAMS

// BOOST_PP only support parenthesis-delimited lists...
// I would have implemented this with BOOST_PP, but <see previous line>
#define OPS_FINITEPARAMS_64 (TRUNC64) (SEEK64)
#define OPS_64 OPS_FINITEPARAMS (OPEN64)
#define ALLOPS_FINITEPARAMS_WPAREN (READ) (WRITE) (CLOSE) (SEEK) (TRUNC) (DUP) (DUP2) (FORK) (READV) (WRITEV) (PIPE) OPS_FINITEPARAMS_64 (PREAD) (PWRITE) (FSYNC) (FDSYNC) (SOCKET) (ACCEPT)
#define ALLOPS_WPAREN (OPEN) (IOCTL) ALLOPS_FINITEPARAMS_WPAREN
// NOTE: clone is missing on purpose.(MMAP) (MUNMAP) (MSYNC) (CLONE) (MMAP64)

#define FILEOPS_WITH_FD (READ) (WRITE) (SEEK) (TRUNC) (READV) (WRITEV) (TRUNC64) (SEEK64) (PREAD) (PWRITE) (FSYNC) (FDSYNC)
//(ACCEPT)
#define FILEOPS_WITHOUT_FD (FORK) (PIPE)
//(SOCKET)


// Every time a function is used, determine whether the module's functions have been resolved.
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
// These aren't resolved at runtime before main() because it's impossible to determine
// RELIABLY which constructor will be called first.
#define CHECK_RESOLVE_FILEOPS_OLDVERSION(NAME) do{ \
	if(UNLIKELY(NAME##fileops==NULL)) { \
		if(NAME##resolve_fileops()!=0){ \
			ERROR("Couldn't resolve " #NAME "!\n"); \
		} \
	} }while(0)

#define CHECK_RESOLVE_FILEOPS(NAME) do{ \
	if(UNLIKELY(NAME##fileops==NULL)) { \
		ERROR("Tried to use "#NAME" fileops, but they weren't initialized!  BLARG\n" FAIL); \
		assert(0); \
	} } while(0)
/*
	if(NAME##fileops==NULL) { usleep(50000); } \
	if(NAME##fileops==NULL) { sleep(1); } \
	if(NAME##fileops==NULL) { \
		DEBUG(#NAME " fileops were not resolved somehow; maybe I can resolve them now...\n"); \
		NAME##resolve_fileops(); \
	} \
*/
/*
#define PRINT_FILEOPS_TABLE \
	DEBUG("Here's what the fileops table looks like: \n"); \
	int _i; \
	for(_i=0; _i<MAX_FILEOPS; _i++) { \
		if(_hub_fileops_lookup[_i]==NULL) { break; } \
		DEBUG("\t%i\t%s\n", _i, _hub_fileops_lookup[_i]->name); \
	}
*/

// Macros to create a Fileops_p structure with pointers for the module,
// then register that Fileops_p structure with the hub.
// Thgis occurs at execution time, before main() is called.
// Fourth argument supported as function call at end of constructor.
#define ADD_FUNCTP(FUNCT, prefix) fo->FUNCT = prefix##FUNCT; 
#define ADD_FUNCTP_IWRAP(r, data, elem) ADD_FUNCTP(elem, data) 

#define INIT_FILEOPS_P(NAME, PREFIX) \
	struct Fileops_p* fo = (struct Fileops_p*) calloc(1, sizeof(struct Fileops_p)); \
	fo->name = NAME; \
	fo->resolve = PREFIX##resolve_fileops; \
	BOOST_PP_SEQ_FOR_EACH(ADD_FUNCTP_IWRAP, PREFIX, ALLOPS_WPAREN) \
	_hub_add_fileop(fo); 

//extern struct Fileops_p* _hub_fileops_lookup[];
#define MODULE_REGISTRATION_F(NAME, PREFIX, ...) \
	extern int OPEN_MAX; \
	struct Fileops_p* PREFIX##fileops; \
	int PREFIX##resolve_fileops(char*); \
	void PREFIX##init(void) __attribute__((constructor)); \
	void PREFIX##init(void) { \
		DEBUG("Initializing " NAME "_init\n"); \
		PREFIX##fileops = NULL; \
		INIT_FILEOPS_P(NAME, PREFIX); \
		if(OPEN_MAX<1) { \
			OPEN_MAX = sysconf(_SC_OPEN_MAX); \
			DEBUG("Maximum simultaneous open files: %i\n", OPEN_MAX); \
		} \
		__VA_ARGS__ \
	} \
	int PREFIX##resolve_fileops(char* tree) { \
		PREFIX##fileops = default_resolve_fileops(tree, NAME); \
		if(PREFIX##fileops) { return 0; } \
		else { \
			ERROR("Failed to resolve "NAME" fileops\n");\
			return -1;\
		} \
	}


struct Fileops_p* default_resolve_fileops(char* tree, char* name);

#include"debug.h"


// Used to determine contents of flags passed to OPEN
#define FLAGS_INCLUDE(flags, x) ((flags&x)||(x==0))
#define DUMP_FLAGS(flags, x) do{ if(FLAGS_INCLUDE(flags, x)) { DEBUG_P("%s(0x%X) ",#x,x); } }while(0)

#define WEAK_ALIAS(a) __attribute__ ((weak, alias(a)))


// Information about the functions which are wrapped by EVERY module
// Alias: the standard function which most users will call
#define ALIAS_OPEN   open
#define ALIAS_READ   read
#define ALIAS_WRITE  write
#define ALIAS_SEEK   lseek
#define ALIAS_CLOSE  close
#define ALIAS_TRUNC  ftruncate
#define ALIAS_DUP    dup
#define ALIAS_DUP2   dup2
#define ALIAS_FORK   fork
#define ALIAS_MMAP   mmap
#define ALIAS_READV  readv
#define ALIAS_WRITEV writev
#define ALIAS_PIPE   pipe
#define ALIAS_IOCTL  ioctl
#define ALIAS_MUNMAP munmap
#define ALIAS_MSYNC  msync
#define ALIAS_CLONE  __clone
#define ALIAS_PREAD  pread
#define ALIAS_PWRITE pwrite
#define ALIAS_FSYNC  fsync
#define ALIAS_FDSYNC fdatasync
#define ALIAS_TRUNC64 ftruncate64
#define ALIAS_OPEN64  open64
#define ALIAS_SEEK64  lseek64
#define ALIAS_MMAP64  mmap64
#define ALIAS_MKSTEMP mkstemp
#define ALIAS_ACCEPT  accept
#define ALIAS_SOCKET  socket

// The function return type
#define RETT_OPEN   int
#define RETT_READ   ssize_t
#define RETT_WRITE  ssize_t
#define RETT_SEEK   off_t
#define RETT_CLOSE  int
#define RETT_TRUNC  int
#define RETT_DUP    int
#define RETT_DUP2   int
#define RETT_FORK   pid_t
#define RETT_MMAP   void*
#define RETT_READV  ssize_t
#define RETT_WRITEV ssize_t
#define RETT_PIPE   int
#define RETT_IOCTL  int
#define RETT_MUNMAP int
#define RETT_MSYNC  int
#define RETT_CLONE  int
#define RETT_PREAD  ssize_t
#define RETT_PWRITE ssize_t
#define RETT_FSYNC  int
#define RETT_FDSYNC int
#define RETT_TRUNC64 int
#define RETT_OPEN64  int
#define RETT_SEEK64  off64_t
#define RETT_MMAP64  void*
#define RETT_MKSTEMP int
#define RETT_ACCEPT  int
#define RETT_SOCKET  int


// The function interface
#define INTF_OPEN   const char* path, int oflag, ...
#define INTF_READ   int file,       void* buf, size_t length
#define INTF_WRITE  int file, const void* buf, size_t length
#define INTF_SEEK   int file, off_t offset, int whence
#define INTF_CLOSE  int file
#define INTF_TRUNC  int file, off_t length
#define INTF_DUP    int file
#define INTF_DUP2   int file, int fd2
#define INTF_FORK   void
#define INTF_MMAP   void *addr, size_t len, int prot, int flags, int file, off_t off
#define INTF_READV  int file, const struct iovec *iov, int iovcnt
#define INTF_WRITEV int file, const struct iovec *iov, int iovcnt
#define INTF_PIPE   int file[2]
#define INTF_IOCTL  int file, int request, ...
#define INTF_MUNMAP void *addr, size_t len
#define INTF_MSYNC  void *addr, size_t len, int flags
#define INTF_CLONE  int (*fn)(void *a), void *child_stack, int flags, void *arg
#define INTF_PREAD  int file,       void *buf, size_t count, off_t offset
#define INTF_PWRITE int file, const void *buf, size_t count, off_t offset
#define INTF_FSYNC  int file
#define INTF_FDSYNC int file
#define INTF_TRUNC64 int file, off64_t length
#define INTF_OPEN64  const char* path, int oflag, ...
#define INTF_SEEK64  int file, off64_t offset, int whence
#define INTF_MMAP64  void *addr, size_t len, int prot, int flags, int file, off64_t off
#define INTF_MKSTEMP char* file
#define INTF_ACCEPT  int file, struct sockaddr *addr, socklen_t *addrlen
#define INTF_SOCKET  int domain, int type, int protocol

// The interface, without types.  Used when calling from inside macros.
// CALL_ names must match INTF_ names.
#define CALL_OPEN   path, oflag
#define CALL_IOCTL  file, request
#define CALL_READ   file, buf, length
#define CALL_WRITE  file, buf, length
#define CALL_SEEK   file, offset, whence
#define CALL_CLOSE  file
#define CALL_TRUNC  file, length
#define CALL_DUP    file
#define CALL_DUP2   file, fd2
#define CALL_FORK   
#define CALL_MMAP   addr, len, prot, flags, file, off
#define CALL_READV  file, iov, iovcnt
#define CALL_WRITEV file, iov, iovcnt
#define CALL_PIPE   file
#define CALL_MUNMAP addr, len
#define CALL_MSYNC  addr, len, flags
#define CALL_CLONE  fn, child_stack, flags, arg
#define CALL_PREAD  file, buf, count, offset
#define CALL_PWRITE file, buf, count, offset
#define CALL_FSYNC  file
#define CALL_FDSYNC file
#define CALL_TRUNC64 CALL_TRUNC
#define CALL_OPEN64  CALL_OPEN
#define CALL_SEEK64  CALL_SEEK
#define CALL_MMAP64  CALL_MMAP
#define CALL_MKSTEMP file
#define CALL_ACCEPT  file, addr, addrlen
#define CALL_SOCKET  domain, type, protocol

// A format string for printf on the parameters
#define PFFS_OPEN   "%s, %i"
#define PFFS_READ   "%i, %p, %i"
#define PFFS_WRITE  "%i, %p, %i"
#define PFFS_SEEK   "%i, %i, %i"
#define PFFS_CLOSE  "%i"
#define PFFS_TRUNC  "%i, %i"
#define PFFS_DUP    "%i"
#define PFFS_DUP2   "%i, %i"
#define PFFS_FORK   ""
#define PFFS_MMAP   "%p, %i, %i, %i, %i"
#define PFFS_READV  "%i, %p, %i"
#define PFFS_WRITEV "%i, %p, %i"
#define PFFS_PIPE   "%p"
#define PFFS_IOCTL  "%i, %i"
#define PFFS_MUNMAP "%p, %i"
#define PFFS_MSYNC  "%p, %i, %i"
#define PFFS_CLONE  "%p, %p, %i, %p"
#define PFFS_PREAD  "%i, %p, %i, %i"
#define PFFS_PWRITE "%i, %p, %i, %i"
#define PFFS_FSYNC  "%i"
#define PFFS_FDSYNC "%i"
#define PFFS_TRUNC64 "%i, %i"
#define PFFS_OPEN64  "%s, %i"
#define PFFS_SEEK64  "%i, %i, %i"
#define PFFS_MMAP64  "%p, %i, %i, %i, %i"
#define PFFS_MKSTEMP "%s"
#define PFFS_ACCEPT  "%d, %p, %p"
#define PFFS_SOCKET  "%d, %d, %d"


// STD: the lowest (non-weak alias) version used by gcc
#define STD_OPEN   __open
#define STD_OPEN64 __open64
#define STD_READ   __read
#define STD_WRITE  __write
#define STD_SEEK   __lseek
#define STD_SEEK64 __lseek64
#define STD_CLOSE  __close
#define STD_TRUNC  __ftruncate
#define STD_TRUNC64 __ftruncate64
#define STD_DUP    __dup
#define STD_DUP2   __dup2
#define STD_FORK   __fork
#define STD_MMAP   __mmap
#define STD_MMAP64 __mmap64
#define STD_READV  __readv
#define STD_WRITEV __writev
#define STD_PIPE   __pipe
#define STD_IOCTL  __ioctl
#define STD_MUNMAP __munmap
#define STD_MSYNC  __libc_msync
#define STD_CLONE  __clone
#define STD_PREAD  __pread
#define STD_PWRITE __pwrite
#define STD_FSYNC  __fsync
#define STD_FDSYNC __fdsync
#define STD_MKSTEMP mkstemp




// declare as extern all the standard functions listed in ALLOPS
#define STD_DECL(FUNCT) extern RETT_##FUNCT STD_##FUNCT ( INTF_##FUNCT ) ;
#define STD_DECL_IWRAP(r, data, elem) STD_DECL(elem)

//BOOST_PP_SEQ_FOR_EACH(STD_DECL_IWRAP, placeholder, ALLOPS_WPAREN);

struct Fileops_p {
	char* name;
	int (*resolve) (char*);
	// add a pointer for each supported operation type
	#define ADD_FILEOP(op) RETT_##op (* op ) ( INTF_##op ) ;
	#define ADD_FILEOP_IWRAP(r, data, elem) ADD_FILEOP(elem)
	BOOST_PP_SEQ_FOR_EACH(ADD_FILEOP_IWRAP, placeholder, ALLOPS_WPAREN); 
};



// These functions are used to manage the standard directory of available functions.
// This directory lives in _hub_.
void _hub_add_fileop(struct Fileops_p* fo);
struct Fileops_p* _hub_find_fileop(const char* name);

void _hub_resolve_all_fileops(char* tree);

// method used by custom module resolvers which will extract module names of
// direct decendents.
struct Fileops_p** resolve_n_fileops(char* tree, char* name, int count);


// Used to declare and set up aliasing for the functions in a module.
// Can be called with ALLOPS_WPAREN or ALLOPS_FINITEPARAMS_WPAREN, for example.
#define DECLARE_AND_ALIAS_FUNCS(FUNCT, prefix) \
	RETT_##FUNCT prefix##FUNCT(INTF_##FUNCT); \
	RETT_##FUNCT ALIAS_##FUNCT(INTF_##FUNCT) WEAK_ALIAS(MK_STR(prefix##FUNCT));
#define DECLARE_AND_ALIAS_FUNCTS_IWRAP(r, data, elem) DECLARE_AND_ALIAS_FUNCS(elem, data)


// Same as above, but used to declare without aliasing for all the functions in a module.
// _hub_ is the only module which actually does external aliasing.
#define DECLARE_WITHOUT_ALIAS_FUNCS(FUNCT, prefix) \
	RETT_##FUNCT prefix##FUNCT(INTF_##FUNCT);
#define DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP(r, data, elem) DECLARE_WITHOUT_ALIAS_FUNCS(elem, data)


// Used to fill in the blanks for functions which aren't implemented in a module.
// The module's .c file should have a list of functions which aren't implemented
// which gets passed in.
#define WRAP_NOT_IMPLEMENTED(op, prefix) \
	RETT_##op prefix##op ( INTF_##op ) { \
		DEBUG("CALL: " MK_STR(prefix##op) " not implemented!\n"); \
		assert(0); \
	}
#define WRAP_NOT_IMPLEMENTED_IWRAP(r, data, elem) WRAP_NOT_IMPLEMENTED(elem, data) 

#define RESOLVE_TWO_FILEOPS(MODULENAME, OP1, OP2) \
	DEBUG("Resolving module "MODULENAME": wants two fileops.\n"); \
	struct Fileops_p** result = resolve_n_fileops(tree, MODULENAME, 2); \
	OP1 = result[0]; \
	OP2 = result[1]; \
	if(OP1 == NULL) { \
		ERROR("Failed to resolve "#OP1"\n"); \
		assert(0); \
	} else { \
		DEBUG(MODULENAME"("#OP1") resolved to %s\n", OP1->name); \
	} \
	if(OP2 == NULL) { \
		ERROR("Failed to resolve "#OP2"\n"); \
		assert(0); \
	} else { \
		DEBUG(MODULENAME"("#OP2") resolved to %s\n", OP2->name); \
	}

#endif

// breaking the build
