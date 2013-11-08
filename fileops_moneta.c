// a module which wraps the Moneta interface to standard Posix

#include "nv_common.h"

typedef size_t offset_t;

#include "Moneta.h"

#define ENV_MONETA_FOP "NVP_MONETA_FOP"


#define CHECK_ALIGNMENT 0


BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _moneta_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _moneta_OPEN(INTF_OPEN);
RETT_IOCTL _moneta_IOCTL(INTF_IOCTL);

void _moneta_init2(void);
void _moneta_exit_cleanup(void);

struct Moneta_FD {
	volatile off64_t *offset; // TODO: this needs a lock
	// volatile int *length;
	int flags;
	void** channel;
	char* path;
	ino_t serialno;
	int managed;
};

// for a given file descriptor (index), stores the index of the file operations to use on that file descriptor
// all values are initialized to ENV_FILTER_FOP
struct Moneta_FD **_moneta_fd_lookup;

void** _moneta_channel;


MODULE_REGISTRATION_F("moneta", _moneta_, _moneta_init2(); )

// the operations which are simiply passed to the underlying file ops
#define MONETA_OPS_SIMPLEWRAP (PIPE) (ACCEPT) (SOCKET)
// _moneta_ does implement open read write seek close truncate dup dup2 readv writev

#define MONETA_WRAP(op) \
	RETT_##op _moneta_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_moneta_); \
		DEBUG("CALL: "MK_STR(_moneta_##op)" is calling \"%s\"->"#op"\n",\
		_moneta_fileops->name); \
		return _moneta_fileops->op( CALL_##op ); \
	}
#define MONETA_WRAP_IWRAP(r, data, elem) MONETA_WRAP(elem)
BOOST_PP_SEQ_FOR_EACH(MONETA_WRAP_IWRAP, z, MONETA_OPS_SIMPLEWRAP)

void _moneta_init2(void)
{
	_moneta_fd_lookup = (struct Moneta_FD**) calloc(OPEN_MAX, sizeof(struct Moneta_FD*));

	_moneta_channel = (void**) calloc(1, sizeof(void*));
	*_moneta_channel = NULL;

	atexit(_moneta_exit_cleanup);

	//_nvp_debug_handoff();
}

void _moneta_exit_cleanup(void)
{
	DEBUG("_moneta_exit_cleanup called.  Closing channel %p\n", *_moneta_channel);
	moneta_CloseChannel(*_moneta_channel);
}

RETT_OPEN _moneta_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_OPEN\n");

	int result;
	int managed;

	struct stat moneta_st;
	struct stat file_st;

	char* path_to_stat = (char*) calloc(strlen(path)+1, sizeof(char));
	strcpy(path_to_stat, path);
	
	if(*_moneta_channel==NULL)
	{
		DEBUG("Opening moneta channel...\n");

		char* dev = MONETA_CHAR_DEVICE_PATH;

		if(!dev) {
			WARNING("Couldn't find a moneta device at %s!\n", MONETA_CHAR_DEVICE_PATH);
			dev = "NOTFOUND";
		}

		DEBUG("Opening moneta channel at device \"%s\"\n", MONETA_CHAR_DEVICE_PATH);
//volatile int asdf=1; while(asdf){};
		if(moneta_OpenChannel(dev, _moneta_channel)) {
			ERROR("moneta_OpenChannel failed: %s\n", strerror(errno));
			assert(0);
		} else {
			DEBUG("Successfully completed moneta_OpenChannel\n");
		}

		if(moneta_CheckChannel(*_moneta_channel)) {
			DEBUG("moneta_CheckChannel failed after open: %s\n", strerror(errno));
			assert(0);
		} else {
			DEBUG("Successfully completed moneta_CheckChannel\n");
		}
	} else {
		if(moneta_CheckChannel(*_moneta_channel)) {
			DEBUG("Moneta channel was already open, but failed CheckChannel.  Will re-open channel\n");
			moneta_ReopenChannel(_moneta_channel);
		} else {
			DEBUG("Moneta channel was already open, but passes CheckChannel\n");

		}
	}


	DEBUG("by popular demand, O_SYNC will be added (if not already present)\n");
	oflag |= O_SYNC;

	/////////  Let's filter out anything that's not on a moneta device

	if( stat(MONETA_BLOCK_DEVICE_PATH, &moneta_st) ) {
		WARNING("Failed to get stats for moneta device: \"%s\"\n", strerror(errno));
		assert(0);
		return -1;
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
//	} else if( S_ISREG(file_st.st_mode) && getenv(ENV_FILTER_OVERRIDE)) {
//		DEBUG("major(st_dev) was %i and major(st_rdev) was %i, but filter OVERRIDE enabled and %s is a regular file: it will be managed!\n", (int)major(file_st.st_dev), (int)major(file_st.st_rdev), path);
//		managed = 1;
	} else {
		DEBUG("major(st_dev) was %i and major(st_rdev) was %i, but we wanted %i: %s will not be managed.\n", 
			(int)major(file_st.st_dev), (int)major(file_st.st_rdev), ST_MONETA_DEVICE_ID, path);
		managed = 0;
	}


	if(managed) {
		if( (S_ISREG(file_st.st_mode)==0) && (FLAGS_INCLUDE(oflag, O_CREAT) && file_st.st_mode == S_IFDIR) ) {
			DEBUG("How did a non-regular file get in to _moneta_OPEN?\npath: %s\npath to stat: %s\n", path, path_to_stat);
			switch(file_st.st_mode)
			{
				case S_IFREG: DEBUG("type was regular file\n"); break;
				case S_IFDIR: DEBUG("type was directory\n"); break;
				case S_IFLNK: DEBUG("type was sym link\n"); break;
				case S_IFBLK: DEBUG("type was special block\n"); break;
				case S_IFCHR: DEBUG("type was special char\n"); break;
				case S_IFIFO: DEBUG("type was special fifo\n"); break;
				case S_IFSOCK: DEBUG("type was socket\n"); break;
				default: DEBUG("type was UNEXPECTED!\n"); _nvp_debug_handoff(); break;
			}
		}
		DEBUG("_moneta_OPEN: moneta will manage \"%s\"\n", path);
	} else {
		DEBUG("_moneta_OPEN: moneta will NOT manage \"%s\" (got device major number %i, wanted %i)\n",
			path, major(file_st.st_dev), ST_MONETA_DEVICE_ID);
	}

	/////////  Done filtering


	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		result = _moneta_fileops->OPEN(path, oflag, mode);
	} else {
		if(access(path, F_OK)) {
			DEBUG("File \"%s\" doesn't exist (and O_CREAT isn't set)\n", path);
			// TODO: errno?
			return -1;
		}
		result = _moneta_fileops->OPEN(path, oflag);
	}

	if (result == -1) {
	     return -1;
	}
	

	if(_moneta_fd_lookup[result]) {
		ERROR("FD %i already existed in the _moneta_fd_lookup table!\n", result);
	}

	struct stat st;
	if( stat(path, &st) ) {
		DEBUG("Failed to get file stats: %s\n", strerror(errno));
		return -1;
	}

	_moneta_fd_lookup[result] = (struct Moneta_FD*) calloc(1, sizeof(struct Moneta_FD));

	int i;
	for(i=0; i<OPEN_MAX; i++) {
		if(_moneta_fd_lookup[i] && _moneta_fd_lookup[i]->serialno == st.st_ino) {
			DEBUG("This file is already open in _moneta_...  just an FYI!\n");
		//	_moneta_fd_lookup[result]->length = _moneta_fd_lookup[i]->length;
			break;
		}
	}
	
	_moneta_fd_lookup[result]->offset  = (off64_t*) calloc(1, sizeof(off64_t));
	*_moneta_fd_lookup[result]->offset = 0;
	_moneta_fd_lookup[result]->flags   = oflag;
	_moneta_fd_lookup[result]->channel = (void**) calloc(1, sizeof(void*));
	_moneta_fd_lookup[result]->channel = _moneta_channel;
	_moneta_fd_lookup[result]->path    = (char*) calloc(strlen(path)+1, sizeof(char));
	strcpy(_moneta_fd_lookup[result]->path, path);
	_moneta_fd_lookup[result]->serialno= st.st_ino;
	_moneta_fd_lookup[result]->managed = managed;


	DEBUG("Calling moneta_FileOpened(%p, %i)\n", *_moneta_channel, result);

	if(moneta_FileOpened(*_moneta_channel, result)) {
		ERROR("moneta_FileOpened failed, somehow!\n");
		assert(0);
	}
	
	return result;
}


