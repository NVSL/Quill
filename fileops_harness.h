
// Header file shared by nvmfileops.c and fileops_compareharness.c

#ifndef __NV_COMPAREHARNESS_H_
#define __NV_COMPAREHARNESS_H_

#include "nv_common.h"

#define ENV_REF_IMPL "NVP_HARNESS_REF_FOP"
#define ENV_TEST_IMPL "NVP_HARNESS_TEST_FOP"

#define DO_PARANOID_FILE_CHECK 0

// declare and alias all the functions in ALLOPS
BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _harness_, ALLOPS_WPAREN)

int _harness_compare_files(int f1, int f2, const char* name);

int _testspecific_resolve_fileops(char* ref, char* test);
void _harness_init2(void);

int _harness_specific_resolver_wrap(char* tree);


// if fileops haven't been resolved yet, look through the list and assign them
// _harness_ uses two fileops_i indices, so it has a special function
#define TEST_CHECK_RESOLVE_FILEOPS_OLD do{ \
	if(fileops_ref==NULL||fileops_test==NULL) { \
		if(_testspecific_resolve_fileops( \
			getenv(ENV_REF_IMPL), \
			getenv(ENV_TEST_IMPL) \
			)!=0) \
			{ return -1; } \
		} \
	}while(0)

#define TEST_CHECK_RESOLVE_FILEOPS do{ \
	if(fileops_ref==NULL||fileops_test==NULL) { \
		ERROR("fileops_ref or fileops_test wasn't resolved!\n"); assert(0); \
	} }while(0)

// Utility macros used to compare results from the STD and UUT functions
#define TEST_COMPARE(name) do{ \
	if(std_result != uut_result) { \
		ERROR("_harness_%s: std_result (%i) != uut_result (%i)\n", name, std_result, uut_result);\
	} \
	if(std_error_result != errno) { \
		ERROR("_harness_%s: errno std (%i) does not match errno uut (%i)\n", name, std_error_result, errno); \
	} \
	errno = std_error_result; \
	if (DO_PARANOID_FILE_CHECK) { \
	if(strcmp(name,"close")!=0){_harness_compare_files(std_file, uut_file, "_harness_" name " (after)");} \
	} \
	} while(0)

#define TEST_COMPARE_EQ(name) do{ \
	if(std_result == uut_result) { \
		ERROR("_harness_%s: std_result (%i) != uut_result (%i)\n", name, std_result, uut_result);\
	} \
	if(std_error_result != errno) { \
		ERROR("_harness_%s: errno std (%i) does not match errno uut (%i)\n", name, std_error_result, errno); \
	} \
	errno = std_error_result; \
	if (DO_PARANOID_FILE_CHECK) { \
	if(strcmp(name,"close")!=0){_harness_compare_files(std_file, uut_file, "_harness_" name " (after)");} \
	} \
	} while(0)


#endif

