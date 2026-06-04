#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# This is a script to be run by the src/tests/ftest/dfuse/daos_build.py to run a test on a CI node.

set -euo pipefail

timestamp_output() {
    while IFS= read -r line; do
        # Use bash printf time formatting to avoid spawning `date` per line
        printf '%(%Y-%m-%d %H:%M:%S)T %s\n' -1 "$line"
    done
}

# Timestamp all script output (stdout + stderr)
exec > >(timestamp_output) 2>&1

show_help() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -p, --python-cmd <val>    Python command to use (default: python3)"
    echo "  -v, --venv-dir <val>      Path to Python virtual environment (default: /tmp/daos_build/venv)"
    echo "  -b, --build-dir <val>     Directory to clone and build DAOS (default: /tmp/daos_build/daos)"
    echo "  -m, --mount-dir <val>     Optional mount directory to use for filesystem tests (default: none)"
    echo "  -g, --git-checkout <val>  Git branch or commit to checkout (default: origin/master)"
    echo "  -d, --distro <val>        Linux distribution for installing dependencies (default: el9)"
    echo "  -j, --build-jobs <val>    Number of parallel jobs for building DAOS (default: 30)"
    echo "  -r, --rebuild             Whether to skip setup of build and venv directories (default: false)"
    echo "  -h, --help                Show this help message and exit"
}

# Argument defaults
python_cmd="python3"
venv_dir="/tmp/daos_build/venv"
build_dir="/tmp/daos_build/daos"
mount_dir=""
git_checkout="origin/master"
distro="el9"
build_jobs="30"
rebuild="false"
debug="false"
uv_index_url=""

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--python-cmd)
      python_cmd="$2"
      shift 2
      ;;
    -v|--venv-dir)
      venv_dir="$2"
      shift 2
      ;;
    -b|--build-dir)
      build_dir="$2"
      shift 2
      ;;
    -m|--mount-dir)
      mount_dir="$2"
      shift 2
      ;;
    -g|--git-checkout)
      git_checkout="$2"
      shift 2
      ;;
    -d|--distro)
      distro="$2"
      shift 2
      ;;
    -j|--build-jobs)
      build_jobs="$2"
      shift 2
      ;;
    -r|--rebuild)
      rebuild="true"
      shift 1
      ;;
    --uv-index-url)
      uv_index_url="$2"
      shift 2
      ;;
    --debug)
      debug="true"
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
    local start_time end_time elapsed_seconds rc
    local cmd="$*"

    echo ">---------------------------------------------------------"
    echo "Running command: ${cmd}"
    start_time=$(date +%s)
    ${cmd}
    rc=$?
    end_time=$(date +%s)
    elapsed_seconds=$((end_time - start_time))

    echo "Command finished in ${elapsed_seconds}s with rc=${rc}: ${cmd}"
    if [ "${rc}" -ne 0 ]; then
        echo "ERROR: command failed with rc=${rc}: ${cmd}" # >&2
        return "${rc}"
    fi
    return 0
}

full_start=$(date +%s)

# Create a Python virtual environment and install python build dependencies
if [ "${rebuild}" = "false" ]; then
    run_cmd "rm -rf ${venv_dir}" || exit
    run_cmd "${python_cmd} -m venv ${venv_dir}" || exit

    cat <<EOF > "${venv_dir}"/pip.conf
[global]
    progress_bar = off
    no_color = true
    quiet = 0
EOF
fi

# Run in the python virtual environment for the rest of the script
run_cmd "source ${venv_dir}/bin/activate" || exit
run_cmd "export VIRTUAL_ENV=${venv_dir}" || exit

# Clone the DAOS repository and install RPM dependencies for the build
if [ "${rebuild}" = "false" ]; then
  run_cmd "rm -rf ${build_dir}" || exit
  run_cmd "git clone https://github.com/daos-stack/daos.git ${build_dir}" || exit
  run_cmd "git -C ${build_dir} checkout ${git_checkout}" || exit
  run_cmd "git -C ${build_dir} submodule update --init --recursive" || exit

  run_cmd "cp ${build_dir}/utils/scripts/install-${distro}.sh /tmp/install.sh" || exit
  run_cmd "sudo -E NO_OPENMPI_DEVEL=1 /tmp/install.sh -y" || exit

  run_cmd "python -m pip install pip --upgrade" || exit
  run_cmd "python -m pip install -r ${build_dir}/requirements-build.txt" || exit
fi

if [[ -n ${uv_index_url} ]]; then
    # Set up the uv (for SPDK installer) to use the artifactory as the installation packages source
  run_cmd "sudo mkdir -p /etc/uv" || exit
  cat <<EOF | sudo tee /etc/uv/uv.toml
index-url = "${uv_index_url}"
native-tls = true
EOF
fi

# Debug
if [ "${debug}" = "true" ]; then
  # Check that all required Python packages are installed and available in PATH
  awk '!/^\s*($|#)/ {print $1}' "${build_dir}/requirements-build.txt" | while read -r pkg; do
    if [ "${pkg}" = "pyelftools" ]; then
      continue
    fi
    run_cmd "which ${pkg}" || exit
  done
fi

# Build DAOS dependencies
run_cmd "scons -C ${build_dir} --jobs ${build_jobs} --enable-virtualenv --build-deps=only" || exit

if [[ -n ${mount_dir-} ]]; then
  # Run filesystem tests to verify the build.
  run_cmd "daos filesystem query ${mount_dir}" || exit
  run_cmd "daos filesystem evict ${build_dir}" || exit
  run_cmd "daos filesystem query ${mount_dir}" || exit
fi

# Build and install DAOS
run_cmd "scons -C ${build_dir} --jobs ${build_jobs} --enable-virtualenv" || exit
run_cmd "scons -C ${build_dir} --jobs ${build_jobs} --enable-virtualenv install --implicit-deps-unchanged" || exit

if [[ -n ${mount_dir-} ]]; then
  run_cmd "daos filesystem query ${mount_dir}" || exit
fi

full_end=$(date +%s)
full_time=$((full_end - full_start))
echo "DAOS build and installation completed successfully in ${full_time}s"
exit 0
