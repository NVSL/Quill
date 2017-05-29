#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "FastRand.hpp"
#include <iostream>
#include <map>
#include <vector>
#include <iomanip>
#include "test_common.h"
#include <sys/time.h>


#define FD_COUNT 16
#define FILE_NAME_COUNT 16
#define BUF_SIZE (1024*1024)
#define FLAGS_RANGE ((O_APPEND|O_ASYNC|O_CREAT|O_DIRECT|O_DIRECTORY|O_EXCL|O_LARGEFILE|O_NOATIME|O_NOCTTY|O_NOFOLLOW|O_NONBLOCK|O_NDELAY|O_SYNC|O_TRUNC) * 4)
//#define FLAGS_RANGE ((O_RDONLY|O_WRONLY|O_RDWR|O_NONBLOCK|O_APPEND|O_CREAT|O_TRUNC|O_EXCL|O_SHLOCK|O_EXLOCK|O_NOFOLLOW|O_SYMLINK|O_EVTONLY) * 4)
#define PERMS_RANGE ((1 << 13) - 1)
#define MAX_OFFSET (1024*1024*128)
#define SEEK_WHENCE (4)

void _exit(int status);

int thread_count;
#define MAX_THREAD_COUNT 5

int fds[FD_COUNT];
char *fileNames[FILE_NAME_COUNT];

typedef void (OpFunc)(void);

template<long long int MIN, long long int MAX> 
class RandFunc {
     static uint64_t seed;
public:
     RandFunc(){}
     int operator() () {
	  return (RandLFSR(&seed) % (MAX - MIN)) + MIN;
     }
};

template<long long int MIN, long long int MAX> 
uint64_t RandFunc<MIN, MAX>::seed = rand();

RandFunc<0, FD_COUNT> RandFDIndex;
RandFunc<0,FILE_NAME_COUNT> RandFileNameIndex;
RandFunc<0, BUF_SIZE> RandBufSize;
RandFunc<0, FLAGS_RANGE> RandFlags;
RandFunc<0, PERMS_RANGE> RandPerms;
RandFunc<0, -1> RandLongInt;
RandFunc<0, MAX_OFFSET> RandOffset;
RandFunc<-2, 2> RandWhence;

int & RandFD() {return fds[RandFDIndex()];}
char * RandFileName() {return fileNames[RandFileNameIndex()];}

int errorCount = 0;

extern "C" {
     int do_test_eval(int argc, char* argv[]);
}

#define OP(OPNAME,DIST, CODE) +1
const int OpCount = 0 
#include"randomOps.hpp"
     ;
#undef OP

int distribution[] = {
#define OP(OPNAME,DIST,CODE) DIST,
#include"randomOps.hpp"
#undef OP
};

#define OP(OPNAME,DIST, CODE) + DIST
const int DistributionMax = 0 
#include"randomOps.hpp"
     ;
#undef OP

typedef std::map<std::string, int> OpCountMapType;
OpCountMapType opCountMap;
OpCountMapType opErrorMap;

typedef std::vector<int> FDListType;
FDListType fdList;

int iterationCounter = 0;
int repCount = 10000;

#define OP(OPNAME,DIST,CODE) void op_##OPNAME() { /*std::cerr << #OPNAME "\n"; */opCountMap[#OPNAME]++; errno = -1; CODE; if (errno != -1) {/*perror("");*/opErrorMap[#OPNAME]++;} }
#include"randomOps.hpp"
#undef OP

OpFunc *Ops[] = {
#define OP(OPNAME,DIST,CODE) op_##OPNAME,
#include"randomOps.hpp"
#undef OP
     NULL
};
     
int do_test_eval(int argc, char* argv[]) {
	thread_count = 1;

     if (argc > 1) {
	  repCount = atoi(argv[1]);
     }

	time_t time_seed = time(NULL);
//	time_t time_seed = 1301629219;

	printf("The seed for this run is %lu\n", time_seed);

     srand(time_seed);	// temporarily setting the seed


     for(int i = 0; i < FILE_NAME_COUNT; i++) {
	  fileNames[i] = new char[1024];
	  sprintf(fileNames[i], "randTestFiles/file_%d.data", i);
     }
     
     op_reset();


     uint64_t seed = rand();
     for(iterationCounter = 0; iterationCounter < repCount; iterationCounter++) {
	  int op = RandLFSR(&seed) % DistributionMax;
	  int s = 0;
	  for(int j = 0; j < OpCount; j++) {
	       s += distribution[j];
	       if (op < s) {
		    Ops[j]();
		    break;
	       }
	  }
	  //	  std::cerr << iterationCounter << " ";
     }

     std::cerr << "\n";
     for(OpCountMapType::iterator i = opCountMap.begin(); i != opCountMap.end(); i++) {
	  std::cerr 
	       << std::setw(10) << i->first 
	       << ": " 
	        << std::setw(10) << i->second
	       << "(" 
	       << std::setw(4) << (100.0*static_cast<float>(i->second)/static_cast<float>(repCount)) 
	       << "%),  errors: " 
	       << std::setw(4) << opErrorMap[i->first] 
	       << "(" 
	       << std::setw(3) << std::setprecision(3) << (100.0*static_cast<float>(opErrorMap[i->first])/static_cast<float>(i->second)) 
	       << "%)\n";
     }
     return 0;
}
