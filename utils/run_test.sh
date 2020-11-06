#!/bin/bash
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
failures=()
log_num=0

# this can be removed once we are no longer using the old CI system
if ${OLD_CI:-true}; then
lock_test()
{
    (
        # clean up all files except the lock
        flock 9
        find /mnt/daos -maxdepth 1 -mindepth 1 \! -name jenkins.lock -print0 | \
             xargs -0r rm -vrf
        eval "${VALGRIND_CMD}" "$@" 2>&1 | grep -v "SUCCESS! NO TEST FAILURE"
        exit "${PIPESTATUS[0]}"
    ) 9>/mnt/daos/jenkins.lock
}

lock_test="lock_test"
fi

run_test()
{
    local in="$*"
    local a="${in// /-}"
    local b="${a////-}"

    if [ -n "${RUN_TEST_FILTER}" ]; then
        if ! [[ "$*" =~ ${RUN_TEST_FILTER} ]]; then
            echo "Skipping test: $in"
            return
        fi
    fi
    export D_LOG_FILE="/tmp/daos_${b}-${log_num}.log"
    echo "Running $* with log file: ${D_LOG_FILE}"

    # We use flock as a way of locking /mnt/daos so multiple runs can't hit it
    #     at the same time.
    # We use grep to filter out any potential "SUCCESS! NO TEST FAILURES"
    #    messages as daos_post_build.sh will look for this and mark the tests
    #    as passed, which we don't want as we need to check all of the tests
    #    before deciding this. Also, we intentionally leave off the last 'S'
    #    in that error message so that we don't guarantee printing that in
    #    every run's output, thereby making all tests here always pass.
    if ! time $lock_test "$@"; then
        echo "Test $* failed with exit status ${PIPESTATUS[0]}."
        ((failed = failed + 1))
        failures+=("$*")
    fi

    ((log_num += 1))
}

if [ -d "/mnt/daos" ]; then
    # shellcheck disable=SC1091
    source ./.build_vars.sh
    if ! ${OLD_CI:-true}; then
        # fix up paths so they are relative to $PWD since we might not
        # be in the same path as the software was built
        SL_PREFIX=$PWD/${SL_PREFIX/*\/install/install}
    fi

    echo "Running Cmocka tests"
    VALGRIND_CMD=""
    if [ -z "$RUN_TEST_VALGRIND" ]; then
        # Tests that do not run valgrind
        run_test src/client/storage_estimator/common/tests/storage_estimator.sh
        run_test src/rdb/raft_tests/raft_tests.py
        go_spdk_ctests="${SL_PREFIX}/bin/nvme_control_ctests"
        if test -f "$go_spdk_ctests"; then
            run_test "$go_spdk_ctests"
        else
            echo "$go_spdk_ctests missing, SPDK_SRC not available when built?"
        fi
        run_test src/control/run_go_tests.sh
    else
        if [ "$RUN_TEST_VALGRIND" = "memcheck" ]; then
            [ -z "$VALGRIND_SUPP" ] &&
                VALGRIND_SUPP="$(pwd)/utils/test_memcheck.supp"
            VALGRIND_XML_PATH="test_results/unit-test-%p.memcheck.xml"
            VALGRIND_CMD="valgrind --leak-check=full --show-reachable=yes \
                                   --error-limit=no \
                                   --suppressions=${VALGRIND_SUPP} \
                                   --xml=yes --xml-file=${VALGRIND_XML_PATH}"
        else
            VALGRIND_SUPP=""
        fi
    fi

    # Tests
    run_test "${SL_BUILD_DIR}/src/tests/ftest/cart/utest/test_linkage"
    run_test "${SL_BUILD_DIR}/src/gurt/tests/test_gurt"
    run_test "${SL_BUILD_DIR}/src/tests/ftest/cart/utest/utest_hlc"
    run_test "${SL_BUILD_DIR}/src/tests/ftest/cart/utest/utest_swim"
    run_test "${SL_PREFIX}/bin/vos_tests" -A 500
    run_test "${SL_PREFIX}/bin/vos_tests" -n -A 500
    export DAOS_IO_BYPASS=pm
    run_test "${SL_PREFIX}/bin/vos_tests" -A 50
    export DAOS_IO_BYPASS=pm_snap
    run_test "${SL_PREFIX}/bin/vos_tests" -A 50
    unset DAOS_IO_BYPASS
    run_test "${SL_BUILD_DIR}/src/common/tests/umem_test"
    run_test "${SL_BUILD_DIR}/src/common/tests/sched"
    run_test "${SL_BUILD_DIR}/src/common/tests/drpc_tests"
    run_test "${SL_BUILD_DIR}/src/client/api/tests/eq_tests"
    run_test "${SL_BUILD_DIR}/src/bio/smd/tests/smd_ut"
    run_test "${SL_PREFIX}/bin/vea_ut"
    run_test "${SL_BUILD_DIR}/src/security/tests/cli_security_tests"
    run_test "${SL_BUILD_DIR}/src/security/tests/srv_acl_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_api_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_valid_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_util_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_principal_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_real_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/prop_tests"
    run_test "${SL_BUILD_DIR}/src/iosrv/tests/drpc_progress_tests"
    run_test "${SL_BUILD_DIR}/src/iosrv/tests/drpc_handler_tests"
    run_test "${SL_BUILD_DIR}/src/iosrv/tests/drpc_listener_tests"
    run_test "${SL_BUILD_DIR}/src/mgmt/tests/srv_drpc_tests"

    # Scripts launching tests
    export USE_VALGRIND=${RUN_TEST_VALGRIND}
    export VALGRIND_SUPP=${VALGRIND_SUPP}
    unset VALGRIND_CMD
    run_test src/common/tests/btree.sh ukey -s 20000
    run_test src/common/tests/btree.sh direct -s 20000
    run_test src/common/tests/btree.sh -s 20000
    run_test src/common/tests/btree.sh perf -s 20000
    run_test src/common/tests/btree.sh perf direct -s 20000
    run_test src/common/tests/btree.sh perf ukey -s 20000
    run_test src/common/tests/btree.sh dyn ukey -s 20000
    run_test src/common/tests/btree.sh dyn -s 20000
    run_test src/common/tests/btree.sh dyn perf -s 20000
    run_test src/common/tests/btree.sh dyn perf ukey -s 20000
    run_test src/vos/tests/evt_ctl.sh
    run_test src/vos/tests/evt_ctl.sh pmem
    unset USE_VALGRIND
    unset VALGRIND_SUPP

    # Reporting
    if [ $failed -eq 0 ]; then
        # spit out the magic string that the post build script looks for
        echo "SUCCESS! NO TEST FAILURES"
    else
        echo "FAILURE: $failed tests failed (listed below)"
        for ((i = 0; i < ${#failures[@]}; i++)); do
            echo "    ${failures[$i]}"
        done
        if ! ${OLD_CI:-true}; then
            exit 1
        fi
    fi
else
    echo "/mnt/daos isn't present for unit tests"
fi
