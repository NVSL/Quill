// a module which wraps the Moneta interface to standard Posix

#include "nv_common.h"
#include <sys/syscall.h>
typedef size_t offset_t;

#include "sdssd_hostio.h"
#include "sdssd_io_errors.h"

//#define ENV_SDSSD_FOP "NVP_SDSSD_FOP"

pid_t gettid( void )
{
        return syscall( __NR_gettid );
}


#define CHECK_ALIGNMENT 0
/* File inode based free space allocator */ 
/*************************************************************************\
| Hash()                                                                  |
|     The hash function I use is due to Bob Jenkins (see                  |
|     http://burtleburtle.net/bob/hash/evahash.html                       |
|     According to http://burtleburtle.net/bob/c/lookup2.c,               |
|     his implementation is public domain.)                               |
|     It takes 36 instructions, in 18 cycles if you're lucky.             |
|        hashing depends on the fact the hashtable size is always a       |
|     power of 2.  cBuckets is probably ht->cBuckets.                     |
\*************************************************************************/

#define LOG_WORD_SIZE 6
#if LOG_WORD_SIZE == 6                      /* 32 bit words */

#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}
#ifdef WORD_HASH                 /* play with this on little-endian machines */
#define WORD_AT(ptr)    ( *(unsigned long *)(ptr) )
#else
#define WORD_AT(ptr)    ( (ptr)[0] + ((unsigned long)(ptr)[1]<<8) + \
			  ((unsigned long)(ptr)[2]<<16) + ((unsigned long)(ptr)[3]<<24) )
#endif

#elif LOG_WORD_SIZE == 5        /* 32 bit words */

#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>43); \
  b -= c; b -= a; b ^= (a<<9); \
  c -= a; c -= b; c ^= (b>>8); \
  a -= b; a -= c; a ^= (c>>38); \
  b -= c; b -= a; b ^= (a<<23); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>35); \
  b -= c; b -= a; b ^= (a<<49); \
  c -= a; c -= b; c ^= (b>>11); \
  a -= b; a -= c; a ^= (c>>12); \
  b -= c; b -= a; b ^= (a<<18); \
  c -= a; c -= b; c ^= (b>>22); \
}
#ifdef WORD_HASH                 /* alpha is little-endian, btw */
#define WORD_AT(ptr)    ( *(unsigned long *)(ptr) )
#else
#define WORD_AT(ptr)    ( (ptr)[0] + ((unsigned long)(ptr)[1]<<8) + \
			  ((unsigned long)(ptr)[2]<<16) + ((unsigned long)(ptr)[3]<<24) + \
			  ((unsigned long)(ptr)[4]<<32) + ((unsigned long)(ptr)[5]<<40) + \
			  ((unsigned long)(ptr)[6]<<48) + ((unsigned long)(ptr)[7]<<56) )
#endif

#else                            /* neither 32 or 64 bit words */
#error This hash function can only hash 32 or 64 bit words.  Sorry.
#endif

