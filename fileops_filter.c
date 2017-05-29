// a module which, depending on some criteria set in OPEN,
// applies the appropriate file operations on a file descriptor.

#include "nv_common.h"
// maximum number of file operations to support simultaneously
#define MAX_NV_FILEOPS 251

// if defined, sets everything to managed.
#define ENV_FILTER_OVERRIDE "NVP_FILTER_OVERRIDE"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _filter_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _filter_OPEN(INTF_OPEN);
RETT_IOCTL _filter_IOCTL(INTF_IOCTL);

void _filter_init2(void);

// for a given file descriptor (index), stores the fileops to use on that fd
// all values are initialized to the posix ops
struct Fileops_p** _filter_fd_lookup;

struct Fileops_p* _filter_managed_fileops;
//#define _filter_nonmanged_fileops_i filter_fileops_i


MODULE_REGISTRATION_F("filter", _filter_, _filter_init2(); )

// used instead of the default fileops resolver
int filter_check_resolve_fileops(char* tree)
{
	RESOLVE_TWO_FILEOPS("filter", _filter_fileops, _filter_managed_fileops)

	return 0;
}

// A special check to make sure _filter_fd_lookup gets initialized
// _filter_fd_lookup is initialized to "not managed"
#define FILTER_CHECK_RESOLVE_FILEOPS(NAME, FUNCT) do{ \
	if(_filter_managed_fileops == NULL) { \
		assert(0); \
	} \
	if(_filter_fileops==NULL) { \
		assert(0); \
	} \
	}while(0)


