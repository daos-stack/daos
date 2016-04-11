##!/bin/bash
#
##All rights reserved. This program and the accompanying materials
#are made available under the terms of the GNU Lesser General Public License
#(LGPL) version 2.1 which accompanies this distribution, and is available at
#http://www.gnu.org/licenses/lgpl-2.1.html
#
#This library is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#Lesser General Public License for more details.
#GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
#The Government's rights to use, modify, reproduce, release, perform, display,
#or disclose this software are subject to the terms of the LGPL License as
#provided in Contract No. B609815.
#any reproduction of computer software, computer software documentation, or
#portions thereof marked with this legend must also reproduce the markings.
#
#-------------------------------------------------------------------------
#(C) Copyright 2016 Intel Corporation.
#Test script for launching vos tests
#vos/tests/vos_test.sh
#Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
#-------------------------------------------------------------------------

#Source Directory
DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/../../..; echo $PWD)}

#Test names
CHASH_TABLE=$DAOS_DIR/build/vos/tests/vos_chash_table_test
POOL_TEST=$DAOS_DIR/build/vos/tests/vos_pool_tests
CONTAINER_TEST=$DAOS_DIR/build/vos/tests/vos_container_tests
POOL_FILE=$1
TEST_CNT=0;
TEST_SUCCESS=0;
TEST_FAILED=0;

function tests_header {
	echo "=========================================="
	echo "VOS Tests"
	echo "=========================================="
}

# argument 1 typically the test name
# argument 2 is optional and can be  any additional
# 	     description
function test_start_header {
	echo "============================================"
	echo "Starting Test $1 $2"
	echo "============================================"
}

# argument 1 typically the test name
# argument 2 is optional and can be  any additional
# 	     description
function test_end_header {
	echo "============================================"
	echo "Ending Test $1 $2"
	echo "============================================"
}

# argument 1 return value from executing test
# argument 2 Test string to identify tests
function compare_and_print {
	if [ $1 == "0" ]; then
		echo "Test Successful"
		TEST_SUCCESS=$((TEST_SUCCESS + 1));
	else
		echo "Test $2 Failed with error $1"
		TEST_FAILED=$((TEST_FAILED + 1));
	fi
}

# Report results
# No arguments
function vtests_summary {
	echo "=============================="
	echo "VOS tests summary "
	echo "=============================="
	echo "$TEST_SUCCESS tests Succeeded"
	echo "$TEST_FAILED tests failed"
}
# argument 1 test_executable
# argument 2 optional test_header info
# argument 3 pool_file
# argument 4 additional arguments (optional)
function vtest {
	TEST_CNT=$((TEST_CNT + 1));
	if [ $# == 0 ]; then
		echo "No arguments for launching test"
	else
		if [ "$1" != 0 ] &&[ "$1" != " " ]; then
			if [ "$2" != 0 ] && [ "$2" != " " ]; then
				test_start_header $1 $2
			else
				test_start_header $1
			fi
			if [ ! -z $3 -a "$3" != " " ]; then
			    if [ "$4" != 0 ] && [ "$4" != " " ]; then
				       $1 $3 $4
				else
				       $1 $3
				fi
			compare_and_print $? $1
			rm -f $2
			else
				echo "Not Enough arguments"
			fi
		fi
	fi
	if [ "$2" != 0 ] && [ "$2" != " "i ]; then
		test_end_header $1 $2
	else
		test_end_header $1
	fi

}

tests_header
if [ $# == 0 ]; then
	echo "Argument missing"
	echo "pool file with path required"
	exit -1
fi

# clean up before previous tests
# force remove pool file
rm -f $POOL_FILE
# invoke tests
vtest $POOL_TEST "" $POOL_FILE ""
vtest $CONTAINER_TEST "" $POOL_FILE ""
vtest $CHASH_TABLE "SINGLE-THREADED" $POOL_FILE "10 100 0"
vtest $CHASH_TABLE "MULTI-THREADED" $POOL_FILE "10 100 1 4"
vtests_summary
