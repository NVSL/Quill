// a module which counts calls to each function

#include "nv_common.h"
#include "perfcount.h"

#define ENV_COUNT_FOP "NVP_COUNT_FOP"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _perfcount_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _perfcount_OPEN(INTF_OPEN);
RETT_IOCTL _perfcount_IOCTL(INTF_IOCTL);

MODULE_REGISTRATION_F("perfcount", _perfcount_)


#define COUNTVAR(FUNCT) stat_per_cpu FUNCT##_stat MY_ALIGNED;
#define COUNTVAR_IWRAP(r, data, elem) COUNTVAR(elem)

BOOST_PP_SEQ_FOR_EACH(COUNTVAR_IWRAP, x, ALLOPS_WPAREN)


#define COUNT_WRAP(FUNCT) \
	RETT_##FUNCT _perfcount_##FUNCT(INTF_##FUNCT) { \
		CHECK_RESOLVE_FILEOPS(_perfcount_); \
		RETT_##FUNCT ret; \
		timing_t start_time = perf_start_timing(); \
		ret = _perfcount_fileops->FUNCT(CALL_##FUNCT); \
		perf_end_timing(FUNCT##_stat, start_time); \
		return ret; \
	}
#define COUNT_WRAP_IWRAP(r, data, elem) COUNT_WRAP(elem)

BOOST_PP_SEQ_FOR_EACH(COUNT_WRAP_IWRAP, x, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _perfcount_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_perfcount_);

	RETT_OPEN ret;

	timing_t start_time = perf_start_timing();
	
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		ret = _perfcount_fileops->OPEN(path, oflag, mode);
	} else {
		ret = _perfcount_fileops->OPEN(path, oflag);
	}

	perf_end_timing(OPEN_stat, start_time);

	return ret;
}

RETT_IOCTL _perfcount_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_perfcount_);
	
	timing_t start_time = perf_start_timing();
	
	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _perfcount_fileops->IOCTL(file, request, third);

	perf_end_timing(IOCTL_stat, start_time);
	
	return result;
}

void _perfcount_init2(void) __attribute__((constructor));
void _perfcount_init2(void)
{
	#define COUNT_INIT(FUNCT) perf_clear_stat(FUNCT##_stat);
	#define COUNT_INIT_IWRAP(r, data, elem) COUNT_INIT(elem)
	BOOST_PP_SEQ_FOR_EACH(COUNT_INIT_IWRAP, x, ALLOPS_WPAREN)
}

void _perfcount_print(void) __attribute__((destructor));
void _perfcount_print(void)
{
	MSG("_perfcount_: Here are the function counts:\n");
	#define COUNT_PRINT(FUNCT) perf_print_stat(NVP_PRINT_FD, FUNCT##_stat, #FUNCT);
	#define COUNT_PRINT_IWRAP(r, data, elem) COUNT_PRINT(elem)
	BOOST_PP_SEQ_FOR_EACH(COUNT_PRINT_IWRAP, x, ALLOPS_WPAREN)
}