static unsigned long Hash(char *key, unsigned long cBuckets)
{
   unsigned long a, b, c, cchKey, cchKeyOrig;

   cchKeyOrig = 8;
   a = b = c = 0x9e3779b9;       /* the golden ratio; an arbitrary value */

   for ( cchKey = cchKeyOrig;  cchKey >= 3 * sizeof(unsigned long);
	 cchKey -= 3 * sizeof(unsigned long),  key += 3 * sizeof(unsigned long) )
   {
      a += WORD_AT(key);
      b += WORD_AT(key + sizeof(unsigned long));
      c += WORD_AT(key + sizeof(unsigned long)*2);
      mix(a,b,c);
   }

   c += cchKeyOrig;
   switch ( cchKey ) {           /* deal with rest.  Cases fall through */
#if LOG_WORD_SIZE == 5
      case 11: c += (unsigned long)key[10]<<24;
      case 10: c += (unsigned long)key[9]<<16;
      case 9 : c += (unsigned long)key[8]<<8;
               /* the first byte of c is reserved for the length */
      case 8 : b += WORD_AT(key+4);  a+= WORD_AT(key);  break;
      case 7 : b += (unsigned long)key[6]<<16;
      case 6 : b += (unsigned long)key[5]<<8;
      case 5 : b += key[4];
      case 4 : a += WORD_AT(key);  break;
      case 3 : a += (unsigned long)key[2]<<16;
      case 2 : a += (unsigned long)key[1]<<8;
      case 1 : a += key[0];
   /* case 0 : nothing left to add */
#elif LOG_WORD_SIZE == 6
      case 23: c += (unsigned long)key[22]<<56;
      case 22: c += (unsigned long)key[21]<<48;
      case 21: c += (unsigned long)key[20]<<40;
      case 20: c += (unsigned long)key[19]<<32;
      case 19: c += (unsigned long)key[18]<<24;
      case 18: c += (unsigned long)key[17]<<16;
      case 17: c += (unsigned long)key[16]<<8;
               /* the first byte of c is reserved for the length */
      case 16: b += WORD_AT(key+8);  a+= WORD_AT(key);  break;
      case 15: b += (unsigned long)key[14]<<48;
      case 14: b += (unsigned long)key[13]<<40;
      case 13: b += (unsigned long)key[12]<<32;
      case 12: b += (unsigned long)key[11]<<24;
      case 11: b += (unsigned long)key[10]<<16;
      case 10: b += (unsigned long)key[ 9]<<8;
      case  9: b += (unsigned long)key[ 8];
      case  8: a += WORD_AT(key);  break;
      case  7: a += (unsigned long)key[ 6]<<48;
      case  6: a += (unsigned long)key[ 5]<<40;
      case  5: a += (unsigned long)key[ 4]<<32;
      case  4: a += (unsigned long)key[ 3]<<24;
      case  3: a += (unsigned long)key[ 2]<<16;
      case  2: a += (unsigned long)key[ 1]<<8;
      case  1: a += (unsigned long)key[ 0];
   /* case 0: nothing left to add */
#endif
   }
   mix(a,b,c);
   return c & (cBuckets-1);
}


#define MAX_THREADS 1024

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _sdssd_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _sdssd_OPEN(INTF_OPEN);
RETT_IOCTL _sdssd_IOCTL(INTF_IOCTL);

void _sdssd_init2(void);
void _sdssd_exit_cleanup(void);

struct SDSSD_FD {
	volatile off64_t *offset; // TODO: this needs a lock
	// volatile int *length;
	int flags;
	//void** channel; in sdssd if the file is managed we use per thread channel
	char* path;
	ino_t serialno;
	int managed;
};

struct SDSSD_Channel{
	void *channel;
	pid_t tid;
};

// for a given file descriptor (index), stores the index of the file operations to use on that file descriptor
// all values are initialized to ENV_FILTER_FOP
struct SDSSD_FD **_sdssd_fd_lookup;

struct SDSSD_Channel *_sdssd_channel;

void *_control_channel; //created at process initalization to send FileOpened and FileClosed calls


MODULE_REGISTRATION_F("sdssd", _sdssd_, _sdssd_init2(); )

// the operations which are simiply passed to the underlying file ops
#define SDSSD_OPS_SIMPLEWRAP (PIPE) (ACCEPT) (SOCKET)
// _sdssd_ does implement open read write seek close truncate dup dup2 readv writev

