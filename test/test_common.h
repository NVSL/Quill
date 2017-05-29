#ifndef __TEST_COMMON_H_
#define __TEST_COMMON_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
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
#include <assert.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <ctype.h>
/*
#include "../debug.h"

#undef ERROR
#undef DEBUG
*/

extern int _nv_error_count;

#define ERROR printf
#define DEBUG printf
#define MSG printf

#define DEFAULT_FILENAME1 "nvmfileops_test1.txt"
#define DEFAULT_FILENAME2 "nvmfileops_test2.txt"
#define DEFAULT_FILENAME3 "nvmfileops_test3.txt"
#define DEFAULT_FILENAME4 "nvmfileops_test4.txt"
#define DEFAULT_FILENAME5 "nvmfileops_test5.txt"
#define DEFAULT_FILENAME6 "nvmfileops_test6.txt"
#define DEFAULT_FILENAME7 "nvmfileops_test7.txt"
#define DEFAULT_FILENAME8 "nvmfileops_test8.txt"

#define CHECK_LEN_INT(filename, expected) do{ if(stat(filename, &st)) { ERROR("failed to get file stats for %s\n", #filename); } else if(st.st_size != expected) { ERROR("expected file length %i, got %i\n", expected, (int)st.st_size); } else { DEBUG("Harness saw file size %i, as expected\n", expected); } }while(0)
#define CHECK_LEN(filename, expected) do{ CHECK_LEN_INT(filename, ((int)strlen(expected))); }while(0)
#define CHECK_WRITE(result, expected) do{ if((size_t)result != strlen(expected) ) { ERROR( "Didn't write %i characters! (wrote %i)\n", (int)strlen(expected), (int)result); } }while(0)
#define CHECK_BIN_RRESULT(buf, expected,len)  do{ MSG("Comparing \"%s\" to \"%s\"\n", buf, expected); if(memcmp(buf,expected,len)) { ERROR("Didn't get expected file contents.\n"); } }while(0)
#define CHECK_BIN_READ(result, expected, len)  do{ if((size_t)result != len ) { ERROR("Didn't read %i characters! (read %i)\n", (int)len, (int)result); CHECK_BIN_RRESULT(buf, expected, len); } }while(0)
#define CHECK_RRESULT(buf, expected)  do{ MSG("Comparing \"%s\" to \"%s\"\n", buf, expected); if(strcmp(buf,expected)) { ERROR("Didn't get expected file contents.\n"); } }while(0)
#define CHECK_READ(result, expected)  do{ if((size_t)result != strlen(expected) ) { ERROR("Didn't read %i characters! (read %i)\n", (int)strlen(expected), (int)result); CHECK_RRESULT(buf, expected); } }while(0)
#define CHECK_SEEK(result, expected)  do{ MSG("Seeking %i\n", expected); if(result != expected) { ERROR("Seek didn't go to %i (went to %i)\n", expected, (int)result); } }while(0)
#define SET_BUFFER(new) do{ memset(buf, '\0', 200); memcpy(buf, new, strlen(new)); MSG("Changing the buffer to \"%s\"\n", buf); }while(0)
#define CLEAR_BUFFER do{ SET_BUFFER("\0"); }while(0)
#define CHECK_CLOSE(result) do{ if(result) { ERROR("failed to close file: %s\n", strerror(errno)); } else { MSG("Closed file.\n"); } }while(0)
#define CHECK_TRUNC(result, expected) do{ MSG("Truncating file to length %i\n", expected); if(result) { ERROR("truncate failed.\n"); } }while(0)
#define CHECK_FD(file) do{ if(file<2) { ERROR("open failed: got fd %i\n", file); } }while(0)

extern void HarnessInit(char* name);
extern void HarnessSuccess(void);
extern void HarnessFail(void);



extern char **theEnvp;
extern char **theArgv;
extern int theArgc;

#endif

