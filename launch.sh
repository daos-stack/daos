#!/bin/bash
# Account name is required on Aurora
#PBS -A Aurora_deployment
#PBS -j oe
#PBS -V

# Exit on error
set -e

{
    if [ -z "$LAUNCH_TEST_TAGS" ]; then
        log "Missing env LAUNCH_TAGS!"
        exit 1
    fi
} &>> $JOB_LOG

{
    source ${JOB_DIR}/setup.sh "$DAOS_TEST_MODE"
} &>> $JOB_LOG


{
    log "Running launch.py"
    if [[ "$DAOS_TEST_MODE" == *source* ]] || [[ "$DAOS_TEST_MODE" == *lustre* ]]; then
        ftest_path="$DAOS_BUILD/install/lib/daos/TESTING/ftest/"
    else
        ftest_path="/usr/lib/daos/TESTING/ftest/"
    fi

    pushd "$ftest_path"

    # Makito added for io sys admin. Make sure to use my branch; makito/DAOS-15960
    echo "## Copy io sys admin files start."
    DAOS_REPO_FTEST=/scratchbox/daos/makito/daos/src/tests/ftest
    cmd1="cp -r ${DAOS_REPO_FTEST}/deployment/io_sys_admin.yaml ${ftest_path}/deployment/"
    cmd2="cp -r ${DAOS_REPO_FTEST}/util/file_count_test_base.py ${ftest_path}/util/"
    cmd3="cp -r ${DAOS_REPO_FTEST}/util//mdtest_test_base.py ${ftest_path}/util/"
    log "$cmd1"
    log "$cmd2"
    log "$cmd3"
    eval "$cmd1"
    eval "$cmd2"
    eval "$cmd3"
    echo "## Copy io sys admin files finish."

    DAOS_TEST_LAUNCH_ARGS="${DAOS_TEST_LAUNCH_ARGS:-}"

    if [[ ! "$DAOS_TEST_LAUNCH_ARGS" =~ "-tm" ]]; then
        if [[ "$DAOS_TEST_MODE" != *soak* ]] && [[ "$LAUNCH_TEST_TAGS" != *soak* ]]; then
            _tm=""
            DAOS_TEST_LAUNCH_ARGS+=" -tm 5"
        fi
    fi

    if [[ ! "$DAOS_TEST_LAUNCH_ARGS" =~ "--provider" ]]; then
        DAOS_TEST_LAUNCH_ARGS+=" --provider ofi+cxi"
    fi

    if [[ ! "$DAOS_TEST_LAUNCH_ARGS" =~ "-n " ]]; then
        DAOS_TEST_LAUNCH_ARGS+=" -n auto_vmd"
    fi

    if [[ "$DAOS_TEST_MODE" == *lustre* ]] && [[ ! "$DAOS_TEST_LAUNCH_ARGS" =~ "--mode " ]]; then
        DAOS_TEST_LAUNCH_ARGS+=" --mode custom_a"
    fi

    cmd="python3 ./launch.py -aro $DAOS_TEST_LAUNCH_ARGS -ts \"$DAOS_SERVERS\" -tc \"$DAOS_CLIENTS\" $LAUNCH_EXTRA_YAML $LAUNCH_TEST_TAGS"
    log "$cmd"
    eval $cmd || :  # ignore return code from launch.py since archiving probably fails
} &>> $JOB_LOG


