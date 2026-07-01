#!/bin/bash
# /*
#  * (C) Copyright 2016-2023 Intel Corporation.
#  * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
#  * (C) Copyright 2025 Google LLC
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

echo "$PWD"
VARS_FILE=.build_vars.sh
if [ -f "./${VARS_FILE}" ]; then
  VARS_LOCAL="./${VARS_FILE}"
elif [ -f "../${VARS_FILE}" ]; then
  VARS_LOCAL="../${VARS_FILE}"
else
  VARS_LOCAL=""
fi

if [ -z "${VARS_LOCAL}" ]
then
    echo "Build vars file ${VARS_FILE} does not exist"
    echo "Cannot continue"
    return 1
fi

echo "Build vars file found: ${VARS_LOCAL}"
# shellcheck disable=SC1090
. "${VARS_LOCAL}"

os="$(uname)"
if [ "$os" = "Darwin" ]; then
    if [ -n "$DYLD_LIBRARY_PATH" ]; then
	export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}:${DYLD_LIBRARY_PATH}
    else
	export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}
    fi
fi

if [ -z "${SL_PREFIX}" ]
then
    SL_PREFIX="$(pwd)/install"
fi

# Export PYTHONPATH if a valid python installation is found
function export_pythonpath()
{
  # Default to PYTHON_VERSION to be compatible with packaging scripts
  local python_version="${PYTHON_VERSION:=3}"
  local python_cmd="python${python_version}"
  if [ ! -x "$(command -v $python_cmd)" ]; then
    echo "unknown Python version: ${python_version}"
    return 0
  fi

  local major="$($python_cmd -c 'import sys; print(sys.version_info.major)')"
  local minor="$($python_cmd -c 'import sys; print(sys.version_info.minor)')"
  python_version="${major}.${minor}"
  export PYTHONPATH=${SL_PREFIX}/lib64/python${python_version}/site-packages:${PYTHONPATH:-}
}
export_pythonpath

function in_list()
{
    this=$1
    shift
    for dir in "$@"; do
        if [ "$dir" == "$this" ]; then
            return 1
        fi
    done
    return 0
}

function create_list()
{
  compgen -A variable | grep "SL_.*_PREFIX" || true
}

list="$(create_list)"
# skip the default paths
added="/ /usr /usr/local"
old_path="${PATH//:/ }"
echo OLD_PATH is "${old_path}"
for item in $list; do
    # shellcheck disable=SC2086
    if ! in_list "${!item}" ${added}; then
        continue
    fi
    export "${item?}"
    added+=" ${!item}"
    # shellcheck disable=SC2086
    if ! in_list "${!item}/bin" ${old_path}; then
        continue
    fi
    if [ -d "${!item}/bin" ]; then
        PATH=${!item}/bin:$PATH
    fi
done

# shellcheck disable=SC2086
if in_list "${SL_PREFIX}/bin" ${old_path}; then
    PATH=$SL_PREFIX/bin:$PATH
fi
export PATH
export SL_PREFIX
export SL_SRC_DIR