#define SDSSD_WRAP(op) \
	RETT_##op _sdssd_##op ( INTF_##op ) { \
		CHECK_RESOLVE_FILEOPS(_sdssd_); \
		DEBUG("CALL: "MK_STR(_sdssd_##op)" is calling \"%s\"->"#op"\n",\
		_sdssd_fileops->name); \
		return _sdssd_fileops->op( CALL_##op ); \
	}
#define SDSSD_WRAP_IWRAP(r, data, elem) SDSSD_WRAP(elem)
BOOST_PP_SEQ_FOR_EACH(SDSSD_WRAP_IWRAP, z, SDSSD_OPS_SIMPLEWRAP)

void _sdssd_init2(void)
{
	int i;
	_sdssd_fd_lookup = (struct SDSSD_FD**) calloc(OPEN_MAX, sizeof(struct SDSSD_FD*));

	_sdssd_channel = (struct SDSSD_Channel*) calloc(MAX_THREADS, sizeof(struct SDSSD_Channel));

	if(!_sdssd_channel || !_sdssd_fd_lookup)
		assert(0);
	
	for(i = 0; i < MAX_THREADS; i ++)
	{
		_sdssd_channel[i].tid = 0;
		_sdssd_channel[i].channel = NULL;
	}
	_control_channel = 0;
	atexit(_sdssd_exit_cleanup);

	//_nvp_debug_handoff();
}

void _sdssd_exit_cleanup(void)
{
	int i;
	DEBUG("_sdssd_exit_cleanup called.  Closing channel %p\n", *_sdssd_channel);
	for(i = 0; i < MAX_THREADS; i ++)
	{
		if(_sdssd_channel[i].channel != NULL)
			sdssd_io_CloseChannel2(_sdssd_channel[i].channel);
	}
	sdssd_io_CloseChannel2(_control_channel);
	free(_sdssd_channel);
	free(_sdssd_fd_lookup);
	_sdssd_channel = NULL; 	
	_sdssd_fd_lookup = NULL;
}

void *_sdssd_getchannel()
{
	int tid = gettid(); 
	int index; 
	char* dev = SDSSD_CHAR_DEVICE_PATH;
	if(!tid)
	{
		ERROR("No Thread id");
		assert(0); 
	}
	if(!dev) {
			WARNING("Couldn't find a sdssd device at %s!\n", SDSSD_CHAR_DEVICE_PATH);
			dev = "NOTFOUND";
	}

	//should use either locks or atomics 
	index= Hash((char *)&tid, MAX_THREADS); 
	while(true)
	{
		if(_sdssd_channel[index].tid)
		{
			if(_sdssd_channel[index].tid == tid)
				return _sdssd_channel[index].channel;
			else{
				ERROR("Tid collision in hash table %d, %d", tid, _sdssd_channel[index].tid);
				assert(0); 
			}	
		}else {
			DEBUG("Opening sdssd channel...\n");
			void *channel; 
			sdssd_io_OpenChannel2(dev, &channel); 
			if(!__sync_bool_compare_and_swap(&_sdssd_channel[index].channel, NULL, channel))
			{
				sdssd_io_CloseChannel2(channel); 
				continue; 
			}else {
				_sdssd_channel[index].tid = tid;
				return channel;
			}
		}
	}
}

RETT_OPEN _sdssd_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_OPEN\n");

	int result;
	int managed;

	struct stat sdssd_st;
	struct stat file_st;

	char* path_to_stat = (char*) calloc(strlen(path)+1, sizeof(char));
	strcpy(path_to_stat, path);
	
	char *dev = SDSSD_CHAR_DEVICE_PATH;
	if(!dev) {
		WARNING("Couldn't find a sdssd device at %s!\n", SDSSD_CHAR_DEVICE_PATH);
		dev = "NOTFOUND";
	}
	if(!_control_channel)
		sdssd_io_OpenChannel2(dev,&_control_channel);

	DEBUG("by popular demand, O_SYNC will be added (if not already present)\n");
	oflag |= O_SYNC;

	/////////  Let's filter out anything that's not on a sdssd device

	if( stat(SDSSD_BLOCK_DEVICE_PATH, &sdssd_st) ) {
		WARNING("Failed to get stats for sdssd device: \"%s\"\n", strerror(errno));
		assert(0);
		return -1;
		DEBUG("ignoring error...  this time.\n");
	}

	dev_t sdssd_dev_id = major(sdssd_st.st_rdev);

	DEBUG("Got major number for sdssd device \"%s\" %i\n", SDSSD_BLOCK_DEVICE_PATH, (int)sdssd_dev_id);

	if(sdssd_dev_id != ST_SDSSD_DEVICE_ID) {
		WARNING("Didn't get the expected device id for sdssd device! (expected %i, got %i)\n", (int)ST_SDSSD_DEVICE_ID, (int)sdssd_dev_id);
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


	if( major(file_st.st_dev) == ST_SDSSD_DEVICE_ID)
	{
		DEBUG("st_dev matches expected major number (%i): %s will be managed.\n", ST_SDSSD_DEVICE_ID, path);
		managed = 1;
	} else if(major(file_st.st_rdev) == ST_SDSSD_DEVICE_ID) {
		DEBUG("major(st_dev) was %i, but major(st_rdev) matched expected major number (%i): %s will be managed.\n", 
			(int)major(file_st.st_rdev), ST_SDSSD_DEVICE_ID, path);
		managed = 1;
//	} else if( S_ISREG(file_st.st_mode) && getenv(ENV_FILTER_OVERRIDE)) {
//		DEBUG("major(st_dev) was %i and major(st_rdev) was %i, but filter OVERRIDE enabled and %s is a regular file: it will be managed!\n", (int)major(file_st.st_dev), (int)major(file_st.st_rdev), path);
//		managed = 1;
	} else {
		DEBUG("major(st_dev) was %i and major(st_rdev) was %i, but we wanted %i: %s will not be managed.\n", 
			(int)major(file_st.st_dev), (int)major(file_st.st_rdev), ST_SDSSD_DEVICE_ID, path);
		managed = 0;
	}


	if(managed) {
		if( (S_ISREG(file_st.st_mode)==0) && (FLAGS_INCLUDE(oflag, O_CREAT) && file_st.st_mode == S_IFDIR) ) {
			DEBUG("How did a non-regular file get in to _sdssd_OPEN?\npath: %s\npath to stat: %s\n", path, path_to_stat);
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
		DEBUG("_sdssd_OPEN: sdssd will manage \"%s\"\n", path);
	} else {
		DEBUG("_sdssd_OPEN: sdssd will NOT manage \"%s\" (got device major number %i, wanted %i)\n",
			path, major(file_st.st_dev), ST_SDSSD_DEVICE_ID);
	}

	/////////  Done filtering


	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		result = _sdssd_fileops->OPEN(path, oflag, mode);
	} else {
		if(access(path, F_OK)) {
			DEBUG("File \"%s\" doesn't exist (and O_CREAT isn't set)\n", path);
			// TODO: errno?
			return -1;
		}
		result = _sdssd_fileops->OPEN(path, oflag);
	}

	if (result == -1) {
	     return -1;
	}
	

	if(_sdssd_fd_lookup[result]) {
		ERROR("FD %i already existed in the _sdssd_fd_lookup table!\n", result);
	}

	struct stat st;
	if( stat(path, &st) ) {
		DEBUG("Failed to get file stats: %s\n", strerror(errno));
		return -1;
	}

	_sdssd_fd_lookup[result] = (struct SDSSD_FD*) calloc(1, sizeof(struct SDSSD_FD));

	int i;
	for(i=0; i<OPEN_MAX; i++) {
		if(_sdssd_fd_lookup[i] && _sdssd_fd_lookup[i]->serialno == st.st_ino) {
			DEBUG("This file is already open in _sdssd_...  just an FYI!\n");
		//	_sdssd_fd_lookup[result]->length = _sdssd_fd_lookup[i]->length;
			break;
		}
	}
	
	_sdssd_fd_lookup[result]->offset  = (off64_t*) calloc(1, sizeof(off64_t));
	*_sdssd_fd_lookup[result]->offset = 0;
	_sdssd_fd_lookup[result]->flags   = oflag;
	_sdssd_fd_lookup[result]->path    = (char*) calloc(strlen(path)+1, sizeof(char));
	strcpy(_sdssd_fd_lookup[result]->path, path);
	_sdssd_fd_lookup[result]->serialno= st.st_ino;
	_sdssd_fd_lookup[result]->managed = managed;


	if(sdssd_io_FileOpened(_control_channel, result)) {
		ERROR("sdssd_FileOpened failed, somehow!\n");
		assert(0);
	}
	
	return result;
}


RETT_CLOSE _sdssd_CLOSE(INTF_CLOSE)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_CLOSE\n");

	int result = _sdssd_fileops->CLOSE(CALL_CLOSE);

	if((_sdssd_fd_lookup[file]->managed) && (sdssd_io_FileClosed(_control_channel, file))) {
		ERROR("sdssd_FileClosed failed, somehow!\n");
		assert(0);
	}
	
	free(_sdssd_fd_lookup[file]);
	_sdssd_fd_lookup[file]= NULL;

	return result;
}