RETT_CLOSE _moneta_CLOSE(INTF_CLOSE)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_CLOSE\n");

	int result = _moneta_fileops->CLOSE(CALL_CLOSE);

	if((_moneta_fd_lookup[file]->managed) && (moneta_FileClosed(*_moneta_channel, file))) {
		ERROR("moneta_FileClosed failed, somehow!\n");
		assert(0);
	}
	
	free(_moneta_fd_lookup[file]);
	_moneta_fd_lookup[file]= NULL;

	return result;
}


RETT_READ _moneta_READ(INTF_READ)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_READ(%i, %p, %i)\n", file, buf, (int)length);

	if(length < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(length == 0)
	{
		return 0;
	}

	struct Moneta_FD *mfd = _moneta_fd_lookup[file];
	
	off64_t file_offset = *mfd->offset;
	
	// printf("File number: %i\nFile offset: %i\nFile length: %i\nRequested read length: %i\n", file, file_offset, file_length, (int)length);
	DEBUG("performing moneta_Read(%p, %i, %i, %i, %p)...\n", *mfd->channel, file, file_offset, (int)length, buf );

	int result;
	if(mfd->managed)
	{
		result = moneta_Read( *mfd->channel, file, file_offset, length, buf );
		if(result < 0) {
			DEBUG("moneta_Read(%p, %i, %li, %li, %p) failed! (error code %i)\n",
				*mfd->channel, file, file_offset, length, buf, result );
			return result;
		}

		*mfd->offset += result;
	}
	else
	{
		result = _moneta_fileops->READ(CALL_READ);
	}

	return result;
}


