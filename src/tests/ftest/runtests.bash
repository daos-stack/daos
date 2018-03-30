#!/bin/bash

# This script is used to run daos tests.  You must supply at least 1 argument to determine
# what tests get run.  You don't have to use this script of course, just keeps me from
# having to remember all the filenames.

# commented out because altogether they run for a really long time, they work fine when
# uncommented
function run_pool_tests {
    avocado run --show-job-log --html-job-result on --mux-yaml \
	./pool/RebuildTests.yaml -- ./pool/RebuildTests.py
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/SimpleCreateDeleteTest.yaml -- ./pool/SimpleCreateDeleteTest.py
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/MultipleCreatesTest.yaml -- ./pool/MultipleCreatesTest.py
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/ConnectTest.yaml -- ./pool/ConnectTest.py
#     avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/MultiServerCreateDeleteTest.yaml -- \
#	./pool/MultiServerCreateDeleteTest.py
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/DestroyTests.yaml -- \
#	./pool/DestroyTests.py
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/InfoTests.yaml -- \
#	./pool/InfoTests.py
#    avocado run --show-job-log --html-job-result on --mux-yaml \
#	./pool/EvictTest.yaml -- \
#	./pool/EvictTest.py
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
