// a module which just wraps up standard functions

#include "nv_common.h"

#define ENV_WRAP_FOP "NVP_WRAP_FOP"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _wrap_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _wrap_OPEN(INTF_OPEN);
RETT_IOCTL _wrap_IOCTL(INTF_IOCTL);

#define WRAP_PRINT_F(...) PRINTFUNC(stdout, __VA_ARGS__)

#define ALLOPS_FINITEPARAMS_WPAREN_NOFORK (READ) (WRITE) (CLOSE) (SEEK) (TRUNC) (DUP) (DUP2) (READV) (WRITEV) (PIPE) OPS_FINITEPARAMS_64 (PREAD) (PWRITE) (FSYNC) (FDSYNC) (SOCKET) (ACCEPT) (UNLINK) (UNLINKAT)


MODULE_REGISTRATION_F("wrap", _wrap_)

// create wrapper functions for each function in ALLOPS_FINITEPARAMS
// (our macros don't support __VA_ARGS__ functions, so they're implemented by hand)
#define WRAP(op) \
	RETT_##op _wrap_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_wrap_); \
		WRAP_PRINT_F("CALL: "MK_STR(_wrap_##op)" is calling \"%s\"->"#op"("PFFS_##op")\n", _wrap_fileops->name, CALL_##op); \
		return _wrap_fileops->op( CALL_##op ); \
	}
#define WRAP_IWRAP(r, data, elem) WRAP(elem)
BOOST_PP_SEQ_FOR_EACH(WRAP_IWRAP, placeholder, ALLOPS_FINITEPARAMS_WPAREN_NOFORK)

int _wrap_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_wrap_);

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		
		WRAP_PRINT_F("CALL: _wrap_OPEN is calling \"%s\"->OPEN("PFFS_OPEN", %i)\n", _wrap_fileops->name, CALL_OPEN, mode);
	
		return _wrap_fileops->OPEN(path, oflag, mode);
	}
	else
	{
		WRAP_PRINT_F("CALL: _wrap_OPEN is calling \"%s\"->OPEN("PFFS_OPEN")\n", _wrap_fileops->name, CALL_OPEN);
	
		return _wrap_fileops->OPEN(path, oflag);
	}
}

int _wrap_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_wrap_);

	WRAP_PRINT_F("CALL: _wrap_IOCTL("PFFS_IOCTL")\n", CALL_IOCTL);
	
	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _wrap_fileops->IOCTL(file, request, third);

	return result;
}

RETT_FORK _wrap_FORK ( INTF_FORK ) {
	CHECK_RESOLVE_FILEOPS(_wrap_);
	WRAP_PRINT_F("CALL: _wrap_FORK is calling \"%s\"->FORK()\n", _wrap_fileops->name);
	return _wrap_fileops->FORK( CALL_FORK );
}

