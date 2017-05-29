
// A wrapper harness for the test suite.

#include "test_common.h"
#include "boost/preprocessor/seq/for_each.hpp"

extern FILE* _nvp_print_fd;

char *_harness_name;
int _harness_status = 0;

void PrintHarnessStatus(void);
void HarnessSuccess(void);
void HarnessFail(void);

extern int do_test_eval(int argc, char* argv[]);

char **theEnvp = NULL;
char **theArgv = NULL;
int theArgc = 0;

void _exit(int status);
void  exit(int status);
void  exit(int status) __attribute__ ((weak, alias(("nv_test_exit"))));
void nv_test_exit(int status)
{
	fprintf(stdout, "HARNESS: Intercepting an exit(%i)\n", status); fflush(stdout);
	if( (status==0) && (_nv_error_count==0) ) {
		HarnessSuccess();
	} else {
		fprintf(stdout, "That's a failure: exit status %i\t_nv_error_count %i\n", status, _nv_error_count);
	}
	PrintHarnessStatus();
	_exit(status);
}

int main(int argc, char* argv[], char *envp[])
{
	theEnvp = envp;
	theArgv = argv;
	theArgc = argc;

	MSG("Welcome to test module %s\n", argv[0]);
	_nv_error_count = 0;

/*
	MSG("Here are the environment varialbes:\n");
	#define ENV_VARS ("NVP_HUB_FOP") ("NVP_WRAP_FOP") ("NVP_FILTER_MANAGED_FOP") ("NVP_FILTER_UNMANAGED_FOP") ("NVP_DEATH_FOP") ("NVP_HARNESS_REF_FOP") ("NVP_HARNESS_TEST_FOP") ("NVP_NV_FOP") ("NVP_MONETA_FOP") ("NVP_FILTER_OVERRIDE")
	#define PRINT_ENV_VAR(r, data, elem) MSG("%s: %s\n", elem, getenv(elem));
	BOOST_PP_SEQ_FOR_EACH(PRINT_ENV_VAR, xxx, ENV_VARS)
*/

	_harness_name = (char*) calloc(strlen(argv[0])+1, sizeof(char));
	strcpy(_harness_name, argv[0]);
	MSG("Registering atexit(PrintHarnessStatus) for %s\n", _harness_name);
	atexit(PrintHarnessStatus);

	MSG("Running tests...\n");

	int result = do_test_eval(argc, argv);
	
	MSG("All tests completed.\n");
	MSG("Total errors: %i\n", _nv_error_count);

	if(_nv_error_count != 0) {
		MSG("Reporting failure.\n");
		HarnessFail();
	} else {
		MSG("Reporting success.\n");
		HarnessSuccess();
	}

	MSG("Goodbye!\n");

	return result;
}

void PrintHarnessStatus(void)
{
	if (_harness_status) {
		fprintf(stdout, "%s: RESULT: SUCCESS\n", _harness_name);
	} else {
		fprintf(stdout, "%s: RESULT: FAILURE\n", _harness_name);
	}
	fflush(stdout);
}

void HarnessSuccess(void)
{
	_harness_status = 1;
}

void HarnessFail(void)
{
	// this space intentionally left blank
}