RETT_READ _sdssd_READ(INTF_READ)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_READ(%i, %p, %i)\n", file, buf, (int)length);

	if(length < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(length == 0)
	{
		return 0;
	}

	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];

	off64_t file_offset = *mfd->offset;
	
	// printf("File number: %i\nFile offset: %i\nFile length: %i\nRequested read length: %i\n", file, file_offset, file_length, (int)length);
	DEBUG("performing sdssd_Read(%i, %i, %i, %p)...\n", file, file_offset, (int)length, buf );

	int result;
	if(mfd->managed)
	{
		void *channel  = _sdssd_getchannel();
		if(!channel)
			assert(0);	
		result = sdssd_io_Read2( channel, file, file_offset, length, buf );
		if(result < 0) {
			DEBUG("sdssd_Read(%p, %i, %li, %li, %p) failed! (error code %i)\n",
				channel, file, file_offset, length, buf, result );
			return result;
		}

		*mfd->offset += result;
	}
	else
	{
		result = _sdssd_fileops->READ(CALL_READ);
	}

	return result;
}


RETT_WRITE _sdssd_WRITE(INTF_WRITE)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_WRITE(%i, %p, %i)\n", file, buf, (int)length);
	
	if(length < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(length == 0)
	{
		return 0;
	}

	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];

	off64_t file_offset = *mfd->offset;

	int result;
	
	if(mfd->managed)
	{
		 void *channel  = _sdssd_getchannel();
		DEBUG("performing sdssd_Write(%p, %i, %li, %li, %p)\n", channel, file, file_offset, length, buf);
	
		result = sdssd_io_Write2(channel, file, file_offset, length, buf );

		if(result < 0) {
			DEBUG("sdssd_Write(%p, %i, %li, %li, %p) failed! (error code %i)\n",
				channel, file, file_offset, length, buf, result );
			return result;
		}

		*mfd->offset += result;
	}
	else
	{
		DEBUG("performing %s->WRITE(CALL_WRITE)\n", _sdssd_fileops->name);
		result = _sdssd_fileops->WRITE(CALL_WRITE);
	}

	return result;
}


