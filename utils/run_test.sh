#!/bin/sh
# A Script that runs a series of simple tests that
#  can be executed in the Jenkins environment.
#  The flock is to ensure that only one jenkins builder
#  runs the tests at a time, as the tests use the same
#  mount point.  For now, there actually isn't much
#  contention, however, because /mnt/daos on the jenkins nodes
#  is subdivided into /mnt/daos/el7; /mnt/daos/sles12sp3;
#  /mnt/daos/ubuntu1404; and /mnt/daos/ubuntu1604
#  These are mapped into /mnt/daos in the docker containers
#  for the corresponding Jenkins distro builder. This "magic"
#  is configured in the Jenkins config page for daos builders
#  in the "build" section.
#
#  Note: Uses .build_vars.sh to find daos artifacts
#  Note: New tests should return non-zero if there are any
#    failures.

#check for existence of /mnt/daos first:
failed=0
function run_test()
{
    time flock /mnt/daos/jenkins.lock $* > tmp_test_file 2>&1
    if [ $? -ne 0 ]; then
        echo "Test $* failed"
        failed=$[ $failed + 1 ]
    fi
    #Hack so I don't need to update scons_local script
    cat tmp_test_file | grep -v SUCCESS
}

if [ -d "/mnt/daos" ]; then
    source ./.build_vars.sh
    run_test ${SL_PREFIX}/bin/vos_tests -A 500
    run_test src/common/tests/btree.sh ukey -s 20000
    run_test src/common/tests/btree.sh direct -s 20000
    run_test src/common/tests/btree.sh -s 20000
    run_test src/common/tests/btree.sh perf -s 20000
    run_test src/common/tests/btree.sh perf direct -s 20000
    run_test src/common/tests/btree.sh perf ukey -s 20000
    run_test build/src/common/tests/sched
    run_test build/src/client/api/tests/eq_tests
    run_test src/vos/tests/evt_ctl.sh
    run_test build/src/vos/vea/tests/vea_ut
    run_test src/rdb/raft_tests/raft_tests.py

    if [ $failed -eq 0 ]; then
        # spit out the magic string that the post build script looks for
        echo "SUCCESS! NO TEST FAILURES"
    else
        echo "FAILURE: $failed tests failed"
    fi
else
    echo "/mnt/daos isn't present for unit tests"
fi