RETT_WRITE _moneta_WRITE(INTF_WRITE)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_WRITE(%i, %p, %i)\n", file, buf, (int)length);
	
	if(length < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(length == 0)
	{
		return 0;
	}

	struct Moneta_FD *mfd = _moneta_fd_lookup[file];

	off64_t file_offset = *mfd->offset;

	int result;
	
	if(mfd->managed)
	{
		DEBUG("performing moneta_Write(%p, %i, %li, %li, %p)\n", *mfd->channel, file, file_offset, length, buf);
	
		result = moneta_Write( *mfd->channel, file, file_offset, length, buf );

		if(result < 0) {
			DEBUG("moneta_Write(%p, %i, %li, %li, %p) failed! (error code %i)\n",
				*mfd->channel, file, file_offset, length, buf, result );
			return result;
		}

		*mfd->offset += result;
	}
	else
	{
		DEBUG("performing %s->WRITE(CALL_WRITE)\n", _moneta_fileops->name);
		result = _moneta_fileops->WRITE(CALL_WRITE);
	}

	return result;
}


RETT_SEEK _moneta_SEEK(INTF_SEEK)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_SEEK\n");
	
	struct Moneta_FD *mfd = _moneta_fd_lookup[file];

	if(!mfd->managed) {
		return _moneta_fileops->SEEK(CALL_SEEK);
	}

	return _moneta_SEEK64(CALL_SEEK);
}


