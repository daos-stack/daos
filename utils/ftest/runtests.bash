#!/bin/bash

# This script is used to run daos tests.  You must supply at least 1 argument to determine
# what tests get run.

export DAOSPATH=/home/skirvan/daos_m10
export LD_LIBRARY_PATH=$DAOSPATH/install/lib:$DAOSPATH/install/lib/daos_srv
export CCI_CONFIG=$DAOSPATH/install/etc/cci.ini
export CRT_PHY_ADDR_STR="cci+tcp"
#export CRT_PHY_ADDR_STR="ofi+sockets"
export DD_LOG=/mnt/shared/test/tmp/daos.log
export ABT_ENV_MAX_NUM_XSTREAMS=100
export ABT_MAX_NUM_XSTREAMS=100
export PATH=$DAOSPATH/install/bin:$DAOSPATH/install/sbin:$PATH
export OFI_PORT=22451
export OFI_INTERFACE=ib0


function run_pool_tests {
    avocado run --show-job-log --html-job-result on --mux-yaml \
	./pool/SimpleCreateDeleteTest.yaml -- ./pool/SimpleCreateDeleteTest.py
    avocado run --show-job-log --html-job-result on --mux-yaml \
	./pool/MultipleCreatesTest.yaml -- ./pool/MultipleCreatesTest.py
# the connection tests just blow up at present
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/ConnectTest.yaml -- ./pool/ConnectTest.py
    avocado run --show-job-log --html-job-result on --mux-yaml \
	./pool/save -- \
	./pool/MultiServerCreateDeleteTest.py
}

function run_server_tests {
    avocado run --show-job-log --mux-yaml ./server/ServerLaunch.yaml -- ./server/ServerLaunch.py
}

function run_repo_tests {
    avocado run --show-job-log --mux-yaml ./repo/RepoTest.yaml -- ./repo/RepoTest.py
}

while getopts apsr option
do
    case $option in
	a | p ) run_pool_tests;;
	a | s ) run_server_tests;;
	a | r ) run_repo_tests;;
     esac
done
