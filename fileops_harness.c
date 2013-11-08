// Comparison Test harness which compares the result of fileops_ref with fileops_std for any given modules.

#include "fileops_harness.h"

int*   _harness_fd_lookup; // maps std->fd to uut->fd
char** _fd_path_lookup; // maps fd to its original path

int* _harness_fd_valid; // set when returned from open or dup or dup2, cleared when passed to close or fd2 of dup2
	// if  0, the fd is not valid
	// if  1, the fd is valid
	// if -1, the fd is valid, but THE SHADOW DESCRIPTOR IS NOT

struct Fileops_p *fileops_ref;
struct Fileops_p *fileops_test;

MODULE_REGISTRATION_F("harness", _harness_, _harness_init2();)



int once = 1;


int _print_file_stats(int fd)
{
	char* path = _fd_path_lookup[fd];

	int result = -1337;


	struct stat file_st;
	if(stat(path, &file_st))
	{
		DEBUG("Failed to stat %s\n", path);
		//assert(0);
	}
	else
	{
		result = _hub_find_fileop("posix")->SEEK(fd, 0, SEEK_CUR);
		DEBUG("pos in file %s: %i\n", path, result);
		DEBUG("file len %i\n", file_st.st_size);

//		if(once && file_st.st_size == 105766836) { _nvp_debug_handoff(); once = 0;}
	}
//	return result;
	return file_st.st_size;
}

struct stat g_stat;

