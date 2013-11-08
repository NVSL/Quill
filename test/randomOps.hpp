OP(open, 20,
   {
	//int fd = open(RandFileName(), RandFlags() /*& (~(O_SHLOCK|O_EXLOCK))*/, RandPerms());
	int flags = RandFlags();
	if(flags & O_WRONLY) { 
		flags &= ~O_WRONLY;
		flags |= O_RDWR;
	}
	int fd = open(RandFileName(), flags, RandPerms());
	fdList.push_back(fd);
	RandFD() = fd;
       
   })

OP(close, 50,
   {
	close(RandFD());
   })

OP(read, 50,
   {
	  char buf[BUF_SIZE];
	  read(RandFD(), buf, RandBufSize());
     })

OP(write, 50,
   {
	  uint64_t buf[BUF_SIZE/sizeof(uint64_t)];
	  int size = RandBufSize()/sizeof(uint64_t);
	  for(int i = 0; i < size; i++) {
	       buf[i] = RandLongInt();
	  }
	  write(RandFD(), buf, RandBufSize());
     })

OP(pread, 50,
   {
	char buf[BUF_SIZE];
	pread(RandFD(), buf, RandBufSize(), RandOffset());
   })

OP(pwrite, 50,
   {
	uint64_t buf[BUF_SIZE/sizeof(uint64_t)];
	int size = RandBufSize()/sizeof(uint64_t);
	for(int i = 0; i < size; i++) {
	     buf[i] = RandLongInt();
	}
	pwrite(RandFD(), buf, RandBufSize(), RandOffset());
   })

OP(seek, 50,
   {
	lseek(RandFD(), RandOffset(), RandWhence());
   })

OP(truncate, 20, 
   {
	ftruncate(RandFD(), RandOffset());
   })

OP(dup, 20,
   {
	RandFD() = dup(RandFD());
   })

OP(dup2, 20,
   {
	dup2(RandFD(),RandFD());
   })

OP(fsync, 0,
   {
	fsync(RandFD());
   })

OP(fdsync, 0,
   {
	fdatasync(RandFD());
   })
     
OP(fork, 1,
   {
	if (fork() != 0) {_exit(0);}
	//if(fork() && (thread_count > MAX_THREAD_COUNT)) { _exit(0); } else { thread_count++; }
   })
     
OP(exec, 0,
   {
	int r = repCount - iterationCounter;
// 	char argv[2][1024];
// 	strcpy(argv[0], theArgv[0]);
	sprintf(theArgv[1], "%d", r);
	//	std::cerr << "execing with " << argv[0] << " " << argv[1] << " to go\n";
	execve(theArgv[0], theArgv, theEnvp);
   })
     
OP(dropFD, 0,
   {
	RandFD() = -1;
   })

OP(unlink, 0,
   {
	unlink(RandFileName());
   })

OP(reset, 0,
   {
	for(FDListType::iterator i = fdList.begin(); i != fdList.end(); i++) {
	     close(*i);
	}
	fdList.clear();
     
	for(int i = 0; i < FD_COUNT; i++) {
	     fds[i] = open(fileNames[i], O_CREAT|O_TRUNC|O_RDWR, -1);
	     fdList.push_back(fds[i]);
	}

   })
