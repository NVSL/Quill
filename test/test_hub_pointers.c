
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "test_common.h"


extern struct Fileops_p* _hub_fileops_lookup[];

extern struct Fileops_p* _hub_fileops;

struct Fileops_p* _hub_find_fileop(const char* name);

#define ENV_HUB_FOP "NVP_HUB_FOP"

int do_test_eval(int argc, char* argv[])
{
	open("fake", O_RDWR); // pointers are resolved at first call

	struct Fileops_p * env_fileops = _hub_find_fileop(getenv(ENV_HUB_FOP));
	struct Fileops_p * hub_fileops = _hub_fileops;

	if(env_fileops != hub_fileops) {
		ERROR("env_fileops != hub_fileops (%p != %p\n", env_fileops, hub_fileops);
		ERROR("If there aren't more errors below this, that's probably even worse!\n");
	}

	//(%p != %p)\n", hub_fileops->NAME != env_fileops->NAME);

	#define TEST_FOP(NAME) do{ \
		if(hub_fileops->NAME == env_fileops->NAME) \
		{ \
			MSG("hub_fileops[" #NAME "] == env_fileops[" #NAME "]\n");\
		} else { \
			ERROR("hub_fileops["#NAME"] != env_fileops[" #NAME "]\n");\
		} }while(0); 

	#define TEST_FOP_IWRAP(r, data, elem) TEST_FOP(elem)

	BOOST_PP_SEQ_FOR_EACH(TEST_FOP_IWRAP, placeholder, ALLOPS_WPAREN (name))

	MSG("Note: this test only tests pointers in hub.\n");

	return 0;
}

