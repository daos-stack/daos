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

if [ -z "$DAOS_BASE" ]; then
    DAOS_BASE="."
fi

produce_output()
{
    echo "$2"

    cname="run_test.${RUN_TEST_VALGRIND:-native}"
    name="run_test"
    fname="${DAOS_BASE}/test_results/test_run_test.sh.${RUN_TEST_VALGRIND:-native}.xml"

    if [ "$1" -eq 0 ]; then
       teststr="    <testcase classname=\"$cname\" name=\"$name\" />"
    else
       teststr="    <testcase classname=\"$cname\" name=\"$name\">
      <failure type=\"format\">
        <![CDATA[$2
          ]]>
      </failure>
    </testcase>"
    fi

    cat > "${fname}" << EOF
<?xml version="1.0" encoding="UTF-8" ?>
<testsuites>
  <testsuite tests="1" failures="$1" errors="0" skipped="0" >
EOF
echo "${teststr}" >> "${fname}"
cat >> "${fname}" << EOF
  </testsuite>
</testsuites>
EOF
}

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
    # shellcheck disable=SC2294
    if ! time eval "${VALGRIND_CMD}" "$@"; then
        retcode=${PIPESTATUS[0]}
        echo "Test $* failed with exit status ${retcode}."
        ((failed = failed + 1))
        failures+=("$*")
    fi

    ((log_num += 1))

    FILES=("${DAOS_BASE}"/test_results/*.xml)

    "${SL_PREFIX}"/lib/daos/TESTING/ftest/scripts/post_process_xml.sh \
                                                                  "${COMP}" \
                                                                  "${FILES[@]}"

    mv "${DAOS_BASE}"/test_results/*.xml "${DAOS_BASE}"/test_results/xml
}

if [ -d "/mnt/daos" ]; then
    # shellcheck disable=SC1091
    source ./.build_vars.sh

    echo "Running Cmocka tests"
    mkdir -p "${DAOS_BASE}"/test_results/xml

    VALGRIND_CMD=""
    if [ -z "$RUN_TEST_VALGRIND" ]; then
        # Tests that do not run valgrind
        COMP="UTEST_client"
        run_test src/vos/storage_estimator/common/tests/storage_estimator.sh
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
        COMP="UTEST_common"
        run_test src/common/tests/btree.sh perf -s 20000
        run_test src/common/tests/btree.sh perf direct -s 20000
        run_test src/common/tests/btree.sh perf ukey -s 20000
        run_test src/common/tests/btree.sh dyn perf -s 20000
        run_test src/common/tests/btree.sh dyn perf ukey -s 20000
        BTREE_SIZE=20000

        COMP="UTEST_vos"
        cat /proc/meminfo
        # Setup AIO device
        AIO_DEV=$(mktemp /tmp/aio_dev_XXXXX)
        dd if=/dev/zero of="${AIO_DEV}" bs=1G count=4

        # Setup daos_nvme.conf
        NVME_CONF="/mnt/daos/daos_nvme.conf"
        cp -f src/vos/tests/daos_nvme.conf ${NVME_CONF}
        sed -i "s+\"filename\": \".*\"+\"filename\": \"${AIO_DEV}\"+g" ${NVME_CONF}

        export VOS_BDEV_CLASS="AIO"
        export UCX_HANDLE_ERRORS=none
        run_test "sudo -E ${SL_PREFIX}/bin/vos_tests" -a

        rm -f "${AIO_DEV}"
        rm -f "${NVME_CONF}"

        run_test src/vos/tests/evt_stress.py
        run_test src/vos/tests/evt_stress.py --algo dist_even
        run_test src/vos/tests/evt_stress.py --algo soff

    else
        if [ "$RUN_TEST_VALGRIND" = "memcheck" ]; then
            [ -z "$VALGRIND_SUPP" ] &&
                VALGRIND_SUPP="$(pwd)/utils/test_memcheck.supp"
            VALGRIND_XML_PATH="unit-test-%q{TNAME}.memcheck.xml"
            export VALGRIND_CMD="valgrind --leak-check=full \
                                          --show-reachable=yes \
                                          --num-callers=20 \
                                          --error-limit=no \
                                          --fair-sched=try \
                                          --suppressions=${VALGRIND_SUPP} \
                                          --error-exitcode=42 \
                                          --xml=yes \
                                          --xml-file=${VALGRIND_XML_PATH}"
        else
            VALGRIND_SUPP=""
        fi
        BTREE_SIZE=200
    fi

    # Tests
    COMP="UTEST_cart"
    run_test "${SL_BUILD_DIR}/src/tests/ftest/cart/utest/test_linkage"
    run_test "${SL_BUILD_DIR}/src/tests/ftest/cart/utest/utest_hlc"
    run_test "${SL_BUILD_DIR}/src/tests/ftest/cart/utest/utest_swim"

    COMP="UTEST_gurt"
    run_test "${SL_BUILD_DIR}/src/gurt/tests/test_gurt"
    run_test "${SL_BUILD_DIR}/src/gurt/tests/test_gurt_telem_producer"

    COMP="UTEST_vos"
    run_test "${SL_PREFIX}/bin/vos_tests" -A 500
    run_test "${SL_PREFIX}/bin/vos_tests" -n -A 500

    COMP="UTEST_vea"
    run_test "${SL_PREFIX}/bin/vea_ut"
    run_test "${SL_PREFIX}/bin/vea_stress -d 60"

    COMP="UTEST_bio"
    run_test "${SL_BUILD_DIR}/src/bio/smd/tests/smd_ut"

    COMP="UTEST_common"
    run_test "${SL_BUILD_DIR}/src/common/tests/umem_test"
    run_test "${SL_BUILD_DIR}/src/common/tests/sched"
    run_test "${SL_BUILD_DIR}/src/common/tests/drpc_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_api_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_valid_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_util_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_principal_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/acl_real_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/prop_tests"
    run_test "${SL_BUILD_DIR}/src/common/tests/fault_domain_tests"

    COMP="UTEST_client"
    run_test "${SL_BUILD_DIR}/src/client/api/tests/eq_tests"
    run_test "${SL_BUILD_DIR}/src/client/api/tests/agent_tests"
    run_test "${SL_BUILD_DIR}/src/client/api/tests/job_tests"

    COMP="UTEST_security"
    run_test "${SL_BUILD_DIR}/src/security/tests/cli_security_tests"
    run_test "${SL_BUILD_DIR}/src/security/tests/srv_acl_tests"

    COMP="UTEST_engine"
    run_test "${SL_BUILD_DIR}/src/engine/tests/drpc_client_tests"
    run_test "${SL_BUILD_DIR}/src/engine/tests/drpc_progress_tests"
    run_test "${SL_BUILD_DIR}/src/engine/tests/drpc_handler_tests"
    run_test "${SL_BUILD_DIR}/src/engine/tests/drpc_listener_tests"

    COMP="UTEST_mgmt"
    run_test "${SL_BUILD_DIR}/src/mgmt/tests/srv_drpc_tests"
    run_test "${SL_PREFIX}/bin/vos_perf" -R '"U;p F;p V"' -o 5 -d 5 \
             -a 5 -n 10
    run_test "${SL_PREFIX}/bin/vos_perf" -R '"U;p F;p V"' -o 5 -d 5 \
             -a 5 -n 10 -A -D /mnt/../mnt/daos
    run_test "${SL_PREFIX}/bin/vos_perf" -R '"U Q;p V"' -o 5 -d 5 \
             -n 10 -A -i -I -D /mnt/daos
    run_test "${SL_PREFIX}/bin/jump_pl_map"

    # Tests launched by scripts
    export USE_VALGRIND=${RUN_TEST_VALGRIND}
    export VALGRIND_SUPP=${VALGRIND_SUPP}
    unset VALGRIND_CMD
    COMP="UTEST_common"
    run_test src/common/tests/btree.sh ukey -s ${BTREE_SIZE}
    run_test src/common/tests/btree.sh direct -s ${BTREE_SIZE}
    run_test src/common/tests/btree.sh -s ${BTREE_SIZE}
    run_test src/common/tests/btree.sh dyn ukey -s ${BTREE_SIZE}
    run_test src/common/tests/btree.sh dyn -s ${BTREE_SIZE}

    COMP="UTEST_vos"
    run_test src/vos/tests/evt_ctl.sh
    run_test src/vos/tests/evt_ctl.sh pmem
    unset USE_VALGRIND
    unset VALGRIND_SUPP

    mv "${DAOS_BASE}"/test_results/xml/*.xml "${DAOS_BASE}"/test_results
    rm -rf "${DAOS_BASE}"/test_results/xml

    if [ -f "/tmp/test.cov" ]; then
        rm /tmp/test.cov
    fi

    if [ -f "${DAOS_BASE}/test.cov" ]; then
        cp "${DAOS_BASE}"/test.cov /tmp/
    fi

    # Reporting
    if [ "$failed" -eq 0 ]; then
        # spit out the magic string that the post build script looks for
        produce_output 0 "SUCCESS! NO TEST FAILURES"
    else
        fail_msg="FAILURE: $failed tests failed (listed below)
"
        for ((i = 0; i < ${#failures[@]}; i++)); do
            fail_msg=$"$fail_msg    ${failures[$i]}
"
        done
        produce_output 1 "$fail_msg"
        if ! ${IS_CI:-false}; then
            exit 1
        fi
    fi
else
    echo "/mnt/daos isn't present for unit tests"
fi
