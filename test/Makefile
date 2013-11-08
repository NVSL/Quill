# Makefile for nvmfileops tests

# this include was moved after all: because it contains a target
# include $(BEE3HOME)/Tools/PosixNVM/common.mk


TESTTREE0  = "test_fake.tree"
TESTTREE1  = "test_no_target.tree"
TESTTREE2  = "test_no_hub.tree"
TESTTREE3  = "test_badformat1.tree"
TESTTREE4  = "test_badformat2.tree"
TESTTREE5  = "test_badhub.tree"

TESTTREE10 = "nvp_posix.tree"
TESTTREE11 = "nvp_count.tree"
TESTTREE12 = "nvp_compare_posix_posix.tree"
TESTTREE13 = "nvp_compare_posix_count.tree"
#TESTTREE14 = "nvp_kill_nonreg.tree"

TESTTREE20 = "nvp_moneta.tree"
TESTTREE21 = "nvp_compare_moneta.tree"

TESTTREE30 = "nvp_nvp.tree"
TESTTREE31 = "nvp_wrap_compare_posix_nvp.tree"

PASSTREENUMS = 10 11 12 13 30 31
FAILTREENUMS = 0 1 2 3 4 5

PASSMONETATREENUMS = 20 21
FAILMONETATREENUMS = 



all: test

include $(BEE3HOME)/test/PosixNVM/common.mk



# Run all tests for the current test tree without modifying the current test tree.
test: clean $(TESTS) process_results

