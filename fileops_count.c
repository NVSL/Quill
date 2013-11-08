// a module which counts calls to each function

#include "nv_common.h"

#define ENV_COUNT_FOP "NVP_COUNT_FOP"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _count_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _count_OPEN(INTF_OPEN);
RETT_IOCTL _count_IOCTL(INTF_IOCTL);

MODULE_REGISTRATION_F("count", _count_)


#define COUNTVAR(FUNCT) int FUNCT##_count;
#define COUNTVAR_IWRAP(r, data, elem) COUNTVAR(elem)

BOOST_PP_SEQ_FOR_EACH(COUNTVAR_IWRAP, x, ALLOPS_WPAREN)


#define COUNT_WRAP(FUNCT) \
	RETT_##FUNCT _count_##FUNCT(INTF_##FUNCT) { \
		CHECK_RESOLVE_FILEOPS(_count_); \
		FUNCT##_count++; \
		return _count_fileops->FUNCT(CALL_##FUNCT); \
	}
#define COUNT_WRAP_IWRAP(r, data, elem) COUNT_WRAP(elem)

BOOST_PP_SEQ_FOR_EACH(COUNT_WRAP_IWRAP, x, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _count_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_count_);
	OPEN_count++;
	
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		return _count_fileops->OPEN(path, oflag, mode);
	} else {
		return _count_fileops->OPEN(path, oflag);
	}
}

RETT_IOCTL _count_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_count_);
	IOCTL_count++;
	
	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _count_fileops->IOCTL(file, request, third);

	return result;
}

void _count_print(void) __attribute__((destructor));
void _count_print(void)
{
	MSG("_count_: Here are the function counts:\n");
	#define COUNT_PRINT(FUNCT) MSG("%7s: %3i \n", #FUNCT, FUNCT##_count);
	//#define COUNT_PRINT(FUNCT) MSG(#FUNCT": %u\n", FUNCT##_count);
	#define COUNT_PRINT_IWRAP(r, data, elem) COUNT_PRINT(elem)
	BOOST_PP_SEQ_FOR_EACH(COUNT_PRINT_IWRAP, x, ALLOPS_WPAREN)

	fflush(stdout);
}

