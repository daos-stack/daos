#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# This is a script to be run by the src/tests/ftest/dfuse/daos_build.py to run a test on a CI node.

set -euo pipefail

# Timestamp all script output (stdout + stderr)
timestamp_output() {
    while IFS= read -r line; do
        # Use bash printf time formatting to avoid spawning `date` per line
        printf '%(%Y-%m-%d %H:%M:%S)T %s\n' -1 "$line"
    done
}

exec > >(timestamp_output) 2>&1

show_help() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -p, --python_cmd <val>    Python command to use (default: python3)"
    echo "  -v, --python_venv <val>   Path to Python virtual environment (default: /tmp/daos_build/venv)"
    echo "  -b, --build_dir <val>     Directory to clone and build DAOS (default: /tmp/daos_build/daos)"
    echo "  -g, --git_checkout <val>  Git branch or commit to checkout (default: origin/master)"
    echo "  -d, --distro <val>        Linux distribution for installing dependencies (default: el9)"
    echo "  -j, --build_jobs <val>    Number of parallel jobs for building DAOS (default: 30)"
    echo "  -f, --filesystem_test     Whether to run filesystem tests (default: false)"
    echo "  -r, --rebuild             Whether to skip setup of build and venv directories (default: false)"
    echo "  -h, --help                Show this help message and exit"
}

# Argument defaults
python_cmd="python3"
python_venv="/tmp/daos_build/venv"
build_dir="/tmp/daos_build/daos"
git_checkout="origin/master"
distro="el9"
build_jobs="30"
filesystem_test="false"
rebuild="false"

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--python_cmd)
      python_cmd="$2"
      shift 2
      ;;
    -v|--python_venv)
      python_venv="$2"
      shift 2
      ;;
    -b|--build_dir)
      build_dir="$2"
      shift 2
      ;;
    -g|--git_checkout)
      git_checkout="$2"
      shift 2
      ;;
    -d|--distro)
      distro="$2"
      shift 2
      ;;
    -j|--build_jobs)
      build_jobs="$2"
      shift 2
      ;;
    -f|--filesystem_test)
      filesystem_test="true"
      shift 1
      ;;
    -r|--rebuild)
      rebuild="true"
      shift 1
      ;;
    --help|-h)
      show_help
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Unknown option: $1" >&2
      show_help
      exit 1
      ;;
  esac
done

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
if [ "${rebuild}" = "false" ]; then
    run_cmd 10s "rm -rf ${python_venv}" || exit $?
    run_cmd 10s "${python_cmd} -m venv ${python_venv}" || exit $?
fi
run_cmd 10s "source ${python_venv}/bin/activate" || exit $?

# Clone the DAOS repository and install RPM dependencies for the build
if [ "${rebuild}" = "false" ]; then
    run_cmd 10s "rm -rf ${build_dir}" || exit $?
    run_cmd 1m "git clone https://github.com/daos-stack/daos.git ${build_dir}" || exit $?
    run_cmd 15s "git -C ${build_dir} checkout ${git_checkout}" || exit $?
    run_cmd 15s "git -C ${build_dir} submodule update --init --recursive" || exit $?

    run_cmd 10s "cp ${build_dir}/utils/scripts/install-${distro}.sh /tmp/install.sh" || exit $?
    run_cmd 3m "sudo -E NO_OPENMPI_DEVEL=1 /tmp/install.sh -y" || exit $?
    run_cmd 1m "${python_cmd} -m pip install pip --upgrade" || exit $?
    run_cmd 5m "${python_cmd} -m pip install -r ${build_dir}/requirements-build.txt" || exit $?
fi

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
