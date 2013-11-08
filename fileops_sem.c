// a module which enforces mutual exclusion

#include "nv_common.h"

#include <pthread.h>

#define ENV_SEM_FOP "NVP_SEMGUARD_FOP"


pthread_rwlock_t *mutex;



BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _sem_, ALLOPS_FINITEPARAMS_WPAREN)

RETT_OPEN _sem_OPEN(INTF_OPEN);
RETT_IOCTL _sem_IOCTL(INTF_IOCTL);

void _sem_init2(void);


MODULE_REGISTRATION_F("semguard", _sem_, _sem_init2(); )


void _sem_init2(void)
{
	mutex = (pthread_rwlock_t*) calloc(OPEN_MAX, sizeof(pthread_rwlock_t));

	int i;
	for(i=0; i<OPEN_MAX; i++)
	{
		pthread_rwlock_init(mutex+i, NULL);
	}
}


RETT_OPEN _sem_OPEN(INTF_OPEN)
{
	CHECK_RESOLVE_FILEOPS(_sem_);

	DEBUG("CALL: _sem_OPEN\n");
	if(access(path, F_OK) && !(FLAGS_INCLUDE(oflag, O_CREAT)) ) {
		DEBUG("File doesn't exist (and O_CREAT) isn't set)\n");
		return -1;
	}

	int result;

	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		result = _sem_fileops->OPEN(path, oflag, mode);
	} else {
		if(access(path, F_OK)) {
			DEBUG("File \"%s\" doesn't exist (and O_CREAT isn't set)\n", path);
			// TODO: errno?
			return -1;
		}
		result = _sem_fileops->OPEN(path, oflag);
	}
	
	return result;
}


//#define SEM_OPS_RDLOCK (READ) (WRITE) (SEEK) (TRUNC) (SEEK64) (TRUNC64) (PREAD) (PWRITE)
//#define SEM_OPS_WRLOCK (CLOSE)
//#define SEM_OPS_NOLOCK (FORK) (READV) (WRITEV) (PIPE) (FSYNC) (FDSYNC)
#define SEM_OPS_RDLOCK (READ) (PREAD) (READV)
#define SEM_OPS_WRLOCK (CLOSE) (WRITE) (SEEK) (TRUNC) (SEEK64) (TRUNC64) (PWRITE) (WRITEV) (FSYNC) (FDSYNC)
#define SEM_OPS_NOLOCK (FORK) (PIPE)

#define SEM_LOCK_RD(FUNCT) \
	CHECK_RESOLVE_FILEOPS(_sem_); \
	DEBUG("Calling _sem_" #FUNCT " with read exclusion.\n"); \
	pthread_rwlock_rdlock(mutex+file); \
	DEBUG("File %i locked.\n", file);

#define SEM_LOCK_WR(FUNCT) \
	CHECK_RESOLVE_FILEOPS(_sem_); \
	DEBUG("Calling _sem_" #FUNCT " with write exclusion.\n"); \
	pthread_rwlock_wrlock(mutex+file); \
	DEBUG("File %i locked.\n", file);

#define SEM_UNLOCK \
	DEBUG("Unlocking file %i\n", file); \
	pthread_rwlock_unlock(mutex+file); \
	DEBUG("File %i unlocked.\n", file);

#define SEM_FUNCT_RDLOCK(FUNCT) \
	RETT_##FUNCT _sem_##FUNCT(INTF_##FUNCT) { \
		SEM_LOCK_RD(FUNCT); \
		RETT_##FUNCT result = _sem_fileops->FUNCT(CALL_##FUNCT); \
		SEM_UNLOCK; \
		return result; \
	}


#define SEM_FUNCT_WRLOCK(FUNCT) \
	RETT_##FUNCT _sem_##FUNCT(INTF_##FUNCT) { \
		SEM_LOCK_WR(FUNCT); \
		RETT_##FUNCT result = _sem_fileops->FUNCT(CALL_##FUNCT); \
		SEM_UNLOCK; \
		return result; \
	}

#define SEM_FUNCT_NOLOCK(FUNCT) \
	RETT_##FUNCT _sem_##FUNCT(INTF_##FUNCT) { \
		CHECK_RESOLVE_FILEOPS(_sem_); \
		DEBUG("Calling _sem_" #FUNCT ", but not doing any controls.\n"); \
		return _sem_fileops->FUNCT(CALL_##FUNCT); \
	}


#define SEM_FUNCT_RDLOCK_IWRAP(r, data, elem) SEM_FUNCT_RDLOCK(elem)
#define SEM_FUNCT_NOLOCK_IWRAP(r, data, elem) SEM_FUNCT_NOLOCK(elem)
#define SEM_FUNCT_WRLOCK_IWRAP(r, data, elem) SEM_FUNCT_WRLOCK(elem)

BOOST_PP_SEQ_FOR_EACH(SEM_FUNCT_NOLOCK_IWRAP,  z, SEM_OPS_NOLOCK )
BOOST_PP_SEQ_FOR_EACH(SEM_FUNCT_RDLOCK_IWRAP,  z, SEM_OPS_RDLOCK )
BOOST_PP_SEQ_FOR_EACH(SEM_FUNCT_WRLOCK_IWRAP,  z, SEM_OPS_WRLOCK )

RETT_IOCTL _sem_IOCTL(INTF_IOCTL)
{
	SEM_LOCK_RD(IOCTL);

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _sem_fileops->IOCTL(file, request, third);
	
	SEM_UNLOCK;
	
	return result;
}

RETT_DUP _sem_DUP(INTF_DUP)
{
	SEM_LOCK_WR(DUP);

	int result = _sem_fileops->DUP(CALL_DUP);

	if(result < 0) {
		DEBUG("call to DUP failed.\n");
		return result;
	}
	
	SEM_UNLOCK;

	return result;
}


RETT_DUP2 _sem_DUP2(INTF_DUP2)
{
	CHECK_RESOLVE_FILEOPS(_sem_);
	DEBUG("Calling _sem_DUP2 with exclusion.\n");
	
	DEBUG("Locking files %i and %i\n", file, fd2);
	
	while(1)
	{
		if(pthread_rwlock_wrlock(mutex+file)) {		// get a lock on the first file.
		} else if(pthread_rwlock_trywrlock(mutex+fd2 )) {	// try and get a lock on the second file.  
			pthread_rwlock_unlock(mutex+file);		// if unsuccessful, release the first one and start over. (avoid deadlocks with other threads)
		} else { break; } 					// if both locks succeeded, go ahead
	}

	DEBUG("Files %i and %i locked.\n", file, fd2);

	int result = _sem_fileops->DUP2(CALL_DUP2);

	if(result != fd2) {
		DEBUG("call to DUP2 failed.\n");
	}

	DEBUG("Unlocking files %i and %i\n", file, fd2);
	pthread_rwlock_unlock(mutex+file);
	pthread_rwlock_unlock(mutex+fd2);
	DEBUG("Files %i and %i unlocked.\n", file, fd2);

	return result;
}