RETT_SEEK64 _moneta_SEEK64(INTF_SEEK64)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_SEEK64\n");
	
	struct Moneta_FD *mfd = _moneta_fd_lookup[file];
	
	if(!mfd->managed) {
		return _moneta_fileops->SEEK64(CALL_SEEK64);
	}
	
	// only used for SEEK_END
	struct stat st;
	
	switch(whence)
	{
		case SEEK_SET:
			if(offset < 0)
			{
				DEBUG("offset out of range (would result in negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(mfd->offset) = offset ;
			return *(mfd->offset);

		case SEEK_CUR:
			if((*(mfd->offset) + offset) < 0)
			{
				DEBUG("offset out of range (would result in negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(mfd->offset) += offset ;
			return *(mfd->offset);

		case SEEK_END:
			if( stat(mfd->path, &st) ) {
				ERROR("Failed to get file stats: %s\n", strerror(errno));
				assert(0);
				return -1;
			}
			if( st.st_size + offset < 0 )
			{
				DEBUG("offset out of range (would result in negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(mfd->offset) = st.st_size + offset;
			return *(mfd->offset);

		default:
			DEBUG("invalid whence parameter.\n");
			errno = EINVAL;
			return -1;
	}
}


RETT_TRUNC _moneta_TRUNC(INTF_TRUNC)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_TRUNC\n");

	struct Moneta_FD *mfd = _moneta_fd_lookup[file];
	
	if(!mfd->managed) {
		return _moneta_fileops->TRUNC(CALL_TRUNC);
	}

	return _moneta_TRUNC64(CALL_TRUNC);

/*
	DEBUG("\n"
		"TTTTTTTTTTTTTTTTTTTTTTTRRRRRRRRRRRRRRRRR   UUUUUUUU     UUUUUUUUNNNNNNNN        NNNNNNNN        CCCCCCCCCCCCC\n" 
		"T:::::::::::::::::::::TR::::::::::::::::R  U::::::U     U::::::UN:::::::N       N::::::N     CCC::::::::::::C\n"
		"T:::::::::::::::::::::TR::::::RRRRRR:::::R U::::::U     U::::::UN::::::::N      N::::::N   CC:::::::::::::::C\n"
		"T:::::TT:::::::TT:::::TRR:::::R     R:::::RUU:::::U     U:::::UUN:::::::::N     N::::::N  C:::::CCCCCCCC::::C\n"
		"TTTTTT  T:::::T  TTTTTT  R::::R     R:::::R U:::::U     U:::::U N::::::::::N    N::::::N C:::::C       CCCCCC\n"
		"        T:::::T          R::::R     R:::::R U:::::D     D:::::U N:::::::::::N   N::::::NC:::::C\n"
		"        T:::::T          R::::RRRRRR:::::R  U:::::D     D:::::U N:::::::N::::N  N::::::NC:::::C\n"
		"        T:::::T          R:::::::::::::RR   U:::::D     D:::::U N::::::N N::::N N::::::NC:::::C\n"
		"        T:::::T          R::::RRRRRR:::::R  U:::::D     D:::::U N::::::N  N::::N:::::::NC:::::C\n"
		"        T:::::T          R::::R     R:::::R U:::::D     D:::::U N::::::N   N:::::::::::NC:::::C\n"
		"        T:::::T          R::::R     R:::::R U:::::D     D:::::U N::::::N    N::::::::::NC:::::C\n"
		"        T:::::T          R::::R     R:::::R U::::::U   U::::::U N::::::N     N:::::::::N C:::::C       CCCCCC\n"
		"      TT:::::::TT      RR:::::R     R:::::R U:::::::UUU:::::::U N::::::N      N::::::::N  C:::::CCCCCCCC::::C\n"
		"      T:::::::::T      R::::::R     R:::::R  UU:::::::::::::UU  N::::::N       N:::::::N   CC:::::::::::::::C\n"
		"      T:::::::::T      R::::::R     R:::::R    UU:::::::::UU    N::::::N        N::::::N     CCC::::::::::::C\n"
		"      TTTTTTTTTTT      RRRRRRRR     RRRRRRR      UUUUUUUUU      NNNNNNNN         NNNNNNN        CCCCCCCCCCCCC\n"
	);

	if(length<0) {
		DEBUG("Invalid length parameter.\n");
		errno = EINVAL;
		return -1;
	}

	struct stat st;
	if( stat(mfd->path, &st) ) {
		DEBUG("Failed to get file stats: %s\n", strerror(errno));
		return -1;
	}
	
	int file_length = st.st_size;
	
	if(length >= file_length ) {
		// glibc (but not the POSIX spec) extends the file in this case, so I guess we should too.
		DEBUG("new length (%i) was >= current length (%i): EXTENDING the file\n", (int)length, file_length );
		int result = moneta_Write( *mfd->channel, file, length-1, 1, "\0" );
		if(result != 1) {
			ERROR("Failed to extend the file by writing 1 byte at position %i: wrote %i bytes\n", (int)length-1, result);
		}
		return result;
	}

	if(*(mfd->offset) > length) {
		DEBUG("offset was beyond the length of the file... but I'm not changing it\n");
	}

	return _moneta_fileops->TRUNC(CALL_TRUNC);
*/
}


RETT_TRUNC64 _moneta_TRUNC64(INTF_TRUNC64)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_TRUNC64\n");

	struct Moneta_FD *mfd = _moneta_fd_lookup[file];
	
	if(!mfd->managed) {
		return _moneta_fileops->TRUNC64(CALL_TRUNC64);
	}

	DEBUG("\n"
		"TTTTTTTTTTTTTTTTTTTTTTTRRRRRRRRRRRRRRRRR   UUUUUUUU     UUUUUUUUNNNNNNNN        NNNNNNNN        CCCCCCCCCCCCC\n" 
		"T:::::::::::::::::::::TR::::::::::::::::R  U::::::U     U::::::UN:::::::N       N::::::N     CCC::::::::::::C\n"
		"T:::::::::::::::::::::TR::::::RRRRRR:::::R U::::::U     U::::::UN::::::::N      N::::::N   CC:::::::::::::::C\n"
		"T:::::TT:::::::TT:::::TRR:::::R     R:::::RUU:::::U     U:::::UUN:::::::::N     N::::::N  C:::::CCCCCCCC::::C\n"
		"TTTTTT  T:::::T  TTTTTT  R::::R     R:::::R U:::::U     U:::::U N::::::::::N    N::::::N C:::::C       CCCCCC\n"
		"        T:::::T          R::::R     R:::::R U:::::D     D:::::U N:::::::::::N   N::::::NC:::::C\n"
		"        T:::::T          R::::RRRRRR:::::R  U:::::D     D:::::U N:::::::N::::N  N::::::NC:::::C\n"
		"        T:::::T          R:::::::::::::RR   U:::::D     D:::::U N::::::N N::::N N::::::NC:::::C\n"
		"        T:::::T    64    R::::RRRRRR:::::R  U:::::D     D:::::U N::::::N  N::::N:::::::NC:::::C     64\n"
		"        T:::::T          R::::R     R:::::R U:::::D     D:::::U N::::::N   N:::::::::::NC:::::C\n"
		"        T:::::T          R::::R     R:::::R U:::::D     D:::::U N::::::N    N::::::::::NC:::::C\n"
		"        T:::::T          R::::R     R:::::R U::::::U   U::::::U N::::::N     N:::::::::N C:::::C       CCCCCC\n"
		"      TT:::::::TT      RR:::::R     R:::::R U:::::::UUU:::::::U N::::::N      N::::::::N  C:::::CCCCCCCC::::C\n"
		"      T:::::::::T      R::::::R     R:::::R  UU:::::::::::::UU  N::::::N       N:::::::N   CC:::::::::::::::C\n"
		"      T:::::::::T      R::::::R     R:::::R    UU:::::::::UU    N::::::N        N::::::N     CCC::::::::::::C\n"
		"      TTTTTTTTTTT      RRRRRRRR     RRRRRRR      UUUUUUUUU      NNNNNNNN         NNNNNNN        CCCCCCCCCCCCC\n"
	);

	if(length<0) {
		DEBUG("Invalid length parameter.\n");
		errno = EINVAL;
		return -1;
	}

	struct stat st;
	if( stat(mfd->path, &st) ) {
		DEBUG("Failed to get file stats: %s\n", strerror(errno));
		return -1;
	}
	
	int file_length = st.st_size;
	
	if(length >= file_length ) {
		// glibc (but not the POSIX spec) extends the file in this case, so I guess we should too.
		DEBUG("new length (%i) was >= current length (%i): EXTENDING the file\n", (int)length, file_length );
		int result = moneta_Write( *mfd->channel, file, length-1, 1, "\0" );
		if(result != 1) {
			ERROR("Failed to extend the file by writing 1 byte at position %i: wrote %i bytes\n", (int)length-1, result);
		}
		return result;
	}

	if(*(mfd->offset) > length) {
		DEBUG("offset was beyond the length of the file... but I'm not changing it\n");
	}

	return _moneta_fileops->TRUNC(CALL_TRUNC);
}


RETT_IOCTL _moneta_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);
	
	DEBUG("CALL: _moneta_IOCTL\n");

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _moneta_fileops->IOCTL(file, request, third);
	
	return result;
}


RETT_DUP _moneta_DUP(INTF_DUP)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_DUP\n");
DEBUG("_moneta_DUP was assigned fileops %p\n", _moneta_fileops);

	assert(_moneta_fileops);
DEBUG("that corresponds to %s\n", _moneta_fileops->name);
	assert(_moneta_fileops->DUP);
DEBUG("preparing to call %s->DUP(%i)\n", _moneta_fileops->name, file);	
	int result = _moneta_fileops->DUP(CALL_DUP);
DEBUG("moneta_fileops->DUP returned %i\n", result);
	
	if(!_moneta_fd_lookup[file]->managed) { return result; }

	if( (result < 0) || (result == file) ) {
		DEBUG("DUP failed.\n");
		return result;
	}

DEBUG("does _moneta_fd_lookup[%i] exist? (_moneta_fd_lookup = %p)\n", result, _moneta_fd_lookup);
DEBUG("attempting to access _moneta_fd_lookup[%i]\n", result);
DEBUG("_moneta_fd_lookup[%i]=%p\n", result, _moneta_fd_lookup[result]);
DEBUG("file = %i\n", file);

	if(_moneta_fd_lookup[result]) {
		ERROR("Moneta already has a FD allocated for %i!\n", result);
	} else {
		DEBUG("moneta_dup is allocating a new fd of size %i\n", (int)sizeof(struct Moneta_FD));	
		_moneta_fd_lookup[result] = (struct Moneta_FD*) calloc(1, sizeof(struct Moneta_FD));
		if(_moneta_fd_lookup[result] == NULL) {
			ERROR("_moneta_DUP FAILED to calloc 1 struct of size %i to hold a Moneta_FD!\n", (int)sizeof(struct Moneta_FD));
			assert(0);
		} else {DEBUG("calloc completed %p, file=%i\n", _moneta_fd_lookup[result], file);}
	}

	DEBUG("_moneta_fd_lookup[file (%i)] = %p\n", file, _moneta_fd_lookup[file]);

	_moneta_fd_lookup[result]->offset  = _moneta_fd_lookup[file]->offset;
	_moneta_fd_lookup[result]->flags   = _moneta_fd_lookup[file]->flags;
	_moneta_fd_lookup[result]->channel = _moneta_fd_lookup[file]->channel;
DEBUG("moneta_DUP returning success (%i)\n", result);
	return result;
}


