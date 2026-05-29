#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# This is a script to be run by the src/tests/ftest/dfuse/daos_build.py to run a test on a CI node.

# Inputs
python_cmd="${1:-python3}"
python_venv="${2:-/tmp/daos_build/venv}"
build_dir="${3:-/tmp/daos_build/daos}"
git_checkout="${4:-origin/master}"
distro="${5:-el9}"
build_jobs="${6:-30}"
filesystem_test="${7:-false}"


run_cmd() {
    local timeout_duration="$1"
    shift

    local start_time end_time elapsed_seconds rc
    local cmd="$*"

    echo "----------------------------------------------------"
    echo "Running command with a ${timeout_duration} timeout: ${cmd}"
    start_time=$(date +%s)
    timeout -k 10s "${timeout_duration}" bash -lc "${cmd}"
    rc=$?
    end_time=$(date +%s)
    elapsed_seconds=$((end_time - start_time))

    echo "Command finished in ${elapsed_seconds}s of ${timeout_duration} with rc=${rc}: ${cmd}"
    if [ "${rc}" -eq 124 ]; then
        echo "ERROR: command timed out after ${timeout_duration}: ${cmd}" # >&2
        return "${rc}"
    elif [ "${rc}" -ne 0 ]; then
        echo "ERROR: command failed with rc=${rc}: ${cmd}" # >&2
        return "${rc}"
    fi

    return 0
}

# Create a Python virtual environment and install python build dependencies
run_cmd 10s "${python_cmd} -m venv ${python_venv}" || exit $?
run_cmd 10s "source ${python_venv}/bin/activate" || exit $?

# Clone the DAOS repository and install RPM dependencies for the build
run_cmd 1m "git clone https://github.com/daos-stack/daos.git ${build_dir}" || exit $?
run_cmd 15s "git -C ${build_dir} checkout ${git_checkout}" || exit $?
run_cmd 15s "git -C ${build_dir} submodule update --init --recursive" || exit $?
run_cmd 10s "cp ${build_dir}/utils/scripts/install-${distro}.sh /tmp/install.sh" || exit $?
run_cmd 3m "sudo -E NO_OPENMPI_DEVEL=1 /tmp/install.sh -y" || exit $?
run_cmd 1m "${python_cmd} -m pip install pip --upgrade" || exit $?
run_cmd 5m "${python_cmd} -m pip install -r ${build_dir}/requirements-build.txt" || exit $?

# Build DAOS dependencies
run_cmd 3h "scons -C ${build_dir} --jobs ${build_jobs} --build-deps=only" || exit $?

if [ "${filesystem_test}" = "true" ]; then
    # Run filesystem tests to verify the build.
    run_cmd 3m "daos filesystem query ${mount_dir}" || exit $?
    run_cmd 3m "daos filesystem evict ${build_dir}" || exit $?
    run_cmd 3m "daos filesystem query ${mount_dir}" || exit $?
fi

# Build and install DAOS
run_cmd 3h "scons -C ${build_dir} --jobs ${build_jobs}" || exit $?
run_cmd 3h "scons -C ${build_dir} --jobs ${build_jobs} install --implicit-deps-unchanged" || exit $?

if [ "${filesystem_test}" = "true" ]; then
    run_cmd 3m "daos filesystem query ${mount_dir}" || exit $?
fi

exit 0