RETT_SEEK _sdssd_SEEK(INTF_SEEK)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_SEEK\n");
	
	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];

	if(!mfd->managed) {
		return _sdssd_fileops->SEEK(CALL_SEEK);
	}

	return _sdssd_SEEK64(CALL_SEEK);
}


RETT_SEEK64 _sdssd_SEEK64(INTF_SEEK64)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_SEEK64\n");
	
	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];
	
	if(!mfd->managed) {
		return _sdssd_fileops->SEEK64(CALL_SEEK64);
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


RETT_TRUNC _sdssd_TRUNC(INTF_TRUNC)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_TRUNC\n");

	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];
	
	if(!mfd->managed) {
		return _sdssd_fileops->TRUNC(CALL_TRUNC);
	}

	return _sdssd_TRUNC64(CALL_TRUNC);

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
		int result = sdssd_Write( *mfd->channel, file, length-1, 1, "\0" );
		if(result != 1) {
			ERROR("Failed to extend the file by writing 1 byte at position %i: wrote %i bytes\n", (int)length-1, result);
		}
		return result;
	}

	if(*(mfd->offset) > length) {
		DEBUG("offset was beyond the length of the file... but I'm not changing it\n");
	}

	return _sdssd_fileops->TRUNC(CALL_TRUNC);
*/
}


RETT_TRUNC64 _sdssd_TRUNC64(INTF_TRUNC64)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_TRUNC64\n");

	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];
	
	if(!mfd->managed) {
		return _sdssd_fileops->TRUNC64(CALL_TRUNC64);
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
		 void *channel  = _sdssd_getchannel();
		DEBUG("new length (%i) was >= current length (%i): EXTENDING the file\n", (int)length, file_length );
		int result = sdssd_io_Write2(channel, file, length-1, 1, "\0" );
		if(result != 1) {
			ERROR("Failed to extend the file by writing 1 byte at position %i: wrote %i bytes\n", (int)length-1, result);
		}
		return result;
	}

	if(*(mfd->offset) > length) {
		DEBUG("offset was beyond the length of the file... but I'm not changing it\n");
	}

	return _sdssd_fileops->TRUNC(CALL_TRUNC);
}


