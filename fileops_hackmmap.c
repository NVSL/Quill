
// a module which just hackmmaps up standard functions

#include "nv_common.h"

#define ENV_HACKMMAP_FOP "NVP_HACKMMAP_FOP"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _hackmmap_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _hackmmap_OPEN(INTF_OPEN);
RETT_IOCTL _hackmmap_IOCTL(INTF_IOCTL);

#define HACKMMAP_PRINT_F(...) PRINTFUNC(stdout, __VA_ARGS__)


MODULE_REGISTRATION_F("hackmmap", _hackmmap_)

// create hackmmapper functions for each function in ALLOPS_FINITEPARAMS
// (our macros don't support __VA_ARGS__ functions, so they're implemented by hand)
#define HACKMMAP(op) \
	RETT_##op _hackmmap_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_hackmmap_); \
		HACKMMAP_PRINT_F("CALL: "MK_STR(_hackmmap_##op)" is calling \"%s\"->"#op"\n", _hackmmap_fileops->name); \
		return _hackmmap_fileops->op( CALL_##op ); \
	}
#define HACKMMAP_IHACKMMAP(r, data, elem) HACKMMAP(elem)
BOOST_PP_SEQ_FOR_EACH(HACKMMAP_IHACKMMAP, placeholder, ALLOPS_FINITEPARAMS_WPAREN)

int _hackmmap_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_hackmmap_);

	HACKMMAP_PRINT_F("CALL: _hackmmap_OPEN is calling \"%s\"->OPEN\n", _hackmmap_fileops->name);
	
	int open_result;
	void* mmap_result;

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		open_result = _hackmmap_fileops->OPEN(path, oflag, mode);
	} else {
		open_result = _hackmmap_fileops->OPEN(path, oflag);
	}

	int flags = 0;
	
	if(FLAGS_INCLUDE(oflag, O_RDWR))
	{
		flags = PROT_READ | PROT_WRITE;
	}
	else if(FLAGS_INCLUDE(oflag, O_WRONLY))
	{
		flags = PROT_WRITE;
	}
	else if(FLAGS_INCLUDE(oflag, O_RDONLY))
	{
		flags = PROT_READ;
	}
	else
	{
		DEBUG("Flags don't include O_RDWR or O_WRONLY or O_RDONLY: how the hell did this pass open?\n");
		assert(0);
	}

	mmap_result = mmap(
		0,
		4096,
		flags,
		MAP_SHARED,
		open_result,
		0
	);

	
	if( mmap_result == MAP_FAILED || mmap_result == NULL )
	{
		DEBUG("Weird corner case achieved.  Returning normal failure.\n");
		return -1;
	}

	munmap(mmap_result, 4096); // don't really care if this succeeds or not

	return open_result;
}

int _hackmmap_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_hackmmap_);

	HACKMMAP_PRINT_F("CALL: _hackmmap_IOCTL\n");
	
	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _hackmmap_fileops->IOCTL(file, request, third);

	return result;
}