#define TEST_WRAP(FUNC) \
	RETT_##FUNC _harness_##FUNC(INTF_##FUNC) { \
		TEST_CHECK_RESOLVE_FILEOPS;\
		RETT_##FUNC (*std_##FUNC)(INTF_##FUNC) = fileops_ref->FUNC;  \
		RETT_##FUNC (*uut_##FUNC)(INTF_##FUNC) = fileops_test->FUNC; \
		if( (file<0) || (file>=OPEN_MAX) ) { \
			DEBUG("_harness_"#FUNC": That base file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file); \
			errno = EBADF; \
			return -1; \
		} \
		if(_harness_fd_lookup[file]<0) { \
			DEBUG("_harness_"#FUNC": That shadow descriptor (%i) is invalid: perhaps you didn't call open first?\n", _harness_fd_lookup[file]); \
			errno = EBADF; \
			return -1; \
		} \
		int std_file = file; \
		int uut_file = _harness_fd_lookup[std_file]; \
		if(!_harness_fd_valid[std_file]) { \
			DEBUG("std_file1 wasn't valid, so we're just going to return whatever std would return.\n"); \
			return std_##FUNC(CALL_##FUNC); \
		} \
		RETT_##FUNC std_result, uut_result; \
		int std_error_result; \
		DEBUG("CALL: _harness_" #FUNC " on %i (std) and %i (test)\n", std_file, uut_file); \
		if( (file<0) || (file>=OPEN_MAX)) { \
			DEBUG("input file descriptor %i is out of range!\n", std_file); \
			return -1; \
		} \
		assert(uut_file >=0); \
		assert(uut_file < OPEN_MAX); \
		assert(_fd_path_lookup[std_file]!=NULL); assert(strlen(_fd_path_lookup[std_file]) > 0); \
		assert(_fd_path_lookup[uut_file]!=NULL); assert(strlen(_fd_path_lookup[uut_file]) > 0); \
		assert(_harness_fd_valid[std_file]==1); \
		assert(_harness_fd_valid[uut_file]==1); \
		_harness_compare_files(std_file, uut_file, "_harness_" #FUNC " (before)\n"); \
		int file_temp = file; \
		file = std_file; \
		if(_print_file_stats(std_file) != _print_file_stats(uut_file) ) { \
			WARNING("File lengths don't match before "#FUNC": this may be OK if this is multithreaded\n"); \
		} \
		errno = 0; \
		std_result = std_##FUNC(CALL_##FUNC); \
		std_error_result = errno; \
		file = uut_file; \
		uut_result = uut_##FUNC(CALL_##FUNC); \
		file = file_temp; \
		if(std_result != uut_result) { \
			ERROR("_harness_" #FUNC ": std_result (%d) != uut_result (%d)\n", std_result, uut_result); \
			ERROR("std_fd %i, uut_fd %i\n", std_file, uut_file); \
			ERROR("errno(std): %s\n", strerror(std_error_result)); PRINT_ERROR_NAME(std_error_result) \
			ERROR("errno(uut): %s\n", strerror(errno)); PRINT_ERROR_NAME(errno) \
			assert(0); \
		} \
		if((std_error_result != errno) && ( ( (!strcmp(#FUNC, "PWRITE")) || ((!strcmp(#FUNC, "WRITE")) ) ) && ( ((std_error_result==14)&&(errno == 22)) || ((std_error_result==22)&&(errno==14))))) { \
			DEBUG("_harness_" #FUNC ": std_error_result (%i) != errno (%i) (special case; ignored)\n", std_error_result, errno); \
			DEBUG("std: %s\n", strerror(std_error_result)); PRINT_ERROR_NAME(std_error_result)\
			DEBUG("uut: %s\n", strerror(errno)); PRINT_ERROR_NAME(errno) \
		} else if(std_error_result != errno) { \
			ERROR("_harness_" #FUNC ": std_error_result (%i) != errno (%i)\n", std_error_result, errno); \
			ERROR("std: %s\n", strerror(std_error_result)); PRINT_ERROR_NAME(std_error_result)\
			ERROR("uut: %s\n", strerror(errno)); PRINT_ERROR_NAME(errno) \
		} \
		if(_print_file_stats(std_file) != _print_file_stats(uut_file) ) { \
			WARNING("File lengths don't match after "#FUNC": this may be OK if this is multithreaded\n"); \
		} \
		_harness_compare_files(std_file, uut_file, "_harness_" #FUNC " (after)\n"); \
		errno = std_error_result; \
		return std_result; \
	 }
#define TEST_WRAP_IWRAP(r, data, elem) TEST_WRAP(elem)

#define TEST_SIMPLEWRAP(FUNC) \
	RETT_##FUNC _harness_##FUNC(INTF_##FUNC) { \
		TEST_CHECK_RESOLVE_FILEOPS; \
		DEBUG("CALL: _harness_" #FUNC ": has no FD, so just calling \"%s\"->" #FUNC "\n", _harness_fileops->name);\
		return _harness_fileops->FUNC(CALL_##FUNC); \
	}
#define TEST_SIMPLEWRAP_IWRAP(r, data, elem) TEST_SIMPLEWRAP(elem)

//#define FILEOPS_WITH_FD (READ) (WRITE) (SEEK) (CLOSE) (TRUNC) (READV) (WRITEV) (TRUNC64) (SEEK64) (PREAD) (PWRITE) (FSYNC) (FDSYNC)
//#define FILEOPS_WITHOUT_FD (FORK) (PIPE)

BOOST_PP_SEQ_FOR_EACH(TEST_WRAP_IWRAP, x, FILEOPS_WITH_FD (ACCEPT))

BOOST_PP_SEQ_FOR_EACH(TEST_SIMPLEWRAP_IWRAP, x, FILEOPS_WITHOUT_FD (SOCKET))

int my_copy(const char* file1, const char* file2);

void _harness_init2(void)
{
	_harness_fd_lookup = (int*)calloc(OPEN_MAX+10, sizeof(int));
	_fd_path_lookup = (char**)calloc(OPEN_MAX+10, sizeof(char*));

	fileops_ref = NULL;
	fileops_test = NULL;

	_harness_fd_valid = (int*) calloc(OPEN_MAX, sizeof(int));

	int i;
	for(i=0; i<OPEN_MAX; i++){
		_harness_fd_lookup[i] = -1;
		_harness_fd_valid[i] = 0;
		_fd_path_lookup[i] = NULL;
	}


	assert(_hub_find_fileop("harness")!=NULL);
	_hub_find_fileop("harness")->resolve = _harness_specific_resolver_wrap;

	// _nvp_debug_handoff();
}

int _harness_specific_resolver_wrap(char* tree)
{
	RESOLVE_TWO_FILEOPS("harness", fileops_ref, fileops_test)

	assert(fileops_ref != NULL);
	assert(fileops_test != NULL);

	return 0;
}


int _testspecific_resolve_fileops(char* fileops_ref_name, char* fileops_harness_name) 
{
	assert(0); // shouldn't be used

	DEBUG("Resolving fileops \"%s\" and \"%s\"\n",
		fileops_ref_name, fileops_harness_name);
	
	fileops_ref = _hub_find_fileop(fileops_ref_name);
	if(fileops_ref == NULL) {
		WARNING("Unable to find fileops \"%s\" on this pass; using posix instead\n", fileops_ref_name);
		fileops_ref = _hub_find_fileop("posix");
	}
	if(fileops_ref == NULL) {
		WARNING("Couldn't find \"posix\" on this pass\n");
		return -1;
	}

	fileops_test = _hub_find_fileop(fileops_harness_name);
	if(fileops_test==NULL) {
		WARNING("Unable to find fileops \"%s\" on this pass; using posix instead\n", fileops_ref_name);
		fileops_ref = _hub_find_fileop("posix");
	}
	if(fileops_test == NULL ) {
		WARNING("Couldn't find \"posix\" on this pass\n");
		return -1;
	}

	//#define STD_FOP _hub_find_fileop("posix")
	#define STD_FOP fileops_test

	_harness_fd_lookup[0] = STD_FOP->DUP(0);
	_harness_fd_valid[0]  = 1;
	_fd_path_lookup[0]    = (char*)calloc(1, sizeof(char));
	_harness_fd_lookup[1] = STD_FOP->DUP(1);
	_harness_fd_valid[1]  = 1;
	_fd_path_lookup[1]    = (char*)calloc(1, sizeof(char));
	_harness_fd_lookup[2] = STD_FOP->DUP(2);
	_harness_fd_valid[2]  = 1;
	_fd_path_lookup[2]    = (char*)calloc(1, sizeof(char));

	return 0;
}


int _harness_OPEN(const char* path, int oflag, ...)
{
	TEST_CHECK_RESOLVE_FILEOPS;

	if(path == NULL) {
		DEBUG("_harness_OPEN: path was null\n");
		return -1;
	}

	int (*std_open)(const char* path, int oflag, ...) = fileops_ref->OPEN;
	int (*uut_open)(const char* path, int oflag, ...) = fileops_test->OPEN;

	const char* std_file = path;
	char* uut_file = (char*) calloc(strlen(path)+5, sizeof(char));
	memcpy(uut_file, std_file, strlen(path)+1);
	strcat(uut_file, ".uut");

	DEBUG("Opening \"%s\" (std) and \"%s\" (uut)\n", std_file, uut_file);

	int std_result, uut_result;
	int std_error_result;

	// if the file didn't exist before and did exist after,
	// then we need to delete the shadow file
	int existed_before = 0;
	int existed_after  = 0;

	if(!access(std_file, F_OK)) {
		existed_before = 1;
	} else {
		existed_before = 0;
	}

	errno = 0;

	int mode = 644;
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		mode = va_arg(arg, int);
		va_end(arg);

		std_result = std_open(std_file, oflag, mode);
		std_error_result = errno;
	} else {
		std_result = std_open(std_file, oflag);
		std_error_result = errno;
	}

	if(std_result < 0) {
		DEBUG("Failed to open file: %s\n", strerror(errno));
		return std_result;
	}

	if(!access(std_file, F_OK)) {
		existed_after = 1;
	} else {
		existed_after = 0;
	}

	// if the file was set to be created, and std_open succeeded, then no shadow copy is needed.
	// else create the shadow copy before opening it
	if( (!existed_before) && (existed_after)  )
	{
		DEBUG("Shadow file is supposed to not exist at time of open: not creating one.\n");
		
		if(!access(uut_file, F_OK)) // file exists
		{
			DEBUG("Shadow file (%s) already exists for file %s, so let's delete it\n", uut_file, std_file); 
			
			int del_result = remove(uut_file);
			
			if(del_result)
			{
				ERROR("Failed to remove file %s, shadow file of %s: %s\n", uut_file, std_file, strerror(errno));
				assert(0);
			}
			else
			{
				DEBUG("Successfully deleted %s\n", uut_file);
			}
		}
	}
	else 
	{
		if(my_copy(std_file, uut_file))
		{
			ERROR("Failed to create shadow file %s\n", uut_file);
			return -1;
		} else {
			DEBUG("Successfully created shadow file %s\n", uut_file);
		}
	}

	DEBUG("Copy command complete.\n");

	DEBUG("Calling uut_open (%s->OPEN)\n", fileops_test->name);

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		uut_result = uut_open(uut_file, oflag, mode);
	} else {
		uut_result = uut_open(uut_file, oflag);
	}
	
	if( (std_result>=0) && (uut_result>=0) && (errno==2) && 
	    (!existed_before) && (existed_after) && (std_error_result == 0) ) {
		errno = 0;
	}

	if( std_result == uut_result) {
		ERROR("std_result (%i) == uut_result (%i)\n", std_result, uut_result);
		PRINT_ERROR_NAME(std_error_result)
		PRINT_ERROR_NAME(errno)
	}

	if(std_error_result != errno) {
		ERROR("_harness_OPEN: std_error_result (%i) != errno (%i)\n", std_error_result, errno);
		ERROR("std: %s\n", strerror(std_error_result));
		PRINT_ERROR_NAME(std_error_result)
		ERROR("uut: %s\n", strerror(errno));
		PRINT_ERROR_NAME(errno)
	}

	if((std_result>=2)&&(uut_result < 2)) {
		ERROR("Got an invalid result for uut_result (%i)\n", uut_result);
	}

	_harness_fd_lookup[std_result] = uut_result;

	_fd_path_lookup[std_result] = (char*)calloc(strlen(std_file)+1, sizeof(char));
	_fd_path_lookup[uut_result] = (char*)calloc(strlen(uut_file)+1, sizeof(char));

	assert(strlen(std_file)>0);
	assert(strlen(uut_file)>strlen(std_file));

	memcpy(_fd_path_lookup[std_result], std_file, strlen(std_file));
	memcpy(_fd_path_lookup[uut_result], uut_file, strlen(uut_file));

	_harness_compare_files(std_result, uut_result, "_harness_OPEN (after)");

	errno = std_error_result;

	if(_harness_fd_valid[std_result])
	{
		ERROR("There was already a valid fd open at that std result (%i)\n", std_result);
		assert(0);
	}

	if(_harness_fd_valid[uut_result])
	{
		ERROR("There was already a valid fd open at that uut result (%i)\n", uut_result);
		assert(0);
	}

	if(std_result > 0)
	{
		DEBUG("FD becoming valid: %i\n", std_result);
		_harness_fd_valid[std_result] = 1;
		if(uut_result > 0)
		{
			_harness_fd_valid[uut_result] = 1;
			DEBUG("FD becoming valid: %i\n", uut_result);
		}
		else
		{
			ERROR("std returned a valid result, and uut did not.\n");
		}
	}

	free(uut_file);

	return std_result;
}

int _harness_IOCTL(INTF_IOCTL)
{
	TEST_CHECK_RESOLVE_FILEOPS;
	
	DEBUG("CALL: _harness_IOCTL: has no FD, so just calling \"%s\"->IOCTL\n", _harness_fileops->name);

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _harness_fileops->IOCTL(file, request, third);

	return result;
}

RETT_CLOSE _harness_CLOSE(INTF_CLOSE)
{
	TEST_CHECK_RESOLVE_FILEOPS;

	DEBUG("CALL: _harness_CLOSE\n");

	if( (file<0) || (file>=OPEN_MAX)) {
		DEBUG("input file descriptor %i is out of range!\n", file);
		return -1;
	}

	if(_harness_fd_valid[file] == -1) {
		DEBUG("Special case: we're closing either stdout or stderr; ignoring shadow descriptor\n");
		int ret = fileops_ref->CLOSE(CALL_CLOSE);
		_harness_fd_lookup[file] = -1;
		_harness_fd_valid[file] = 0;
		return ret;
	}

	if(_harness_fd_lookup[file]<0) {
		DEBUG("That shadow descriptor (%i) is invalid: perhaps you didn't call open first?\n", _harness_fd_lookup[file]);
		errno = EBADF;
		return -1;
	}
	
	int (*std_close)(INTF_CLOSE) = fileops_ref->CLOSE;
	int (*uut_close)(INTF_CLOSE) = fileops_test->CLOSE;

	int std_file = file;
	int uut_file = _harness_fd_lookup[std_file];

	if(!_harness_fd_valid[std_file]) {
		DEBUG("std_file wasn't valid, so we're just going to return whatever std would return.\n");
		return std_close(CALL_CLOSE);
	}

	errno = 0;
	
	int std_result = std_close(std_file);
	int std_error_result = errno;

	if(std_result>-1) {
		DEBUG("_harness_CLOSE succeeded on std_file\n");
		_harness_fd_valid[std_file] = 0;
		DEBUG("FD becoming invalid: %i\n", std_file);
	} else {
		DEBUG("_harness_CLOSE failed on std_file\n");
	}

	int uut_result = uut_close(uut_file);
	if(uut_result>-1) {
		DEBUG("_harness_CLOSE succeeded on uut_file\n");
		_harness_fd_valid[uut_file] = 0;
		DEBUG("FD becoming invalid: %i\n", uut_file);
	} else {
		DEBUG("_harness_CLOSE failed on uut_file\n");
	}

	if(std_result != uut_result) {
		ERROR("_harness_CLOSE: std_result (%i) != uut_result (%i)\n", std_result, uut_result);
	}

	if(std_error_result != errno) {
		ERROR("_harness_CLOSE: std_error_result (%i) != errno (%i)\n", std_error_result, errno);
		ERROR("std: %s\n", strerror(std_error_result));
		ERROR("uut: %s\n", strerror(errno));
	}

	_harness_fd_lookup[file] = -1;

	if(uut_result == 0) // close was successful
	{
		if(_fd_path_lookup[uut_file] == NULL) {
			DEBUG("Was going to delete the shadow file for fd %i (shadow fd %i) but the path was null...\n", std_file, uut_file);
		}
		/*
		else
		{
			DEBUG("Deleting the shadow file which was associated with fd %i (shadow fd %i; shadow path %s)\n", 
				file, uut_file, _fd_path_lookup[uut_file]);
			
			int del_result = remove(_fd_path_lookup[uut_file]);
			
			if(del_result)
			{
				DEBUG("Failed to remove file %s, shadow file of fd %i (shadow fd %i).  This is probably bad but who cares\n", _fd_path_lookup[uut_file], std_file, uut_file);
			}
			else
			{
				DEBUG("Successfully deleted %s\n", _fd_path_lookup[uut_file]);
			}
		}
		*/
	}

	errno = std_error_result;
	return std_result;
}

int _harness_DUP(int filedes)
{
	TEST_CHECK_RESOLVE_FILEOPS;

	DEBUG("_harness_DUP: can't return both FDs, but shadow FD will be evaluated.\n");

	if( (filedes<0) || (filedes>=OPEN_MAX)) {
		DEBUG("input file descriptor %i is out of range!\n", filedes);
		return -1;
	}
	if(_harness_fd_lookup[filedes]<0) {
		DEBUG("That shadow descriptor (%i) is invalid: perhaps you didn't call open first?\n", _harness_fd_lookup[filedes]);
		errno = EBADF;
		return -1;
	}
	
	int (*std_dup)(int filedes) = fileops_ref->DUP;
	int (*uut_dup)(int filedes) = fileops_test->DUP;

	int std_file = filedes;
	int uut_file = _harness_fd_lookup[std_file];

	if(!_harness_fd_valid[std_file]) {
		DEBUG("std_file wasn't valid, so we're just going to return whatever std would return.\n");
		return std_dup(filedes);
	}

	errno = 0;
	
	int std_result = std_dup(std_file);
	int std_error_result = errno;

	if(std_result>-1)
	{
		DEBUG("_harness_DUP succeeded on std_file\n");
		assert(!_harness_fd_valid[std_result]);
		_harness_fd_valid[std_result] = 1;
		DEBUG("FD becoming valid: %i\n", std_result);
		
		if(_fd_path_lookup[std_result])
		{
			free(_fd_path_lookup[std_result]);
			_fd_path_lookup[std_result] = NULL;
		}
		assert(_fd_path_lookup[std_file]);
		assert(strlen(_fd_path_lookup[std_file]) >= 0);
		_fd_path_lookup[std_result] = (char*) calloc(strlen(_fd_path_lookup[std_file])+1, sizeof(char));
		memcpy(_fd_path_lookup[std_result], _fd_path_lookup[std_file], strlen(_fd_path_lookup[std_file]));
	} else {
		DEBUG("_harness_DUP failed on std_file\n");
		//return -1;
	}

	int uut_result = uut_dup(uut_file);
	if(uut_result>-1)
	{
		DEBUG("_harness_DUP succeeded on uut_file\n");
		assert(!_harness_fd_valid[uut_result]);
		_harness_fd_valid[uut_result] = 1;
		DEBUG("FD becoming valid: %i\n", uut_result);
	
		if(_fd_path_lookup[uut_result])
		{
			free(_fd_path_lookup[uut_result]);
			_fd_path_lookup[uut_result] = NULL;
		}
		assert(_fd_path_lookup[uut_file]);
		assert(strlen(_fd_path_lookup[uut_file]) > 0);
		_fd_path_lookup[uut_result] = (char*) calloc(strlen(_fd_path_lookup[uut_file])+1, sizeof(char));
		memcpy(_fd_path_lookup[uut_result], _fd_path_lookup[uut_file], strlen(_fd_path_lookup[uut_file]));
	}

	if(std_result == uut_result) {
		ERROR("_harness_DUP: std_result (%i) == uut_result (%i)\n", std_result, uut_result);
		PRINT_ERROR_NAME(std_error_result)
		PRINT_ERROR_NAME(errno)
	}

	if(std_error_result != errno) {
		ERROR("_harness_DUP: std_error_result (%i) != errno (%i)\n", std_error_result, errno);
		ERROR("std: %s\n", strerror(std_error_result));
		PRINT_ERROR_NAME(std_error_result)
		ERROR("uut: %s\n", strerror(errno));
		PRINT_ERROR_NAME(errno)
	}

	_harness_fd_lookup[std_result] = uut_result;

	errno = std_error_result;
	return std_result;
}


int _harness_DUP2(int filedes, int fd2)
{
	TEST_CHECK_RESOLVE_FILEOPS;

	DEBUG("CALL: _harness_DUP2(%i, %i).\n", filedes, fd2);
	
	if( (filedes<0) || (filedes>=OPEN_MAX)) {
		DEBUG("first input file descriptor %i is out of range!\n", filedes);
		return -1;
	}
	
	if( (fd2<0) || (fd2>=OPEN_MAX)) {
		DEBUG("second input file descriptor %i is out of range!\n", fd2);
		return -1;
	}
	
	if( (_harness_fd_lookup[filedes]<0) || (_harness_fd_lookup[filedes]>=OPEN_MAX)) {
		DEBUG("The first shadow descriptor (%i) is invalid: perhaps you didn't call open first?\n", _harness_fd_lookup[filedes]);
		errno = EBADF;
		return -1;
	}
	
	if( (_harness_fd_lookup[fd2]<0) || (_harness_fd_lookup[fd2]>=OPEN_MAX) ) {
		DEBUG("The second shadow descriptor (%i) is invalid: perhaps you didn't call open first?\n", _harness_fd_lookup[fd2]);
		//errno = EBADF;
		//return -1;
		DEBUG("However, I'm going to allow it, since it's just going to get closed anyway.\n");
	}
	
	int testfd1 = _harness_fd_lookup[filedes];
	int testfd2 = _harness_fd_lookup[fd2];

	int (*std_dup2)(int filedes, int fd2) = fileops_ref->DUP2;
	int (*uut_dup2)(int filedes, int fd2) = fileops_test->DUP2;

	DEBUG("Will evaluate shadow FD with %s->(%i, %i)\n", fileops_test->name, testfd1, testfd2);

	int std_file = filedes;
	int uut_file = _harness_fd_lookup[std_file];

	if(!_harness_fd_valid[std_file]) {
		DEBUG("std_file1 wasn't valid, so we're just going to return whatever std would return.\n");
		return std_dup2(filedes, fd2);
	}
	if(!_harness_fd_valid[fd2]) {
		DEBUG("std_file2 wasn't valid, so we're just going to return whatever std would return.\n");
		return std_dup2(filedes, fd2);
	}

	errno = 0;
	int std_result = std_dup2(std_file, fd2);
	int std_error_result = errno;


	if(std_result >= 0)
	{
		_harness_fd_valid[fd2] = 0;
		DEBUG("FD becoming invalid: %i\n", fd2);
		assert(!_harness_fd_valid[std_result]);
		_harness_fd_valid[std_result] = 1;
		DEBUG("FD becoming valid: %i\n", std_result);
		

		if(std_result != std_file)
		{
			DEBUG("Allocating and copying path from std_file (%i) to std_result (%i): %s\n", std_file, std_result, _fd_path_lookup[std_file]);

			assert(_fd_path_lookup[std_file]);
			assert(strlen(_fd_path_lookup[std_file]) > 0);

			char* temp = _fd_path_lookup[fd2];

			// bring the paths too
			_fd_path_lookup[std_result] = (char*) calloc(strlen(_fd_path_lookup[std_file])+1, sizeof(char));
			memcpy(_fd_path_lookup[std_result], _fd_path_lookup[std_file], strlen(_fd_path_lookup[std_file]));
			if(temp != NULL) { free(temp); } // TODO
			if(fd2 != std_result) { _fd_path_lookup[fd2] = NULL; }
		}
	}
	
	if(_harness_fd_valid[testfd1] == -1) { 
		DEBUG("fd %i was valid, but doesn't have a valid shadow file; not doing the shadow dup2.\n");
		DEBUG("If this isn't being used on STDOUT or STDERR, something probably went wrong!\n");
		return std_result;
	}

	int uut_result = uut_dup2(testfd1, testfd2);

	if(uut_result < 0)
	{
		ERROR("std_dup2 succeeded (%i), but uut_dup2 failed (%i): %s\n", std_result, uut_result, strerror(errno));
		assert(0);
	}
	else
	{
		_harness_fd_valid[testfd2] = 0;
		DEBUG("FD becoming invalid: %i\n", testfd2);
		assert(!_harness_fd_valid[uut_result]);
		_harness_fd_valid[uut_result] = 1;
		DEBUG("FD becoming valid: %i\n", uut_result);
		
		if(uut_result != testfd1)
		{
			DEBUG("Allocating and copying path from testfd1 (%i) to uut_result (%i): %s\n", testfd1, uut_result, _fd_path_lookup[testfd1]);
			
			assert(_fd_path_lookup[testfd1]);
			assert(strlen(_fd_path_lookup[testfd1]) > 0);
			
			char* temp = _fd_path_lookup[testfd2];

			_fd_path_lookup[uut_result] = (char*) calloc(strlen(_fd_path_lookup[testfd1])+1, sizeof(char));
			memcpy(_fd_path_lookup[uut_result], _fd_path_lookup[testfd1], strlen(_fd_path_lookup[testfd1]));
			if(temp != NULL) { free(temp); } // TODO
			if(testfd2 != uut_result) { _fd_path_lookup[testfd2] = NULL; }
		} else { assert(std_result == std_file); }

		DEBUG("Done copying paths.\n");

		assert(strcmp(_fd_path_lookup[std_file], _fd_path_lookup[std_result]) == 0);
		assert(strcmp(_fd_path_lookup[uut_file], _fd_path_lookup[uut_result]) == 0);
	}
	
	if(std_result == uut_result) {
		ERROR("_harness_DUP2: std_result (%d) == uut_result (%d)\n", std_result, uut_result);
	}
	
	if(std_error_result != errno) {
		ERROR("_harness_DUP2: std_error_result (%i) != errno (%i)\n", std_error_result, errno);
		ERROR("std: %s\n", strerror(std_error_result));
		PRINT_ERROR_NAME(std_error_result)
		ERROR("uut: %s\n", strerror(errno));
		PRINT_ERROR_NAME(errno)
	}

	_harness_fd_lookup[std_result] = uut_result;

	_harness_compare_files(std_file, uut_file, "_harness_DUP2 (after)\n");

	errno = std_error_result;

	return std_result;
}


// returns 0 if the files are the same, nonzero otherwise
// prints ERROR() messages for infractions
int _harness_compare_files(int f1, int f2, const char* name)
{
	#if DO_PARANOID_FILE_CHECK
	if(( f1>0) && (f2>0) && (f1<OPEN_MAX) && (f2<OPEN_MAX)) {
		DEBUG("Comparing files %i and %i\n", f1, f2);
	} else {
		DEBUG("Not comparing files %i and %i (invalid file(s))\n", f1, f2);
		return 0;
	}

	if( !_harness_fd_valid[f1])
	{
		if(!_harness_fd_valid[f2])
		{
			DEBUG("Neither FD passed to _harness_compare_files was valid (%i and %i)\n", f1, f2);
			return 0;
		}
		else
		{
			ERROR("at least one invalid fd was passed to compare files (%i and %i)\n", f1, f2);
			assert(0);
		}
	}

	struct stat st1, st2;
	int result = 0;

	if( stat(_fd_path_lookup[f1], &st1) ) {
		ERROR("%s: failed to get std_file stats for %s: %s\n", name, _fd_path_lookup[f1], strerror(errno));
		result++;
	}
	if( stat(_fd_path_lookup[f2], &st2) ) {
		ERROR("%s: failed to get uut_file stats for %s: %s\n", name, _fd_path_lookup[f2], strerror(errno));
		result++;
	}

#define DUMP_STAT(item) do{ if(st1.item != st2.item) { DEBUG("%s: std_file %s (%i) didn't match uut_file %s (%i)!\n", name, #item, (int)st1.item, #item, (int)st2.item); } }while(0)

	DUMP_STAT(st_size);
	DUMP_STAT(st_mode);
	DUMP_STAT(st_uid);
	DUMP_STAT(st_gid);

	if((st1.st_size == 0)&&(st2.st_size==0)) {
		return result;
	}
	
	char *b1, *b2;
	b1 = (char*)calloc(st1.st_size, sizeof(char));
	b2 = (char*)calloc(st2.st_size, sizeof(char));

	struct Fileops_p* posix = _hub_find_fileop("posix");

	assert(posix != NULL);

	int fo1 = posix->OPEN(_fd_path_lookup[f1], O_RDONLY); assert(fo1>=0);
	int fo2 = posix->OPEN(_fd_path_lookup[f2], O_RDONLY); assert(fo1>=0);

	assert(posix->READ(fo1, b1, st1.st_size)==st1.st_size);
	assert(posix->READ(fo2, b2, st2.st_size)==st2.st_size);

	posix->CLOSE(fo1);
	posix->CLOSE(fo2);

	if(memcmp(b1, b2, st1.st_size)) {
		ERROR("%s: did not get the same file contents in both files.\n", name);
		result++;
	} else { 
	//	DEBUG("At the end of %s, file contents appear to be the same.\n", name);
	}

	free(b1);
	free(b2);

	return result;
	#else
	return 0;
	#endif
}

int my_copy(const char* file1, const char* file2)
{
	int input, output;
	size_t filesize;

	struct Fileops_p *posix = _hub_find_fileop("posix");

	if((input = posix->OPEN(file1, O_RDONLY)) == -1) {
		ERROR("my_copy couldn't open file %s: %s\n", file1, strerror(errno));
		return -1;
	}

	if((output = posix->OPEN(file2, O_RDWR|O_CREAT|O_TRUNC, 0666)) == -1) {
		ERROR("my_copy couldn't open file %s: %s\n", file2, strerror(errno));
		return -1;
	}

	filesize = posix->SEEK(input, 0, SEEK_END);

	if(filesize > 0)
	{
		char* buf = (char*) calloc(filesize+1, sizeof(char));

		posix->SEEK(input, 0, SEEK_SET);

		if(posix->READ(input, buf, filesize)!=filesize) {
			ERROR("Failed to read from input file\n");
			return -1;
		}
		if(posix->WRITE(output, buf, filesize)!=filesize) {
			ERROR("Failed to write to output file\n");
			return -1;
		}

		free(buf);
	}

	posix->CLOSE(input);
	posix->CLOSE(output);

	return 0;
}