RETT_IOCTL _sdssd_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);
	
	DEBUG("CALL: _sdssd_IOCTL\n");

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _sdssd_fileops->IOCTL(file, request, third);
	
	return result;
}


RETT_DUP _sdssd_DUP(INTF_DUP)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_DUP\n");
DEBUG("_sdssd_DUP was assigned fileops %p\n", _sdssd_fileops);

	assert(_sdssd_fileops);
DEBUG("that corresponds to %s\n", _sdssd_fileops->name);
	assert(_sdssd_fileops->DUP);
DEBUG("preparing to call %s->DUP(%i)\n", _sdssd_fileops->name, file);	
	int result = _sdssd_fileops->DUP(CALL_DUP);
DEBUG("sdssd_fileops->DUP returned %i\n", result);
	
	if(!_sdssd_fd_lookup[file]->managed) { return result; }

	if( (result < 0) || (result == file) ) {
		DEBUG("DUP failed.\n");
		return result;
	}

DEBUG("does _sdssd_fd_lookup[%i] exist? (_sdssd_fd_lookup = %p)\n", result, _sdssd_fd_lookup);
DEBUG("attempting to access _sdssd_fd_lookup[%i]\n", result);
DEBUG("_sdssd_fd_lookup[%i]=%p\n", result, _sdssd_fd_lookup[result]);
DEBUG("file = %i\n", file);

	if(_sdssd_fd_lookup[result]) {
		ERROR("Moneta already has a FD allocated for %i!\n", result);
	} else {
		DEBUG("sdssd_dup is allocating a new fd of size %i\n", (int)sizeof(struct SDSSD_FD));	
		_sdssd_fd_lookup[result] = (struct SDSSD_FD*) calloc(1, sizeof(struct SDSSD_FD));
		if(_sdssd_fd_lookup[result] == NULL) {
			ERROR("_sdssd_DUP FAILED to calloc 1 struct of size %i to hold a SDSSD_FD!\n", (int)sizeof(struct SDSSD_FD));
			assert(0);
		} else {DEBUG("calloc completed %p, file=%i\n", _sdssd_fd_lookup[result], file);}
	}

	DEBUG("_sdssd_fd_lookup[file (%i)] = %p\n", file, _sdssd_fd_lookup[file]);

	_sdssd_fd_lookup[result]->offset  = _sdssd_fd_lookup[file]->offset;
	_sdssd_fd_lookup[result]->flags   = _sdssd_fd_lookup[file]->flags;
DEBUG("sdssd_DUP returning success (%i)\n", result);
	return result;
}


RETT_DUP2 _sdssd_DUP2(INTF_DUP2)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);
//_nvp_debug_handoff();
	DEBUG("CALL: _sdssd_DUP2\n");
	
	int result = _sdssd_fileops->DUP2(CALL_DUP2);

	if (file >= 0 && file <= 2) return result;
	
	assert(_sdssd_fd_lookup[file]);
	if(!_sdssd_fd_lookup[file]->managed) { return result; }

	if(result != fd2) {
		ERROR("DUP2 call had an error.\n");
	} else { 
		DEBUG("DUP2 call completed successfully.\n");
	}

	if(result < 0) {
		ERROR("DUP2 failed.\n");
		return result;
	}
	
	if(!_sdssd_fd_lookup[result]) {
		ERROR("Moneta DIDN'T already have a FD allocated for %i!\n", fd2);
		_sdssd_fd_lookup[result] = (struct SDSSD_FD*) calloc(1, sizeof(struct SDSSD_FD));
	}

	assert(_sdssd_fd_lookup[result]);
	assert(_sdssd_fd_lookup[file]->offset);

	_sdssd_fd_lookup[result]->offset  = _sdssd_fd_lookup[file]->offset;
	_sdssd_fd_lookup[result]->flags   = _sdssd_fd_lookup[file]->flags;

	return result;
}