RETT_DUP2 _moneta_DUP2(INTF_DUP2)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);
//_nvp_debug_handoff();
	DEBUG("CALL: _moneta_DUP2\n");
	
	int result = _moneta_fileops->DUP2(CALL_DUP2);

	if (file >= 0 && file <= 2) return result;
	
	assert(_moneta_fd_lookup[file]);
	if(!_moneta_fd_lookup[file]->managed) { return result; }

	if(result != fd2) {
		ERROR("DUP2 call had an error.\n");
	} else { 
		DEBUG("DUP2 call completed successfully.\n");
	}

	if(result < 0) {
		ERROR("DUP2 failed.\n");
		return result;
	}
	
	if(!_moneta_fd_lookup[result]) {
		ERROR("Moneta DIDN'T already have a FD allocated for %i!\n", fd2);
		_moneta_fd_lookup[result] = (struct Moneta_FD*) calloc(1, sizeof(struct Moneta_FD));
	}

	assert(_moneta_fd_lookup[result]);
	assert(_moneta_fd_lookup[file]->offset);
	assert(_moneta_fd_lookup[file]->channel);

	_moneta_fd_lookup[result]->offset  = _moneta_fd_lookup[file]->offset;
	_moneta_fd_lookup[result]->flags   = _moneta_fd_lookup[file]->flags;
	_moneta_fd_lookup[result]->channel = _moneta_fd_lookup[file]->channel;

	return result;
}


