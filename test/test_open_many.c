
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"


int do_test_eval(int argc, char* argv[])
{
	char* filename;
	char* buf = (char*) calloc(200, sizeof(char));

	int result;

	int i;
	for(i=0; i<30; i++)
	{
		buf[i] = '0'+(i%10);
	}
	
	struct stat st;
	
	filename = DEFAULT_FILENAME1;

	MSG("Testing open(2) on file \"%s\"\n", filename);

	CHECK_LEN(filename, "Hello");

	DEBUG("O_RDONLY %i\nO_WRONLY %i\nO_RDWR %i\n", O_RDONLY, O_WRONLY, O_RDWR);

	for(i=0; i<500; i++)
	{
		MSG("Attempting open(%s, %i): read? %s   write? %s\n", filename, i, ((FLAGS_INCLUDE(i, O_RDWR)||FLAGS_INCLUDE(i, O_RDONLY))?"YES":"NO"), ((FLAGS_INCLUDE(i, O_RDWR)||FLAGS_INCLUDE(i, O_WRONLY))?"YES":"NO") );
		result = open(filename, i);
		if(result < 0)
		{
			ERROR("Failed to open file on iteration %i\n", i);
		}
		else
		{
			close(result);
		}
	}

	CHECK_LEN(filename, "Hello");

	for(i=0; i<500; i++)
	{
		result = open(filename, O_RDONLY, i);
		if(result < 0)
		{
			ERROR("Failed to open file on iteration %i\n", i);
		}
		else
		{
			close(result);
		}
	}

	for(i=0; i<500; i++)
	{
		result = open(filename, O_RDWR, i);
		if(result < 0)
		{
			ERROR("Failed to open file on iteration %i\n", i);
		}
		else
		{
			close(result);
		}
	}

	for(i=0; i<500; i++)
	{
		result = open(filename, O_WRONLY, i);
		if(result < 0)
		{
			ERROR("Failed to open file on iteration %i\n", i);
		}
		else
		{
			close(result);
		}
	}

	MSG("Done.\n");

	return 0;
}