RETT_READV _sdssd_READV(INTF_READV)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_READV\n");

	if(!_sdssd_fd_lookup[file]->managed) {
		return _sdssd_fileops->READV(CALL_READV);
	}
	
	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _sdssd_READ(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_sdssd_READV failed\n");
		return -1;
	}

	return 0;
}


RETT_WRITEV _sdssd_WRITEV(INTF_WRITEV)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_WRITEV\n");

	if(!_sdssd_fd_lookup[file]->managed) {
		return _sdssd_fileops->WRITEV(CALL_WRITEV);
	}

	//TODO: opportunities for optimization exist here

	int fail = 0;

	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _sdssd_WRITE(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_sdssd_WRITEV failed\n");
		return -1;
	}

	return 0;
}

RETT_FORK _sdssd_FORK(INTF_FORK)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	DEBUG("CALL: _sdssd_FORK\n");

	RETT_FORK result = _sdssd_fileops->FORK(CALL_FORK);

	if(result < 0)
	{
		ERROR("_sdssd_FORK->%s_FORK failed!\n", _sdssd_fileops->name);
	}
	else  if(result==0)
	{
		DEBUG("Child process is calling sdssd_ReopenChannel\n");

	}
	else
	{
		DEBUG("Moneta parent has finished fork.\n");
	}

	return result;
}

RETT_PREAD _sdssd_PREAD(INTF_PREAD)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);
	
	if(count < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(count == 0)
	{
		return 0;
	}

	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];

	if(!mfd->managed) {
		return _sdssd_fileops->PREAD(CALL_PREAD);
	}

	void *channel = _sdssd_getchannel();

	DEBUG("CALL: _sdssd_PREAD(%i, %p, %i, %i)\n", file, buf, (int)count, (int)offset);

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

	return (RETT_PREAD) sdssd_io_Read2(channel, file, offset, count, buf);
}

RETT_PWRITE _sdssd_PWRITE(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	if(count < 0)
	{
		errno = EINVAL;
		return -1;
	}
	if(count == 0)
	{
		return 0;
	}

	struct SDSSD_FD *mfd = _sdssd_fd_lookup[file];

	if(!mfd->managed) {
		return _sdssd_fileops->PWRITE(CALL_PWRITE);
	}

	DEBUG("CALL: _sdssd_PWRITE(%i, %p, %i, %i)\n", file, buf, (int)count, (int)offset);

	#if CHECK_ALIGNMENT
	DEBUG("Checking alignment to %i bytes\n", CHECK_ALIGNMENT);
	if(offset%CHECK_ALIGNMENT) {
		DEBUG("offset %i is not %i-byte aligned.\n", (int)offset, CHECK_ALIGNMENT);
	}
	if(count%CHECK_ALIGNMENT) {
		DEBUG("count %i is not %i-byte aligned.\n", (int)count, CHECK_ALIGNMENT);
	}
	#endif
	void *channel = _sdssd_getchannel();
	int r = sdssd_io_Write2(channel, file, offset, count, buf);
	DEBUG("CALL: _sdssd_PWRITE(%i, %p, %i, %i), returned %d\n", file, buf, (int)count, (int)offset, r);
	
	return (RETT_PWRITE) r;
}

RETT_FSYNC _sdssd_FSYNC(INTF_FSYNC)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	if(!_sdssd_fd_lookup[file]->managed) {
		return _sdssd_fileops->FSYNC(CALL_FSYNC);
	}

	DEBUG("CALL: _sdssd_FSYNC: CRUSHED\n");

	return 0;
}

RETT_FDSYNC _sdssd_FDSYNC(INTF_FDSYNC)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	if(!_sdssd_fd_lookup[file]->managed) {
		return _sdssd_fileops->FDSYNC(CALL_FDSYNC);
	}

	DEBUG("CALL: _sdssd_FDSYNC: CRUSHED\n");

	return 0;
}

RETT_MKSTEMP _sdssd_MKSTEMP(INTF_MKSTEMP)
{
	CHECK_RESOLVE_FILEOPS(_sdssd_);

	char* suffix = file + strlen(file) - 6; // char* suffix = strstr(file, "XXXXXX");

	DEBUG("Called _sdssd_mkstemp with template %s; making a new filename...\n", file);

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

