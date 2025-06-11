#!/bin/bash
# /*
#  * (C) Copyright 2016-2023 Intel Corporation.
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

function export_pythonpath()
{
  MAJOR="${1}"
  MINOR="$(python3 -c 'import sys; print(sys.version_info.minor)')"
  VERSION="${MAJOR}.${MINOR}"
  if [ "${MAJOR}" -eq 3 ]; then
    PYTHONPATH=${SL_PREFIX}/lib64/python${VERSION}/site-packages:${PYTHONPATH:-}
  else
    echo "unknown Python version: ${VERSION}"
    return 0
  fi

  export PYTHONPATH
}

# look for a valid installation of python
if [ -x "$(command -v python3)" ]; then
  PYTHON_VERSION="$(python3 -c 'import sys; print(sys.version_info.major)')"
  export_pythonpath "${PYTHON_VERSION}"
else
  echo "python3 not found"
fi

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
  compgen -A variable | grep "SL_.*_PREFIX"
}

list="$(create_list)"
# skip the default paths
added="/ /usr /usr/local"
old_path="${PATH//:/ }"
echo OLD_PATH is "${old_path}"
for item in $list; do
    in_list "${!item}" "${added}"
    if [ $? -eq 1 ]; then
        continue
    fi
    export "${item?}"
    added+=" ${!item}"
    in_list "${!item}/bin" "${old_path}"
    if [ $? -eq 1 ]; then
        continue
    fi
    if [ -d "${!item}/bin" ]; then
        PATH=${!item}/bin:$PATH
    fi
done

in_list "${SL_PREFIX}/bin" "${old_path}"
# shellcheck disable=SC2181
if [ $? -eq 0 ]; then
    PATH=$SL_PREFIX/bin:$PATH
fi
export PATH
export SL_PREFIX
export SL_SRC_DIR