// create wrapper functions for each function in ALLOPS_FINITEPARAMS
// (our macros don't support __VA_ARGS__ functions, so they're implemented by hand)
#define FILTER_WRAP_HAS_FD(op) \
	RETT_##op _filter_##op ( INTF_##op ) { \
		FILTER_CHECK_RESOLVE_FILEOPS(_filter_, op); \
		DEBUG("CALL: _filter_" #op "\n"); \
		if(file>=OPEN_MAX) { ERROR("file descriptor too large (%i > %i)\n", file, OPEN_MAX-1); } \
		if(file<0) { DEBUG("file < 0 (file = %i).  return -1;\n", file); return (RETT_##op) -1; } \
		DEBUG("_filter_" #op " is calling %s->" #op "\n", _filter_fd_lookup[file]->name); \
		return (RETT_##op) _filter_fd_lookup[file]->op( CALL_##op ); \
	}
/*
	printf("_filter_fd_lookup %p\n", _filter_fd_lookup); fflush(stdout); \
	printf("file %i\n", file); \
	printf("_filter_fd_lookup[file] %i\n", _filter_fd_lookup[file]); fflush(stdout);\
	printf("_hub_fileops_lookup %p\n", _hub_fileops_lookup); fflush(stdout);\
	printf("_hub_fileops_lookup[_filter_fd_lookup[file] %p\n", _filter_fd_lookup[file]); fflush(stdout);\
	printf("name %s\n", _filter_fd_lookup[file]->name); fflush(stdout); \
	printf("function %p\n", _filter_fd_lookup[file]->op); fflush(stdout); \
*/
#define FILTER_WRAP_HAS_FD_IWRAP(r, data, elem) FILTER_WRAP_HAS_FD(elem)
BOOST_PP_SEQ_FOR_EACH(FILTER_WRAP_HAS_FD_IWRAP, placeholder, FILEOPS_WITH_FD)


// Some of these calls use paths instead of FDs.
// We can't do these on a FD-by-FD basis, so just use default.
// TODO:  we could use the path to track inode, and do it that way.....
#define FILTER_WRAP_NO_FD(op) \
	RETT_##op _filter_##op ( INTF_##op ) { \
		FILTER_CHECK_RESOLVE_FILEOPS(_filter_, op); \
		DEBUG("CALL: " MK_STR(_filter_##op) "\n"); \
		DEBUG("_filter_" #op " is calling %s->" #op "\n", _filter_fileops->name); \
		return _filter_fileops->op( CALL_##op ); \
	}
#define FILTER_WRAP_NO_FD_IWRAP(r, data, elem) FILTER_WRAP_NO_FD(elem)
BOOST_PP_SEQ_FOR_EACH(FILTER_WRAP_NO_FD_IWRAP, placeholder, FILEOPS_WITHOUT_FD)

/*
	printf("_filter_fileops_i %i\n", _filter_fileops_i); fflush(stdout);\
	printf("_hub_fileops_lookup %p\n", _hub_fileops_lookup); fflush(stdout);\
	printf("_hub_fileops_lookup[_filter_fileops_i] %p\n", _hub_fileops_lookup[_filter_fileops_i]); fflush(stdout);\
	printf("name %s\n", _hub_fileops_lookup[_filter_fileops_i]->name); fflush(stdout); \
	printf("function %p\n", _hub_fileops_lookup[_filter_fileops_i]->op); fflush(stdout); \
*/

//#define FILTER_NOT_IMPLEMENTED 
//BOOST_PP_SEQ_FOR_EACH(WRAP_NOT_IMPLEMENTED_IWRAP, _filter_, FILTER_NOT_IMPLEMENTED)


void _filter_init2(void)
{
	DEBUG("CALLING filter_init2\n");

	_filter_managed_fileops = NULL;
	
	// these values are initialized during CHECK_RESOLVE_FILEOPS
	_filter_fd_lookup = (struct Fileops_p**) calloc(OPEN_MAX, sizeof(struct Fileops_p*));

	//struct Fileops_p* fill = _hub_find_fileop("posix");
	//memset(_filter_fd_lookup, fill, OPEN_MAX);
	
	assert(_hub_find_fileop("filter")!=NULL);
	_hub_find_fileop("filter")->resolve = filter_check_resolve_fileops;
}

RETT_OPEN _filter_OPEN(INTF_OPEN)
{
	FILTER_CHECK_RESOLVE_FILEOPS(_filter_, OPEN);

	DEBUG("CALL: _filter_OPEN\n");
	
	int managed;
	struct Fileops_p* op_to_use;
	int result;

	struct stat moneta_st;
	struct stat file_st;
	
	char* path_to_stat = (char*) calloc(strlen(path)+1, sizeof(char));
	strcpy(path_to_stat, path);

	if( stat(MONETA_BLOCK_DEVICE_PATH, &moneta_st) ) {
		WARNING("Failed to get stats for moneta device: \"%s\"\n", strerror(errno));
		//return -1;
		DEBUG("ignoring error...  this time.\n");
	}

	dev_t moneta_dev_id = major(moneta_st.st_rdev);

	DEBUG("Got major number for moneta device \"%s\" %i\n", MONETA_BLOCK_DEVICE_PATH, (int)moneta_dev_id);

	if(moneta_dev_id != ST_MONETA_DEVICE_ID) {
		WARNING("Didn't get the expected device id for moneta device! (expected %i, got %i)\n", (int)ST_MONETA_DEVICE_ID, (int)moneta_dev_id);
	}

	// if the file doesn't exist, and it is set to O_CREAT...
	if(FLAGS_INCLUDE(oflag, O_CREAT) && ((access(path, F_OK)) != 0) )
	{
		DEBUG("Flags include O_CREAT and file doesn't exist: determining path of file.\n");
		char* last_slash = strrchr(path, '/');
		if(last_slash == NULL) {
			path_to_stat = ".";
		} else {
			int newlen = last_slash - path;
			path_to_stat = (char*) calloc(newlen+1, sizeof(char));
			memcpy(path_to_stat, path, newlen);
		}
		DEBUG("Got path \"%s\"\n", path_to_stat);
	} else {
		DEBUG("O_CREAT not set OR file exists; path_to_stat == path (%s) (no change)\n", path);
	}
	

	if(stat(path_to_stat, &file_st) && !FLAGS_INCLUDE(oflag, O_CREAT)) {
		DEBUG("Failed to get file stats for path \"%s\": %s\n", path_to_stat, strerror(errno));
		return -1;
	}

	if( major(file_st.st_dev) == ST_MONETA_DEVICE_ID)
	{
		DEBUG("st_dev matches expected major number (%i): %s will be managed.\n", ST_MONETA_DEVICE_ID, path);
		managed = 1;
	} else if(major(file_st.st_rdev) == ST_MONETA_DEVICE_ID) {
		DEBUG("major(st_dev) was %i, but major(st_rdev) matched expected major number (%i): %s will be managed.\n", 
			(int)major(file_st.st_rdev), ST_MONETA_DEVICE_ID, path);
		managed = 1;
	} else if( S_ISREG(file_st.st_mode) && getenv(ENV_FILTER_OVERRIDE)) {
		DEBUG("major(st_dev) was %i and major(st_rdev) was %i, but filter OVERRIDE enabled and %s is a regular file: it will be managed!\n", (int)major(file_st.st_dev), (int)major(file_st.st_rdev), path);
		managed = 1;
	} else {
		DEBUG("major(st_dev) was %i and major(st_rdev) was %i, but we wanted %i and override was not enabled: %s will not be managed.\n", 
			(int)major(file_st.st_dev), (int)major(file_st.st_rdev), ST_MONETA_DEVICE_ID, path);
		managed = 0;
	}


	if(managed) {
		op_to_use = _filter_managed_fileops;
		DEBUG("_filter_OPEN: filter will manage \"%s\" with fileop \"%s\"\n", path, op_to_use->name);
	} else {
		op_to_use = _filter_fileops;
		DEBUG("_filter_OPEN: filter will NOT manage \"%s\"; will use fileop \"%s\". (got device major number %i, wanted %i)\n",
			path, op_to_use->name, major(file_st.st_dev), ST_MONETA_DEVICE_ID);
	}

	DEBUG("_filter_OPEN  is calling %s->OPEN\n", op_to_use->name);

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		result = op_to_use->OPEN(path, oflag, mode);
	} else {
		result = op_to_use->OPEN(path, oflag);
	}

	if( result >= 0 ) {
			DEBUG("_filter_OPEN assigning fd %i fileop \"%s\"\n",
				result, op_to_use->name);
			_filter_fd_lookup[result] = op_to_use;
	} else if(managed) {
		DEBUG("_filter_OPEN tried to manage \"%s\", but \"%s\"->OPEN failed.\n",
			path, op_to_use->name);
	}
	
	return result;
}


RETT_IOCTL _filter_IOCTL(INTF_IOCTL)
{
	FILTER_CHECK_RESOLVE_FILEOPS(_filter_, IOCTL);

	DEBUG("CALL: _filter_IOCTL\n");

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	DEBUG("_filter_IOCTL  is calling %s->IOCTL\n", _filter_fileops->name);
	
	RETT_IOCTL result = _filter_fileops->IOCTL(file, request, third);

	return result;
}

RETT_CLOSE _filter_CLOSE(INTF_CLOSE)
{
	FILTER_CHECK_RESOLVE_FILEOPS(_filter_, CLOSE);

	DEBUG("CALL: _filter_CLOSE\n");
	
	DEBUG("_filter_CLOSE  is calling %s->CLOSE\n", _filter_fd_lookup[file]->name);
	
	int result = _filter_fd_lookup[file]->CLOSE(CALL_CLOSE);

	if(result) {
		DEBUG("call to %s->CLOSE failed: %s\n", _filter_fileops->name, strerror(errno));
		return result;
	}
	
	_filter_fd_lookup[file] = _filter_fileops;

	return result;
}

RETT_DUP _filter_DUP(INTF_DUP)
{
	FILTER_CHECK_RESOLVE_FILEOPS(_filter_, DUP);

	DEBUG("CALL: _filter_DUP\n");
	
	DEBUG("_filter_DUP  is calling %s->DUP\n", _filter_fd_lookup[file]->name);
	
	int result = _filter_fd_lookup[file]->DUP(CALL_DUP);

	if(result >= 0) {
		DEBUG("Filter managing new FD %i with same ops (\"%s\") as initial FD (%i)\n",
			result, _filter_fd_lookup[file]->name, file);
		_filter_fd_lookup[result] = _filter_fd_lookup[file];
	}

	return result;
}


RETT_DUP2 _filter_DUP2(INTF_DUP2)
{
	FILTER_CHECK_RESOLVE_FILEOPS(_filter_, DUP2);

	DEBUG("CALL: _filter_DUP2\n");

	if(_filter_fd_lookup[file]==NULL) {
		ERROR("invalid fd1 (%i)\n", file);
		assert(0);
	}
	if(_filter_fd_lookup[fd2]==NULL) {
		ERROR("invalid fd2 (%i)\n", fd2);
		assert(0);
	}

	if( _filter_fd_lookup[file] != _filter_fd_lookup[fd2] ) {
	     WARNING("fd1 (%i) and fd2 (%i) do not have the same handlers!\n", file, fd2);
	} else {
		DEBUG("_filter_DUP2: fd1 (%i) and fd2 (%i) have the same handler (%s)\n", file, fd2, _filter_fd_lookup[file]->name);
	}
	
	DEBUG("_filter_DUP2  is calling %s->DUP2(%i, %i) (p=%p)\n", _filter_fd_lookup[file]->name, file, fd2,
		_filter_fd_lookup[file]->DUP2);
DEBUG("CALLING\n");//volatile int asdf=1; while(asdf) {};
	int result = _filter_fd_lookup[file]->DUP2(CALL_DUP2);
DEBUG("CALLED\n");

	if(result != fd2) {
		DEBUG("DUP2 call had an error.\n");
	} else { 
		DEBUG("DUP2 call completed successfully.\n");
	}

	if(result >= 0) {
		DEBUG("Filter managing new FD %i with same ops (\"%s\") as initial FD (%i)\n",
			result, _filter_fd_lookup[file]->name, file);
		_filter_fd_lookup[result] = _filter_fd_lookup[file];
	}

	return result;
}

/*
RETT_MMAP _filter_MMAP(INTF_MMAP)
{
	FILTER_CHECK_RESOLVE_FILEOPS(_filter_, MMAP);
	
	DEBUG("CALL: _filter_MMAP\n");

	if(FLAGS_INCLUDE(flags, MAP_ANONYMOUS)) { // pthreads uses this and it sucks.
		WARNING("Anonymous mapping requested.  Thunking to UNMANAGED (%s)\n", _filter_fileops->name);
		DEBUG("_filter_MMAP  is calling %s->MMAP\n", _filter_fileops->name);
		return _filter_fileops->MMAP( CALL_MMAP );
	}
	if(file < -1) {
		DEBUG("Calling _filter_MMAP with file < -1 (file = %i).  Get outa town!\n", file);
		return NULL;
	}
	
	DEBUG("_filter_MMAP  is calling %s->MMAP\n", _filter_fd_lookup[file]->name);
	
	return _filter_fd_lookup[file]->MMAP( CALL_MMAP );
}
*/

