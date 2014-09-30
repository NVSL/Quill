// This serves as a common framework for any libraries serving runtime-determined file ops.
// Does aliasing for ALIAS_*, redirecting them to whatever internal functions are desired.

#include "nv_common.h"
#include "debug.h"

#define ENV_HUB_FOP "NVP_HUB_FOP"

#define ENV_TREE_FILE "NVP_TREE_FILE"

//#define LIBC_SO_LOC "/lib64/libc-2.5.so"
#define LIBC_SO_LOC "/lib64/libc.so.6"

// for a given file descriptor (index), stores the fileops to use on that fd
// all vlaues initialized to the posix ops
struct Fileops_p** _hub_fd_lookup;
struct Fileops_p*  _hub_managed_fileops;

void* _libc_so_p;

int OPEN_MAX; // maximum number of simultaneous open files

int _hub_fileops_count = 0;
struct Fileops_p* _hub_fileops_lookup[MAX_FILEOPS];

int _hub_add_and_resolve_fileops_tree_from_file(char* filename);

int hub_check_resolve_fileops(char* tree);
void _hub_init2(void);

#define HUB_CHECK_RESOLVE_FILEOPS(NAME, FUNCT)  \
	assert( _hub_managed_fileops != NULL ); \
	assert( _hub_fileops         != NULL );

