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

run_test()
{
    local in="$*"
    local a="${in// /-}"
    local b="${a////-}"
    b="${b//;/-}"
    b="${b//\"/-}"

    if [ -n "${RUN_TEST_FILTER}" ]; then
        if ! [[ "$*" =~ ${RUN_TEST_FILTER} ]]; then
            echo "Skipping test: $in"
            return
        fi
    fi
    export D_LOG_FILE="/tmp/daos_${b}-${log_num}.log"
    echo "Running $* with log file: ${D_LOG_FILE}"

    export TNAME="${b}-${log_num}"

    # We use grep to filter out any potential "SUCCESS! NO TEST FAILURES"
    #    messages as daos_post_build.sh will look for this and mark the tests
    #    as passed, which we don't want as we need to check all of the tests
    #    before deciding this. Also, we intentionally leave off the last 'S'
    #    in that error message so that we don't guarantee printing that in
    #    every run's output, thereby making all tests here always pass.
    if ! time eval "${VALGRIND_CMD}" "$@"; then
        retcode=${PIPESTATUS[0]}
        echo "Test $* failed with exit status ${retcode}."
        ((failed = failed + 1))
        failures+=("$*")
    fi

    ((log_num += 1))

    FILES=(${DAOS_BASE}/test_results/*.xml)

    "${SL_PREFIX}"/lib/daos/TESTING/ftest/scripts/post_process_xml.sh \
                                                                  "${COMP}" \
                                                                  "${FILES[@]}"

    if [ -f "${DAOS_BASE}"/test_results/run_go_tests.xml ]; then
        cat "${DAOS_BASE}"/test_results/run_go_tests.xml
    fi

    mv "${DAOS_BASE}"/test_results/*.xml "${DAOS_BASE}"/test_results/xml
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
    mkdir -p "${DAOS_BASE}"/test_results/xml

    VALGRIND_CMD=""
    if [ -z "$RUN_TEST_VALGRIND" ]; then
        # Tests that do not run valgrind
        COMP="UTEST_client"
        run_test src/client/storage_estimator/common/tests/storage_estimator.sh
        COMP="UTEST_rdb"
        run_test src/rdb/raft_tests/raft_tests.py
        go_spdk_ctests="${SL_PREFIX}/bin/nvme_control_ctests"
        if test -f "$go_spdk_ctests"; then
            COMP="UTEST_control"
            run_test "$go_spdk_ctests"
        else
            echo "$go_spdk_ctests missing, SPDK_SRC not available when built?"
        fi
        COMP="UTEST_control"
        run_test src/control/run_go_tests.sh
    fi
else
    echo "/mnt/daos isn't present for unit tests"
fi