RETT_READV _moneta_READV(INTF_READV)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_READV\n");

	if(!_moneta_fd_lookup[file]->managed) {
		return _moneta_fileops->READV(CALL_READV);
	}
	
	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _moneta_READ(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_moneta_READV failed\n");
		return -1;
	}

	return 0;
}


RETT_WRITEV _moneta_WRITEV(INTF_WRITEV)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_WRITEV\n");

	if(!_moneta_fd_lookup[file]->managed) {
		return _moneta_fileops->WRITEV(CALL_WRITEV);
	}

	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _moneta_WRITE(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_moneta_WRITEV failed\n");
		return -1;
	}

	return 0;
}

RETT_FORK _moneta_FORK(INTF_FORK)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	DEBUG("CALL: _moneta_FORK\n");

	RETT_FORK result = _moneta_fileops->FORK(CALL_FORK);

	if(result < 0)
	{
		ERROR("_moneta_FORK->%s_FORK failed!\n", _moneta_fileops->name);
	}
	else  if(result==0)
	{
		DEBUG("Child process is calling moneta_ReopenChannel\n");

		int r = moneta_ReopenChannel(_moneta_channel);

		if(r) {
			ERROR("moneta_ReopenChannel(%p) failed: returned %i\n", _moneta_channel, r);
		}
	}
	else
	{
		DEBUG("Moneta parent has finished fork.\n");
	}

	return result;
}