#define HUB_ADD_FUNCTP(SOFILE, FUNCT) \
	dlsym_result = dlsym(SOFILE, MK_STR3( ALIAS_##FUNCT ) ); \
	if(!dlsym_result) { \
		ERROR("Couldn't find symbol \"%s\" for " #FUNCT " in \"" #SOFILE "\"\n", MK_STR3( ALIAS_##FUNCT ) );\
		ERROR("%s\n", dlerror()); \
		assert(0); \
	} \
	fo->FUNCT = (RETT_##FUNCT(*)(INTF_##FUNCT)) dlsym_result ;

#define HUB_ADD_FUNCTP_IWRAP(r, data, elem) HUB_ADD_FUNCTP(data, elem) 

#define HUB_INIT_FILEOPS_P(NAME, SOFILELOC) \
	so_p = dlopen(SOFILELOC, RTLD_LAZY|RTLD_LOCAL); \
	if(!so_p) { \
		ERROR("Couldn't locate \"%s\" at supplied path \"%s\"\n", NAME, SOFILELOC);\
		ERROR("%s\n", dlerror());\
		assert(0);\
	}\
	struct Fileops_p* fo = (struct Fileops_p*) calloc(1, sizeof(struct Fileops_p)); \
	fo->name = (char*)calloc(strlen(NAME)+1, sizeof(char)); \
	fo->name = strcpy(fo->name, NAME); \
	DEBUG("Populating %s Fileops_p from \"%s\"\n", NAME, SOFILELOC); \
	void* dlsym_result; \
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_FUNCTP_IWRAP, so_p, ALLOPS_WPAREN) \


#define HUB_ADD_POSIX_FUNCTP(FUNCT) \
	dlsym_result = dlsym(_libc_so_p, MK_STR3( ALIAS_##FUNCT ) ); \
	if(!dlsym_result) { \
		ERROR("Couldn't find symbol \"%s\" for " #FUNCT " in \"" LIBC_SO_LOC "\"\n", MK_STR3( ALIAS_##FUNCT ) );\
		ERROR("%s\n", dlerror()); \
		assert(0); \
	} \
	fo->FUNCT = (RETT_##FUNCT(*)(INTF_##FUNCT)) dlsym_result ;

#define HUB_ADD_POSIX_FUNCTP_IWRAP(r, data, elem) HUB_ADD_POSIX_FUNCTP(elem) 

#define HUB_INIT_POSIX_FILEOPS_P(NAME) \
	struct Fileops_p* fo = (struct Fileops_p*) calloc(1, sizeof(struct Fileops_p)); \
	fo->name = (char*)calloc(strlen(NAME)+1, sizeof(char)); \
	fo->name = strcpy(fo->name, NAME); \
	DEBUG("Populating hub Fileops_p from libc.so at \"" LIBC_SO_LOC "\"\n" ); \
	void* dlsym_result; \
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_POSIX_FUNCTP_IWRAP, xxx, ALLOPS_WPAREN) \
	_hub_add_fileop(fo); 


int _hub_add_and_resolve_fileops_tree_from_file(char* filename)
{
	if(filename==NULL) {
		ERROR("Filename was null! Can't build tree! (Did you forget to set env var %s?)\n", ENV_TREE_FILE);
		assert(0);
	} else {
		DEBUG("Got filename %s\n", filename);
	}

	if(access(filename, R_OK)) {
		ERROR("Couldn't open file %s for reading: %s\n", filename, strerror(errno));
		assert(0);
	} else {
		DEBUG("File %s is OK for reading\n", filename);
	}

	DEBUG("Reading from file %s\n", filename);

	FILE *file;
	char *buffer;
	unsigned long fileLen;

	//Open file
	file = fopen(filename, "r");
	if (!file)
	{
		ERROR("Unable to open file %s", filename);
		assert(0);
	}

	//Get file length
	fseek(file, 0, SEEK_END);
	fileLen = ftell(file);
	fseek(file, 0, SEEK_SET);

	//Allocate memory
	buffer = (char*) calloc(fileLen+1, sizeof(char));
	if (!buffer)
	{
		ERROR("Memory error!");
		fclose(file);
		assert(0);
	}
	fread(buffer, fileLen, 1, file);
	fclose(file);


	char* modules_to_load[MAX_FILEOPS];
	int module_count = 0;

	char* b = (char*) calloc(strlen(buffer)+1, sizeof(char));
	memcpy(b, buffer, strlen(buffer)+1);

	char* tok = strtok(b, "(), \t\n");
	
	while(tok)
	{
		//DEBUG("Got token \"%s\"\n", tok);
		if(module_count == MAX_FILEOPS) {
			ERROR("Too many fileops!  Change MAX_FILEOPS in nv_common.h (current max is %i)\n", MAX_FILEOPS);
			assert(0);
		}
		int i;
		int cmp_match = 0;
		for(i=0; i<module_count; i++) {
			if(!strcmp(modules_to_load[i], tok)) {
				cmp_match = 1;
				break;
			}
		}
		if(cmp_match) {
			DEBUG("Module %s was already in the list; skipping.\n", tok);
		} else {
		//	DEBUG("Adding module %s\n", tok);
			modules_to_load[module_count] = calloc(strlen(tok)+1, sizeof(char));
			memcpy(modules_to_load[module_count], tok, strlen(tok)+1);
			module_count++;
		}
		tok = strtok(NULL, "(), \t\n");
	}

	if(strcmp(modules_to_load[0],"hub")) {
		ERROR("Invalid format: first item must be hub\n");
		assert(0);
	}

	char* tree = (char*) calloc(fileLen, sizeof(char));
	int i;
	char c[2] = { '\0', '\0' };

	// strip whitespace from the tree
	for(i=0; i<fileLen; i++)
	{
		if(!isspace(buffer[i]))
		{
			c[0] = buffer[i];
			strcat(tree, c);
		}
	}

	DEBUG("Here's the tree without whitespace: %s\n", tree);

	DEBUG("%i modules will be loaded (not counting hub).  Here are their names:\n", module_count-1);
	char fname[256];
	void* so_p;
	for(i=1; i<module_count; i++)
	{
		sprintf(fname, "libfileops_%s.so", modules_to_load[i]);
		DEBUG_P("%s (%s)\n", modules_to_load[i], fname);
	}
	for(i=1; i<module_count; i++)
	{
		sprintf(fname, "libfileops_%s.so", modules_to_load[i]);
		if(strcmp(modules_to_load[i],"posix")==0) {
			DEBUG("Module \"posix\" is loaded manually; skipping.\n");
		} else {
			DEBUG("Loading module \"%s\"\n", modules_to_load[i]);
			HUB_INIT_FILEOPS_P(modules_to_load[i], fname);
		}
	}

	DEBUG("Done adding fileops.  Resolving all fileops...\n");

	_hub_resolve_all_fileops(tree);

	DEBUG("Done initializing hub and resolving fileops.\n");
	
	return 0;
}


// Declare and do aliasing for every function with finite parameters.
BOOST_PP_SEQ_FOR_EACH(DECLARE_AND_ALIAS_FUNCTS_IWRAP, _hub_, ALLOPS_FINITEPARAMS_WPAREN)

// OPEN and IOCTL don't have finite parameters; declare and alias them manually.
RETT_OPEN ALIAS_OPEN(INTF_OPEN) WEAK_ALIAS("_hub_OPEN");
RETT_OPEN  _hub_OPEN(INTF_OPEN);
RETT_IOCTL ALIAS_IOCTL(INTF_IOCTL) WEAK_ALIAS("_hub_IOCTL");
RETT_IOCTL  _hub_IOCTL(INTF_IOCTL);

RETT_OPEN64 ALIAS_OPEN64(INTF_OPEN64) WEAK_ALIAS("_hub_OPEN64");
RETT_OPEN64  _hub_OPEN64(INTF_OPEN64);
RETT_MKSTEMP ALIAS_MKSTEMP(INTF_MKSTEMP) WEAK_ALIAS("_hub_MKSTEMP");
RETT_MKSTEMP  _hub_MKSTEMP(INTF_MKSTEMP);

MODULE_REGISTRATION_F("hub", _hub_, _hub_init2(); )


// Creates the set of standard posix functions as a module.
void _hub_init2(void)
{
	MSG("Initializing the libnvp hub.  If you're reading this, the library is being loaded! (this is PID %i, my parent is %i)\n", getpid(), getppid());
	MSG("Call tree will be parsed from %s\n", getenv(ENV_TREE_FILE));

	DEBUG("Initializing posix module\n");

	_libc_so_p = dlopen(LIBC_SO_LOC, RTLD_LAZY|RTLD_LOCAL);

	if(!_libc_so_p) {
		ERROR("Couldn't locate libc.so at supplied path \"" LIBC_SO_LOC "\"\n");
		ERROR("%s\n", dlerror());
		assert(0);
	}

	HUB_INIT_POSIX_FILEOPS_P("posix");


	_hub_fd_lookup = (struct Fileops_p**) calloc(OPEN_MAX, sizeof(struct Fileops_p*));

	assert(_hub_find_fileop("hub")!=NULL);
	_hub_find_fileop("hub")->resolve = hub_check_resolve_fileops;

	_hub_add_and_resolve_fileops_tree_from_file(getenv(ENV_TREE_FILE));

	DEBUG("Currently printing on stderr\n");

	_nvp_print_fd = fdopen(_hub_find_fileop("posix")->DUP(2), "a"); 

	DEBUG("Now printing on fd %p\n", _nvp_print_fd);
	assert(_nvp_print_fd >= 0);

	//_nvp_debug_handoff();
}


// used instead of the default fileops resolver
int hub_check_resolve_fileops(char* tree)
{
	RESOLVE_TWO_FILEOPS("hub", _hub_fileops, _hub_managed_fileops);

	int i;
	for(i=0; i<OPEN_MAX; i++)
	{
		_hub_fd_lookup[i] = _hub_fileops;
	}
	_hub_fd_lookup[0] = _hub_managed_fileops;
	_hub_fd_lookup[1] = _hub_managed_fileops;
	_hub_fd_lookup[2] = _hub_managed_fileops;

	return 0;
}

//RETT_CLONE clone(INTF_CLONE) WEAK_ALIAS("_hub_CLONE");
//RETT_CLONE __clone(INTF_CLONE) WEAK_ALIAS("_hub_CLONE");


// RETT_CLONE __clone(INTF_CLONE) WEAK_ALIAS( "_hub_CLONE" );


#define HUB_WRAP_HAS_FD(op) \
	RETT_##op _hub_##op ( INTF_##op ) { \
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op); \
		DEBUG("CALL: _hub_" #op "\n"); \
		if(file>=OPEN_MAX) { DEBUG("file descriptor too large (%i > %i)\n", file, OPEN_MAX-1); errno = EBADF; return (RETT_##op) -1; } \
		if(file<0) { DEBUG("file < 0 (file = %i).  return -1;\n", file); errno = EBADF; return (RETT_##op) -1; } \
		if(_hub_fd_lookup[file]==NULL) { DEBUG("_hub_"#op": That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file); errno = EBADF; return -1; } \
		DEBUG("_hub_" #op " is calling %s->" #op "\n", _hub_fd_lookup[file]->name); \
		return (RETT_##op) _hub_fd_lookup[file]->op( CALL_##op ); \
	}

#define HUB_WRAP_NO_FD(op) \
	RETT_##op _hub_##op ( INTF_##op ) { \
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op); \
		DEBUG("CALL: " MK_STR(_hub_##op) "\n"); \
		DEBUG("_hub_" #op " is calling %s->" #op "\n", _hub_fileops->name); \
		return _hub_fileops->op( CALL_##op ); \
	}

#define HUB_WRAP_HAS_FD_IWRAP(r, data, elem) HUB_WRAP_HAS_FD(elem)
#define HUB_WRAP_NO_FD_IWRAP(r, data, elem) HUB_WRAP_NO_FD(elem)

BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_HAS_FD_IWRAP, placeholder, FILEOPS_WITH_FD)
BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_NO_FD_IWRAP, placeholder, FILEOPS_WITHOUT_FD)



void _hub_resolve_all_fileops(char* tree)
{
	// do a resolve for all ops in the table so far
	int i;
	DEBUG("_hub_fileops_lookup contains %i elements, and here they are:\n", _hub_fileops_count);
	for(i=0; i<_hub_fileops_count; i++)
	{
		DEBUG("\t%s\n", _hub_fileops_lookup[i]->name);
	}

	for(i=0; i<_hub_fileops_count; i++)
	{
		DEBUG("Resoving %i of %i: %s\n", i+1, _hub_fileops_count, _hub_fileops_lookup[i]->name);

		if(!strcmp(_hub_fileops_lookup[i]->name, "posix")) { continue; }
		
		_hub_fileops_lookup[i]->resolve(tree);
	}
}

struct Fileops_p* default_resolve_fileops(char* tree, char* name)
{
	DEBUG("Resolving \"%s\" fileops using default resolver.\n", name);
	
	struct Fileops_p* fileops = *(resolve_n_fileops(tree, name, 1));

	if(fileops == NULL){
		ERROR("Couldn't resolve fileops %s\n", name);
		assert(0);
	}
	DEBUG("\"%s\" resolved to \"%s\"\n", name, fileops->name);
	return fileops;
}

struct Fileops_p** resolve_n_fileops(char* tree, char* name, int count)
{
	struct Fileops_p** result = (struct Fileops_p**) calloc(count, sizeof(struct Fileops_p*));

	char* start = strstr(tree, name);
	if(!start){
		ERROR("Coudln't find this module (%s) in the tree (%s)\n", name, tree);
		assert(0);
	}
	
	start += strlen(name)+1;

	int slot = 0;
	for(slot = 0; slot<count; slot++)
	{
	//	DEBUG("Attempting to fill slot %i\n", slot);

		int paren_level = 0;
		int i;
		// find the next punctuation mark
		for(i=0; i<strlen(start); i++)
		{
			if(ispunct(start[i])) { break; }
		}

		char* module_name = (char*) calloc(i+3, sizeof(char));
		module_name = memcpy(module_name, start, i);

	//	DEBUG("Module %s looking for module %s for slot %i\n", name, module_name, slot);
		
		result[slot] = _hub_find_fileop(module_name);
		if(result[slot] == NULL){
			ERROR("Couldn't resolve fileops %s slot %i to expected module %s\n", name, slot, module_name);
			assert(0);
		} else {
	//		DEBUG("Successfully resolved slot %i to module %s\n", slot, result[slot]->name);
		}

		// if we have more tokens to find, skip to the start of the next one
		// otherwise fuhgeddaboudit
		if(slot+1>=count) {
	//		DEBUG("That was the last token; we out.\n");
			continue;
		}
	//	DEBUG("Old start: %s\n", start);
		start += i;
	//	DEBUG("New start: %s\n", start);

		// if it's a comma, the next element follows immediately
		if(start[0] == ',') {
	//		DEBUG("Next element was a comma; it's a leaf.\n");
			start++;
			continue;
		}
		
		// we have to do some digging to find the next element
		assert(start[0]=='(');
	//	DEBUG("Next element was a paren; it's an internal node.\n");
		start++;
		paren_level=1;
		for(i=0; i<strlen(start); i++)
		{
			//DEBUG("%c\n", start[i]);
			switch(start[i]) {
				case '(': paren_level++; break;
				case ')': paren_level--; break;
			}
			if(paren_level == 0) {
				start += i+2;
				break;
			}
		}
	//	DEBUG("After skipping stuff, start looks like this: %s\n", start);
	}
	return result;
}



// registers a module to be searched later
void _hub_add_fileop(struct Fileops_p* fo)
{

	DEBUG("Registering Fileops_p \"%s\" at index %i\n",
		fo->name, _hub_fileops_count);
	
	int i=0;
	for(i=0; i<_hub_fileops_count; i++)
	{
		if(!strcmp(_hub_fileops_lookup[i]->name, fo->name))
		{
			ERROR("Can't add fileop %s: one with the same name already exists at index %i\n", fo->name, i);
			assert(0);
		}
	}
		
	if(_hub_fileops_count >= MAX_FILEOPS) {
		ERROR("_hub_fileops_lookup is full: too many Fileops_p!\n");
		ERROR("Maximum supported: %i\n", MAX_FILEOPS);
		ERROR("Check fileops_compareharness.c to increase\n");
		return;
	}
	
	_hub_fileops_lookup[_hub_fileops_count] = fo;
	_hub_fileops_count++;
}


// given the name of a Fileops_p, returns the index.
// if the specified fileop is not found, will return the index of "posix"
// or -1 if "posix" isn't found (critical failure)
struct Fileops_p* _hub_find_fileop(const char* name)
{
	if(name == NULL) {
		DEBUG("Name was null; using default: \"posix\"\n");
		name = "posix";
		assert(0);
	}

	int i;
	for(i=0; i<_hub_fileops_count; i++)
	{
		if(_hub_fileops_lookup[i] == NULL) { break; }
		if(strcmp(name, _hub_fileops_lookup[i]->name)==0)
		{
			return _hub_fileops_lookup[i];
		}
	}

	DEBUG("Fileops_p \"%s\" not found; resolving to \"posix\"\n", name);
	name = "posix";
	assert(0);

	for(i=0; i<_hub_fileops_count; i++)
	{
		if(_hub_fileops_lookup[i] == NULL) { break; }
		if(strcmp(name, _hub_fileops_lookup[i]->name)==0)
		{
			return _hub_fileops_lookup[i];
		}
	}

	assert(0);

	return NULL;
}

RETT_OPEN _hub_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_OPEN for %s\n", path);
	
	struct Fileops_p* op_to_use = NULL;

	int result;


	if(access(path, F_OK))
	{
		if(FLAGS_INCLUDE(oflag, O_CREAT))
		{
			DEBUG("File does not exist and is set to be created.  Using managed fileops (%s)\n",
				_hub_managed_fileops->name);
			op_to_use = _hub_managed_fileops;
		} else {
			DEBUG("File does not exist and is not set to be created.  Using unmanaged fileops (%s)\n",
				_hub_fileops->name);
			op_to_use = _hub_fileops;
		}
	} 
	else // file exists
	{
		struct stat file_st;

		if(stat(path, &file_st))
		{
			DEBUG("_hub: failed to get device stats for \"%s\" (error: %s).  Using unmanaged fileops (%s)\n",
				path, strerror(errno), _hub_fileops->name);
			op_to_use = _hub_fileops;
		}
		else if(S_ISREG(file_st.st_mode))
		{
			DEBUG("_hub: file exists and is a regular file.  Using managed fileops (%s)\n",
				_hub_managed_fileops->name);
			op_to_use = _hub_managed_fileops;
		}
		else if (S_ISBLK(file_st.st_mode))
		{
			DEBUG("_hub: file exists and is a block device.  Using managed fileops (%s)\n",
				_hub_managed_fileops->name);
			op_to_use = _hub_managed_fileops;
		}
		else
		{
			DEBUG("_hub: file exists and is not a regular file.  Using unmanaged fileops (%s)\n",
				_hub_fileops->name);
			op_to_use = _hub_fileops;
		}
	}
	
	assert(op_to_use != NULL);

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

	if(result >= 0)
	{
		DEBUG("_hub_OPEN assigning fd %i fileop %s\n", result, op_to_use->name);
		_hub_fd_lookup[result] = op_to_use;
	}
	else 
	{
		DEBUG("_hub_OPEN->%s_OPEN failed; not assigning fileop.\n", op_to_use->name);
	}

	return result;
}

RETT_MKSTEMP _hub_MKSTEMP(INTF_MKSTEMP)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKSTEMP);

	DEBUG("Called _hub_mkstemp with template %s; making a new filename...\n", file);

	char* suffix = file + strlen(file) - 6; // char* suffix = strstr(file, "XXXXXX");

	if(suffix == NULL) {
		DEBUG("Invalid template string (%s) passed to mkstemp\n", file);
		errno = EINVAL;

		return -1;
	}

	int attempts;
	RETT_OPEN result = -1;

	for(attempts = 0; result < 0; attempts++)
	{
		if(attempts > 100)
		{
			DEBUG("We have fallen victim to the race condition between mktemp() and open()!\n");
			assert(0);
		}

		// generate a valid file name
		mktemp(file);

		DEBUG("Generated filename %s.  Calling (regular) open...\n", file);
		
		result = open(file, O_CREAT | O_EXCL | O_RDWR, 0600); // this gets picked up by hub through the usual mechanisms

		if(result < 0 && errno == EEXIST) {
			DEBUG("The file that didn't exist when we started started existing before we called open; trying another file.\n");
			// a race condition exists between mktemp() and calling open.

			//restore filename template characters
			suffix[0] = 'X';
			suffix[1] = 'X';
			suffix[2] = 'X';
			suffix[3] = 'X';
			suffix[4] = 'X';
			suffix[5] = 'X';
		} else {
			DEBUG("Failed to open file %s for reason %i: %s.  Trying another filename.\n", file, errno, strerror(errno));
		}
	}

	DEBUG("Returning %d from _hub_MKSTEMP\n", result);
	return result;
}

RETT_CLOSE _hub_CLOSE(INTF_CLOSE)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, CLOSE);

	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}

	if(_hub_fd_lookup[file]==NULL) {
		DEBUG("That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file);
		errno = EBADF;
		return -1;
	}
	
	DEBUG("CALL: _hub_CLOSE\n");

	assert(_hub_fd_lookup[file]!=NULL);
	assert(_hub_fd_lookup[file]->name!=NULL);

	DEBUG("_hub_CLOSE is calling %s->CLOSE\n", _hub_fd_lookup[file]->name);
	
	int result = _hub_fd_lookup[file]->CLOSE(CALL_CLOSE);

	_hub_fd_lookup[file] = NULL;

	if(result) {
		DEBUG("call to %s->CLOSE failed: %s\n", _hub_fileops->name, strerror(errno));
		return result;
	}
	
	return result;
}

RETT_DUP _hub_DUP(INTF_DUP)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, DUP);
	
	DEBUG("CALL: _hub_DUP(%i)\n", file);
	
	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}
	
	if(_hub_fd_lookup[file]==NULL) {
		DEBUG("That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file);
		errno = EBADF;
		return -1;
	}
	
	assert(_hub_fd_lookup[file]!=NULL);
	assert(_hub_fd_lookup[file]->name!=NULL);
	
	DEBUG("_hub_DUP  is calling %s->DUP\n", _hub_fd_lookup[file]->name);
	
	int result = _hub_fd_lookup[file]->DUP(CALL_DUP);

	if(result >= 0) {
		DEBUG("Hub(dup) managing new FD %i with same ops (\"%s\") as initial FD (%i)\n",
			result, _hub_fd_lookup[file]->name, file);
		_hub_fd_lookup[result] = _hub_fd_lookup[file];
	}

	return result;
}


RETT_DUP2 _hub_DUP2(INTF_DUP2)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, DUP2);

	DEBUG("CALL: _hub_DUP2(%i, %i)\n", file, fd2);

	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}
	
	if( (fd2<0) || (fd2 >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", fd2);
		errno = EBADF;
		return -1;
	}
	
	if(_hub_fd_lookup[file]==NULL) {
		DEBUG("The first file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file);
		errno = EBADF;
		return -1;
	}

	if(_hub_fd_lookup[fd2]==NULL) {
		DEBUG("The second file descriptor (%i) is invalid: perhaps you didn't call open first?\n", fd2);
		errno = EBADF;
		return -1;
	}

	if( _hub_fd_lookup[file] != _hub_fd_lookup[fd2] ) {
		WARNING("fd1 (%i) and fd2 (%i) do not have the same handlers! (%s and %s)\n", file, fd2,
			_hub_fd_lookup[file]->name, _hub_fd_lookup[fd2]->name );
		if(_hub_fd_lookup[file] == _hub_managed_fileops) {
			DEBUG("I'm going to allow this because it's closing the unmanaged file\n");
		}
		else
		{
			DEBUG("This shall be allowed because we want to handle normal files with Posix operations.\n");
		}
	} else {
		DEBUG("_hub_DUP2: fd1 (%i) and fd2 (%i) have the same handler (%s)\n", file, fd2,
			_hub_fd_lookup[file]->name);
	}
	
	DEBUG("_hub_DUP2 is calling %s->DUP2(%i, %i) (p=%p)\n", _hub_fd_lookup[file]->name, file, fd2,
		_hub_fd_lookup[file]->DUP2);

	int result = _hub_fd_lookup[file]->DUP2(CALL_DUP2);


	if(result < 0)
	{
		DEBUG("DUP2 call had an error.\n");
		WARNING("fd2 (%i) may not be correctly marked as valid/invalid in submodules\n", fd2);
	}
	else
	{ 
		DEBUG("DUP2 call completed successfully.\n");
		
		if(result != fd2)
		{
			DEBUG("_hub_DUP2: result!=fd2 (%i != %i), so let's update the table...\n", result, fd2);
			_hub_fd_lookup[fd2] = NULL;
		}

		DEBUG("Hub(dup2) managing new FD %i with same ops (\"%s\") as initial FD (%i)\n",
			result, _hub_fd_lookup[file]->name, file);

		_hub_fd_lookup[result] = _hub_fd_lookup[file];
	}

	return result;
}





RETT_IOCTL _hub_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_IOCTL\n");
	
	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _hub_fileops->IOCTL(file, request, third);

	return result;
}

RETT_OPEN64 _hub_OPEN64(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_OPEN64\n");
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		//return _hub_OPEN(path, oflag|O_LARGEFILE, mode);
		return _hub_OPEN(path, oflag, mode);
	} else {
		//return _hub_OPEN(path, oflag|O_LARGEFILE);
		return _hub_OPEN(path, oflag);
	}
}

RETT_SOCKET _hub_SOCKET(INTF_SOCKET)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_SOCKET\n");

	RETT_SOCKET result = _hub_fileops->SOCKET(CALL_SOCKET);

	if (result > 0) {
		//sockets always use default fileops
		_hub_fd_lookup[result] = _hub_fileops;
	}

	return result;
}

RETT_ACCEPT _hub_ACCEPT(INTF_ACCEPT)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_ACCEPT\n");

	RETT_ACCEPT result = _hub_fileops->ACCEPT(CALL_ACCEPT);

	if (result > 0) {
		//sockets always use default fileops
		_hub_fd_lookup[result] = _hub_fileops;
	}

	return result;
}

RETT_UNLINK _hub_UNLINK(INTF_UNLINK)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_UNLINK\n");

	RETT_UNLINK result = _hub_managed_fileops->UNLINK(CALL_UNLINK);

	return result;
}