clean:
	- chmod 700 nvmfileops_test5.txt
	rm -f *.o *.i *.i64 *.ci *.oi nvmfileops_test*.txt *.txt.uut *.result *.testexe *.i *.ci supertest.results static_copy *.testexe.bakedin xddtestfile.txt $(BDB_TESTS) $(OLTP_TESTS) randTestFiles/*

%.i : %.c
	$(CC) $(CFLAGS) -c -E $< -o $@

%.i64 : %.c
	$(CC) -D_LARGEFILE64_SOURCE $(CFLAGS) -c -E $< -o $@

%.ci : %.i
	indent < $< | grep -v '^#' > $@

%.oi : %.ci
	$(CC) $(CFLAGS) -x c -c $< -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PRECIOUS: %.testexe 
%.testexe: %.o harness.o
	$(LD) $(CFLAGS) $< -ldl harness.o ../nvp_printf.o -o $@

helloworld.testexe: helloworld.o
	$(CC) $< -ldl -o $@


# Set up preconditions assumed in tests: create (or delete) files with the appropriate contents.
.PHONY: setuptestfiles
setuptestfiles:
	@ echo "Setting up test files."
	@ echo -n "Hello" > nvmfileops_test1.txt
	@ echo -n "Croissant" > nvmfileops_test2.txt
	@ rm -f nvmfileops_test3.txt
	@ echo -n "" > nvmfileops_test4.txt
	@ rm -f nvmfileops_test5.txt; echo -n "You can't write this" > nvmfileops_test5.txt; chmod 400 nvmfileops_test5.txt
	@ echo -n "You can't read this" > nvmfileops_test6.txt; chmod 377 nvmfileops_test6.txt
	@ rm -f nvmfileops_test7.txt; ln -s nvmfileops_test1.txt nvmfileops_test7.txt
	@ rm -f nvmfileops_test8.txt; ln nvmfileops_test2.txt nvmfileops_test8.txt
	@ rm -rf randTestFiles; mkdir randTestFiles


.PRECIOUS: %.result
# Set up test files in the current directory.  Compile and run test.
%.result: %.testexe
	@ $(MAKE) setuptestfiles
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	-export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so $(MONETA_TEST)"; ./$< 2>&1 | (unset LD_PRELOAD; tee $(LOG_OUT_DIR)/$@ | $(MARKERRORS) )


# Prints formatted results for all run tests
process_results:
	@ $(foreach F, $(TESTS), $(MAKE) do_process PROCFILE=$(LOG_OUT_DIR)/$(F) PROCFILENAME=$(F) --no-print-directory; )


# Print a formatted output for the selected .result file
do_process:
	@ grep -a RESULT $(PROCFILE) | $(SPECIALCASEFORTESTTESTER) | $(HIGHLIGHTFAILURE)
	@ grep "MSG: Initializing the libnvp hub." $(PROCFILE) -L | $(MARKNOLOAD) | $(HIGHLIGHTFAILURE)
	@ grep RESULT $(PROCFILE) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)
	@ grep ERROR $(PROCFILE) | sed -e "s/\(ERROR:\)/$(PROCFILENAME): FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)
	@ grep Assertion $(PROCFILE) | sed -e "s/\(Assertion\)/$(PROCFILENAME): FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)
	


#################  Supertest  ##################
#                                              #
#       For all test trees, run all tests.     #
#                                              #
################################################

# Run all tests for all trees.
# Results for each tree are saved in supertest.results,
# and are played back at the end of all tests.
# TODO: change foreach numbers
.PHONY: supertest
supertest: supertestsetup
	@ $(foreach TN, $(FAILTREENUMS), $(MAKE) treetest_run treetest_process_shouldfail TESTNUM=$(TN) ; )
	@ $(foreach TN, $(PASSTREENUMS), $(MAKE) treetest_run treetest_process_shouldpass TESTNUM=$(TN) ; )
	@ echo "All tree tests completed."
	@ cat supertest.results

# Build test executables without running tests
.PHONY: supertestsetup
supertestsetup: clean
	rm -f supertest.results
	$(foreach T, $(TESTS), $(MAKE) $(subst .result,.testexe,$(T)) ; )

# Run all tests for a specific tree.
treetest_run:
	@ echo "" >> supertest.results; echo "$(TESTTREE$(TESTNUM))" >> supertest.results
	@ $(MAKE) setuptestfiles; rm -f *.result
	export NVP_TREE_FILE=$(LIBNVP_TREE_DIR)$(TESTTREE$(TESTNUM)); $(MAKE) $(TESTS)

treetest_process_shouldpass:
	$(MAKE) process_results >> supertest.results

treetest_process_shouldfail:
	$(MAKE) process_results | $(SWAPSUCCESSFAILURE) >> supertest.results





################  Moneta  ##################
#                                          #
# Run tests which require Moneta hardware. #
#                                          #
############################################

# Run the simplest possible test tree with the moneta device.
test_moneta: check_moneta clean
	$(MAKE) $(TESTS) MONETA_TEST=libmoneta.so process_results

simpletest_moneta:
	export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)$(TESTTREE20)"; $(MAKE) test_moneta

# Run all test trees, including those involving the moneta device.
# TODO: change foreach numbers
supertest_moneta: check_moneta clean
	@ $(foreach TN, $(FAILTREENUMS) $(FAILMONETATREENUMS), $(MAKE) treetest_run treetest_process_shouldfail TESTNUM=$(TN) MONETA_TEST=libmoneta.so ; )
	@ $(foreach TN, $(PASSTREENUMS) $(PASSMONETATREENUMS), $(MAKE) treetest_run treetest_process_shouldpass TESTNUM=$(TN) MONETA_TEST=libmoneta.so ; )
	@ echo "All moneta tree tests completed."
	@ cat supertest.results





#####################  XDD  ####################
#                                              #
#        Compare XDD on and off Moneta.        #
#                                              #
################################################

XDDEXEC  = $(BEE3HOME)/Workloads/xdd/bin/xdd.linux
XDDFLAGS =     -rwratio 50 -mbytes 54000 -noproclock -nomemlock -runtime 100 -target $(XDD_FILE)
#XDDFLAGS_NVP = -rwratio 50 -mbytes 54000 -noproclock -nomemlock -runtime 180 -target $(XDD_FILE_NVP)
XDDFLAGS_NVP = -mbytes 54000 -noproclock -nomemlock -runtime 2 -blocksize 4096 -reqsize 1 -dio -target $(XDD_FILE_NVP)
XDD_TESTS =     xdd_unmod.result xdd_wrap.result xdd_moneta.result
#XDD_TESTS_NVP = xdd_unmod.result xdd_wrap.result xdd_nvp.result xdd_compare_nvp.result
XDD_TESTS_NVP = xdd_unmod-rw1.result xdd_unmod-rw50.result xdd_unmod-rw100.result xdd_nvp-rw1.result xdd_nvp-rw50.result xdd_nvp-rw100.result
XDD_FILE = $(MONETA_MOUNT_DIR)/xddtestfile.txt
XDD_FILE_NVP = $(RAMDISK_TEST_DIR)xddtestfile.txt

XDD_WRAPPER_DIR = $(BEE3HOME)/test/PosixNVM/bin
XDD_WRAPPER_SCRIPTS = nvpUnmod nvpNVP
XDD_RATIOS = 1 50 100
XDD_NUM_THREADS = 1 8
XDD_LOG_FILENAME = xdd_nvp_$(SCRIPT)-tc$(THREADS)-rw$(RATIO).result

test_xdd_nvp:
	$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
	#$(MAKE) test_xdd_generate XDD_OFILE=$(XDD_FILE_NVP)
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	$(foreach SCRIPT, $(XDD_WRAPPER_SCRIPTS), $(foreach THREADS, $(XDD_NUM_THREADS), $(foreach RATIO, $(XDD_RATIOS), $(XDD_WRAPPER_DIR)/$(SCRIPT) $(XDDEXEC) $(XDDFLAGS_NVP) -rwratio $(RATIO) 2>&1 | tee $(LOG_OUT_DIR)/$(XDD_LOG_FILENAME); )))
	cd $(LOG_OUT_DIR); grep -H "Combined" $(foreach SCRIPT, $(XDD_WRAPPER_SCRIPTS), $(foreach THREADS, $(XDD_NUM_THREADS), $(foreach RATIO, $(XDD_RATIOS), $(XDD_LOG_FILENAME) ) )) ;
	
test_xdd_old_holder_temp:
	#grep "Combined" $(XDD_LOG_FILENAME) | sed -e "s/\(Combined\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE) ; ) ))
	#grep "Ending time for this run" $(XDD_LOG_FILENAME) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE) ; grep ERROR $(XDD_LOG_FILENAME) | sed -e "s/\(ERROR:\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE) ; grep Assertion $(XDD_LOG_FILENAME) | sed -e "s/\(Assertion\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE) ))

# create the test files for xdd.  Called automatically by test_xdd[_nvp]; no point in calling it manually
test_xdd_generate:
	dd if=/dev/zero of=$(XDD_OFILE) bs=1048576 count=56320 oflag=direct

test_xdd: check_moneta
	$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
	$(MAKE) test_xdd_generate XDD_OFILE=$(XDD_FILE)
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	$(XDDEXEC) $(XDDFLAGS) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_unmod.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_posix.tree"; $(XDDEXEC) $(XDDFLAGS) -target $(MONETA_MOUNT_DIR)/xddtestfile.txt 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_wrap.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so libmoneta.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_moneta.tree"; $(XDDEXEC) $(XDDFLAGS) -target $(MONETA_MOUNT_DIR)/xddtestfile.txt 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_moneta.result )
	@ cd $(LOG_OUT_DIR); grep "Combined" $(XDD_TESTS) | sed -e "s/\(Combined\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep "Ending time for this run" $(XDD_TESTS) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)

test_xdd_nvp_old:
	$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
	$(MAKE) test_xdd_generate XDD_OFILE=$(XDD_FILE_NVP)
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	$(XDDEXEC) $(XDDFLAGS_NVP) -rwratio 1 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_unmod-rw1.result )
	$(XDDEXEC) $(XDDFLAGS_NVP) -rwratio 50 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_unmod-rw50.result )
	$(XDDEXEC) $(XDDFLAGS_NVP) -rwratio 100 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_unmod-rw100.result )
	####- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_posix.tree"; $(XDDEXEC) $(XDDFLAGS_NVP) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_wrap.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_nvp.tree"; $(XDDEXEC) $(XDDFLAGS_NVP) -rwratio 1 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_nvp-rw1.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_nvp.tree"; $(XDDEXEC) $(XDDFLAGS_NVP) -rwratio 50 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_nvp-rw50.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_nvp.tree"; $(XDDEXEC) $(XDDFLAGS_NVP) -rwratio 100 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_nvp-rw100.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_nvp.tree"; $(XDDEXEC) $(XDDFLAGS_NVP) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_nvp.result )
	#- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_compare_posix_nvp.tree"; $(XDDEXEC) $(XDDFLAGS_NVP) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/xdd_compare_nvp.result )
	@ cd $(LOG_OUT_DIR); grep "Combined" $(XDD_TESTS_NVP) | sed -e "s/\(Combined\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep "Ending time for this run" $(XDD_TESTS_NVP) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep ERROR $(XDD_TESTS_NVP) | sed -e "s/\(ERROR:\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep Assertion $(XDD_TESTS_NVP) | sed -e "s/\(Assertion\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)




#####################  BDB  ####################
#                                              #
#        Compare BDB on and off Moneta.        #
#                                              #
################################################

BDB_TESTS = BDB_tree_unmod.result BDB_tree_wrap.result BDB_tree_moneta.result BDB_hash_unmod.result BDB_hash_wrap.result BDB_hash_moneta.result
BDB_TESTS_NVP = BDB_tree_unmod.result BDB_tree_wrap.result BDB_tree_nvp.result BDB_tree_compare_nvp.result
BDB_FLAGS_CREATE = -rt 600 -tc 1 -foot 50000 -file $(BDB_FILE_NVP) -create
BDB_FLAGS_RUN    = -rt 600 -tc 1 -foot 50000 -file $(BDB_FILE_NVP) -reload
BDB_FILE = $(MONETA_MOUNT_DIR)/test/
BDB_FILE_NVP = $(RAMDISK_TEST_DIR)bdbtest

# Create a test database for BDB.  DONE ONCE, DON'T DO IT AGAIN; it will be automatically loaded in test_bdb[_nvp]
test_bdb_create:
	@ echo "Creating BDB database in $(BDB_FILE_NVP) before copying it to $(LOAD_DIR_X)/bdb_database"
	$(BDB_EXEC_DIR)/BTree.exe BDB_Create_Database $(BDB_FLAGS_CREATE) | tee $(LOG_OUT_DIR)/BDB_tree_create.result
	#cp -r $(BDB_FILE_NVP)/ $(LOAD_DIR_X)/bdb_database

test_bdb: check_moneta
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	-rm -rf $(BDB_FILE)
	mkdir $(BDB_FILE)
	cp -r $(LOAD_DIR_X)/bdb_database/bdbtest $(BDB_FILE)
	#$(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_unmod $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_unmod.result )
	#- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_posix.tree"; $(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_wrap $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_wrap.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so libmoneta.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_moneta.tree"; $(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_moneta $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_moneta.result )
	@ cd $(LOG_OUT_DIR); grep "BDBtest_" $(BDB_TESTS) | sed -e "s/\(bdb\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep "BDBtest_" $(BDB_TESTS) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)


test_bdb_nvp:
	$(MAKE) -C$(BDB_EXEC_DIR) clean all
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	-rm -rf $(BDB_FILE_NVP)
	mkdir $(BDB_FILE_NVP)
	cp -r $(LOAD_DIR_X)/bdb_database/bdbtest/* $(BDB_FILE_NVP)
	$(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_unmod $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_unmod.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_posix.tree"; $(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_wrap $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_wrap.result )
	- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_nvp.tree"; $(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_nvp $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_nvp.result )
	#- export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_compare_posix_nvp.tree"; $(BDB_EXEC_DIR)/BTree.exe BDBtest_tree_compare_nvp $(BDB_FLAGS_RUN) 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/BDB_tree_compare_nvp.result | $(MARKERRORS) )
	@ cd $(LOG_OUT_DIR); grep "BDBtest_" $(BDB_TESTS_NVP) | sed -e "s/\(bdb\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep "BDBtest_" $(BDB_TESTS_NVP) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep ERROR $(BDB_TESTS_NVP) | sed -e "s/\(ERROR:\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)
	@ cd $(LOG_OUT_DIR); grep Assertion $(BDB_TESTS_NVP) | sed -e "s/\(Assertion\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)


####################  OLTP  ####################
#                                              #
#        Compare OLTP on and off Moneta.       #
#                                              #
################################################

OLTP_TESTS = oltp_unmod.result oltp_wrap.result oltp_moneta.result oltp_nvp.result oltp_compare_nvp.result

EXPORT_UNMOD = 
#EXPORT_WRAP = nvpOLTPwrap
#EXPORT_MONETA = nvpOLTPmoneta
EXPORT_WRAP = export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_posix.tree";
EXPORT_MONETA = export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so libmoneta.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_moneta.tree";
EXPORT_NVP = export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_nvp.tree";
EXPORT_COMPARE_NVP = export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_wrap_compare_posix_nvp.tree";


# TODO: rewrite this to properly use tree structure
test_oltp: check_moneta
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	rm -f $(MONETA_MOUNT_DIR)/mysqld.log
	$(MAKE)  setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_UNMOD  2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_unmod.result )
	sleep 60
	-$(MAKE) setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_WRAP   2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_wrap.result )
	sleep 60
	-$(MAKE) setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_MONETA 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_moneta.result )
	@grep "transactions:" $(OLTP_TESTS) | sed -e "s/\(transactions:\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "transactions:" $(OLTP_TESTS) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)
	@grep "Aborted" $(OLTP_TESTS) | sed -e "s/\(Aborted:\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "Segmentation" $(OLTP_TESTS) | sed -e "s/\(Segmentation\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)

test_oltp_nvp:
	@ echo -n "Running test $@ on host "; hostname | awk '{ printf "%s ", $$0 }'; echo -n "in directory "; pwd
	rm -f $(LOCAL_TEST_DIR)/mysqld.log
	$(MAKE)  setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_UNMOD  2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_unmod.result )
	sleep 60
	-$(MAKE) setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_WRAP   2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_wrap.result )
	sleep 60
	-$(MAKE) setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_NVP 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_nvp.result )
	sleep 60
	-$(MAKE) setup_oltp run_oltp close_oltp OLTPEXPORT=EXPORT_COMPARE_NVP 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/oltp_compare_nvp.result )
	@grep "transactions:" $(OLTP_TESTS) | sed -e "s/\(transactions:\)/	SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "transactions:" $(OLTP_TESTS) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)
	@grep "Aborted" $(OLTP_TESTS) | sed -e "s/\(Aborted:\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "Segmentation" $(OLTP_TESTS) | sed -e "s/\(Segmentation\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)
	@ grep ERROR $(BDB_TESTS_NVP) | sed -e "s/\(ERROR:\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)
	@ grep Assertion $(BDB_TESTS_NVP) | sed -e "s/\(Assertion\)/ FAILURE: \1/g" | $(MARKERRORS) | $(HIGHLIGHTFAILURE)

setup_oltp:
	#@echo "Setting up mysql tests..."
	rm -rf /mnt/beecube/mysql /tmp/mysql
	mkdir /mnt/beecube/mysql /tmp/mysql
	#cp alex.cnf /mnt/beecube/my.cnf
	mysql_install_db --basedir=${BEE3HOME}/ext/mysql-5.1.46/install --datadir=/mnt/beecube/mysql --defaults-file=/tmp/my.cnf
	#$($(OLTPEXPORT)) mysqld_safe --defaults-file=/mnt/beecube/my.cnf &
	cd ${BEE3HOME}/ext/mysql-5.1.46/install; ./bin/mysqld_safe_moneta --defaults-file=/tmp/my.cnf --nvp-preloads='$($(OLTPEXPORT))' &
	sleep 180
	#sleep 80
	mysql -u root < ${BEE3HOME}/Automate/SC2010/scripts/util/oltp_init.sql
	$(BEE3HOME)/ext/sysbench-0.4.12/sysbench/sysbench --test=oltp --db-driver=mysql --mysql-table-engine=innodb --mysql-user=root --mysql-password= --mysql-socket=/tmp/mysql.sock --oltp-table-size=32000000 prepare

run_oltp:
	#@echo "Setup complete.  Running mysql test $(OUTFILE)"
	#$($(OLTPEXPORT)) $(BEE3HOME)/ext/sysbench-0.4.12/install/bin/sysbench --num-threads=1 --max-time=30 --max-requests=0 --test=oltp --oltp-table-size=100000 --db-driver=mysql --mysql-table-engine=innodb --mysql-user=root --mysql-password= --mysql-socket=/tmp/mysql.sock run
	if [ "`stat . | grep 'Device: fc00h' | wc -l`" == "0" ]; then echo "FAILURE: Not A Moneta Device...." | $(HIGHLIGHTFAILURE) ; exit 1; else echo "SUCCESS: moneta device mounted!" | $(HIGHLIGHTFAILURE); fi
	$(BEE3HOME)/ext/sysbench-0.4.12/install/bin/sysbench --num-threads=8 --max-time=60 --max-requests=0 --test=oltp --oltp-table-size=32000000 --db-driver=mysql --mysql-table-engine=innodb --mysql-user=root --mysql-password= --mysql-socket=/tmp/mysql.sock run

close_oltp:
	#@echo "Done running tests.  Cleaning up..."
	mysqladmin --defaults-file=/tmp/my.cnf -u root shutdown






################  POSTGRES  ####################
#                                              #
#     Compare POSTGRES on and off Moneta.      #
#                                              #
################################################

PG_TESTS = pgsql_unmod.result pgsql_wrap.result pgsql_moneta.result

# TODO: make sure this target still works
test_pg:
	rm -f /tmp/pgsql.log
	#${BEE3HOME}/Tools/PosixNVM/bin/test_pgsql_unmod 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/pgsql_unmod.result )
	#sleep 2
	#${BEE3HOME}/Tools/PosixNVM/bin/test_pgsql_wrap 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/pgsql_wrap.result )
	#sleep 2
	${BEE3HOME}/test/PosixNVM/bin/test_pgsql_moneta 2>&1 | ( unset LD_PRELOAD; tee $(LOG_OUT_DIR)/pgsql_moneta.result )
	@grep "transactions:" $(PG_TESTS) | sed -e "s/\(transactions:\)/      SUCCESS: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "transactions:" $(PG_TESTS) -L | $(MARKINCOMPLETE) | $(HIGHLIGHTFAILURE)
	@grep "Aborted" $(PG_TESTS) | sed -e "s/\(Aborted:\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "Segmentation" $(PG_TESTS) | sed -e "s/\(Segmentation\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)
	@grep "Check your installation." $(PG_TESTS) | sed -e "s/\(Check your installation.\)/FAILURE: \1/g" | $(HIGHLIGHTFAILURE)