RETT_PREAD _moneta_PREAD(INTF_PREAD)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);
	
	if(count < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(count == 0)
	{
		return 0;
	}

	struct Moneta_FD *mfd = _moneta_fd_lookup[file];

	if(!mfd->managed) {
		return _moneta_fileops->PREAD(CALL_PREAD);
	}

	DEBUG("CALL: _moneta_PREAD(%i, %p, %i, %i)\n", file, buf, (int)count, (int)offset);

	#if CHECK_ALIGNMENT
	DEBUG("Checking alignment to %i bytes\n", CHECK_ALIGNMENT);
	if(offset%CHECK_ALIGNMENT) {
		DEBUG("offset %i is not %i-byte aligned.\n", (int)offset, CHECK_ALIGNMENT);
	}
	if(count%CHECK_ALIGNMENT) {
		DEBUG("count %i is not %i-byte aligned.\n", (int)count, CHECK_ALIGNMENT);
	}
	#endif

//volatile int cc=1; while(cc){};

	return (RETT_PREAD) moneta_Read(*mfd->channel, file, offset, count, buf);
}

RETT_PWRITE _moneta_PWRITE(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	if(count < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(count == 0)
	{
		return 0;
	}

	struct Moneta_FD *mfd = _moneta_fd_lookup[file];

	if(!mfd->managed) {
		return _moneta_fileops->PWRITE(CALL_PWRITE);
	}

	DEBUG("CALL: _moneta_PWRITE(%i, %p, %i, %i)\n", file, buf, (int)count, (int)offset);

	#if CHECK_ALIGNMENT
	DEBUG("Checking alignment to %i bytes\n", CHECK_ALIGNMENT);
	if(offset%CHECK_ALIGNMENT) {
		DEBUG("offset %i is not %i-byte aligned.\n", (int)offset, CHECK_ALIGNMENT);
	}
	if(count%CHECK_ALIGNMENT) {
		DEBUG("count %i is not %i-byte aligned.\n", (int)count, CHECK_ALIGNMENT);
	}
	#endif

	int r = moneta_Write(*mfd->channel, file, offset, count, buf);
	DEBUG("CALL: _moneta_PWRITE(%i, %p, %i, %i), returned %d\n", file, buf, (int)count, (int)offset, r);
	
	return (RETT_PWRITE) r;
}

RETT_FSYNC _moneta_FSYNC(INTF_FSYNC)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	if(!_moneta_fd_lookup[file]->managed) {
		return _moneta_fileops->FSYNC(CALL_FSYNC);
	}

	DEBUG("CALL: _moneta_FSYNC: CRUSHED\n");

	return 0;
}

RETT_FDSYNC _moneta_FDSYNC(INTF_FDSYNC)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	if(!_moneta_fd_lookup[file]->managed) {
		return _moneta_fileops->FDSYNC(CALL_FDSYNC);
	}

	DEBUG("CALL: _moneta_FDSYNC: CRUSHED\n");

	return 0;
}

RETT_MKSTEMP _moneta_MKSTEMP(INTF_MKSTEMP)
{
	CHECK_RESOLVE_FILEOPS(_moneta_);

	char* suffix = file + strlen(file) - 6; // char* suffix = strstr(file, "XXXXXX");

	DEBUG("Called _moneta_mkstemp with template %s; making a new filename...\n", file);

	if(suffix == NULL) {
		DEBUG("Invalid template string (%s) passed to mkstemp\n", file);
		errno = EINVAL;

		return -1;
	}

	// generate a valid file name
	int i;
	int success = 0;
	for(i=0; i<1000000; i++)
	{
		sprintf(suffix, "%.6i", i);

		int fs = access(file, F_OK);

		if(fs == 0) { // file doesn't exist; we're good
			success = 1;
			break;
		}
	}

	if(!success) {
		DEBUG("No available file names!\n");
		return -1;
	}

	DEBUG("Generated filename %s.  Calling (regular) open...\n", file);
	
	return open(file, O_CREAT | O_EXCL, 0600);
}