/*
RETT_CLONE _hub_CLONE(INTF_CLONE)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_CLONE\n");

	DEBUG("\n"
	"        CCCCCCCCCCCCCLLLLLLLLLLL                  OOOOOOOOO     NNNNNNNN        NNNNNNNNEEEEEEEEEEEEEEEEEEEEEE\n"
	"     CCC::::::::::::CL:::::::::L                OO:::::::::OO   N:::::::N       N::::::NE::::::::::::::::::::E\n"
	"   CC:::::::::::::::CL:::::::::L              OO:::::::::::::OO N::::::::N      N::::::NE::::::::::::::::::::E\n"
	"  C:::::CCCCCCCC::::CLL:::::::LL             O:::::::OOO:::::::ON:::::::::N     N::::::NEE::::::EEEEEEEEE::::E\n"
	" C:::::C       CCCCCC  L:::::L               O::::::O   O::::::ON::::::::::N    N::::::N  E:::::E       EEEEEE\n"
	"C:::::C                L:::::L               O:::::O     O:::::ON:::::::::::N   N::::::N  E:::::E             \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON:::::::N::::N  N::::::N  E::::::EEEEEEEEEE   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N N::::N N::::::N  E:::::::::::::::E   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N  N::::N:::::::N  E:::::::::::::::E   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N   N:::::::::::N  E::::::EEEEEEEEEE   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N    N::::::::::N  E:::::E             \n"
	" C:::::C       CCCCCC  L:::::L         LLLLLLO::::::O   O::::::ON::::::N     N:::::::::N  E:::::E       EEEEEE\n"
	"  C:::::CCCCCCCC::::CLL:::::::LLLLLLLLL:::::LO:::::::OOO:::::::ON::::::N      N::::::::NEE::::::EEEEEEEE:::::E\n"
	"   CC:::::::::::::::CL::::::::::::::::::::::L OO:::::::::::::OO N::::::N       N:::::::NE::::::::::::::::::::E\n"
	"     CCC::::::::::::CL::::::::::::::::::::::L   OO:::::::::OO   N::::::N        N::::::NE::::::::::::::::::::E\n"
	"        CCCCCCCCCCCCCLLLLLLLLLLLLLLLLLLLLLLLL     OOOOOOOOO     NNNNNNNN         NNNNNNNEEEEEEEEEEEEEEEEEEEEEE\n"
	);

	assert(0);

	return (RETT_CLONE) -1;
}
*/

// breaking the build
