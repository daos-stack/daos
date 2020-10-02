#!/bin/bash
# Copyright (C) Copyright 2020 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#######################################
# Print usage to user.
#######################################
usage() {
    echo -n "get_remote_files.sh [OPTION]...

This script has 4 modes that can be executed independently if needed.

 1. Display big files specified by threshold value i.e. -t 5mb. This option
    will list files within provided local path with -d option and will prepend
    each file with a \"Y\" if file exceeds threshold value and with \"N\" if
    it does not.

 2. Compress files specified in provided local path with -d option that exceed
    1M in size. This script will check if current local system is a VM host, if
    so, compression will not be executed.

 3. Archive files specified in provided local path with -d option to provided
    remote path destination.

 4. Create and display cart_logtest log files created by running cart_logtest.py
    script. Output of running the script will be redirected to *_ctestlog.log
    files.

 Options:
      -c        Compress files
      -t        Threshold value to determine classification of big logs
      -r        Remote path to scp files to i.e. server-A:/path/to/file
      -d        Local path to files for archiving/compressing/size checking
      -s        Enable running cart_logtest.py
      -v        Display commands being executed
      -h        Display this help and exit
"
}

#######################################
# CONSTANT variables.
#######################################
# shellcheck disable=SC2046,SC2086
CART_LOGTEST_PATH="$(dirname $(readlink -f ${0}))/cart/util/cart_logtest.py"

#######################################
# Print set GLOBAL variables.
#######################################
display_set_vars() {
    echo "Set Variables:
    COMPRESS = ${COMPRESS}
    THRESHOLD = ${THRESHOLD}
    REMOTE_DEST = ${REMOTE_DEST}
    LOCAL_SRC = ${LOCAL_SRC}
    CART_LOGTEST = ${CART_LOGTEST}
    VERBOSE = ${VERBOSE}"
}

#######################################
# Convert human readable size values to respective byte value.
# Arguments:
#   size: size value to convert to bytes. i.e. 5mb, 40gb, 6k, 10g
# Returns:
#   converted byte value.
#######################################
human_to_bytes() {
    h_size=${1,,}

    declare -A units=(
        [b]=1
        [kb]=$(python -c "print (2**10)")
        [k]=$(python -c "print (2**10)")
        [mb]=$(python -c "print (2**20)")
        [m]=$(python -c "print (2**20)")
        [gb]=$(python -c "print (2**30)")
        [g]=$(python -c "print (2**30)")
    )
    if [[ "${h_size,,}" =~ ([0-9.]+)([kbmgt]+) ]]; then
        val="${BASH_REMATCH[1]}"
        unit="${BASH_REMATCH[2]}"
        (( val = val*${units[${unit}]} ))
        echo "${val}"
    else
        echo "Invalid value provided: ${1}"
        return 1
    fi
}

#######################################
# List and tag files with a 'Y' or 'N' to classify
# big files based on the threshold provided.
# Arguments:
#   logs_dir: path to local logs
#   threshold: value to use as threshold to classify big files
# Returns:
#   stdout output listing the files prepended with a 'Y' or 'N' tag
#######################################
list_tag_files() {
    # shellcheck disable=SC2016,SC1004,SC2086
    awk_cmd='{ \
    if (th <= $1){ \
        print "Y:",$1,$2} \
    else { \
        print "N:",$1,$2}\
    }'

    # Convert to bytes and run
    bts=$(human_to_bytes "${2}")
    # shellcheck disable=SC2086
    du -ab -d 1 ${1} | awk -v th="${bts}" "${awk_cmd}"
}

check_hw() {
    dmesg | grep "Hypervisor detected"
    return $?
}

compress_files() {
    # shellcheck disable=SC2086
    find ${1} -maxdepth 0 -type f -size +1M -print0 | xargs -r0 lbzip2 -v
}

scp_files() {
    set -ux
    rc=0
    copied=()
    # shellcheck disable=SC2045,SC2086
    for file in $(ls -d ${1})
    do
        ls -sh "${file}"
        if scp -r "${file}" "${2}"/"$(hostname -s)"-"${file##*/}"; then
            copied+=("${file}")
            if ! sudo rm -fr "${file}"; then
                ((rc++))
                ls -al "${file}"
            fi
        fi
    done
    echo Copied "${copied[@]:-no files}"
}

get_cartlogtest_files() {
    set -ux
    # shellcheck disable=SC2045,SC2086
    for file in $(ls -d ${1})
    do
        ls -sh "${file}"
        "${CART_LOGTEST_PATH}" "${file}" |& tee "${file}"_ctestlog
    done
}

####### MAIN #######
if [[ $# -eq 0 ]] ; then
    usage
    exit 1
fi

# Setup defaults
COMPRESS="false"
VERBOSE="false"
CART_LOGTEST="false"
THRESHOLD=""

# Step through arguments
while getopts "vhcs:r:d:t:" opt; do
    case ${opt} in
        r )
            REMOTE_DEST=$OPTARG
            ;;
        d )
            LOCAL_SRC=$OPTARG
            ;;
        t )
            THRESHOLD=$OPTARG
            ;;
        s )
            CART_LOGTEST="true"
            ;;
        c )
            COMPRESS="true"
            ;;
        v )
            VERBOSE="true"
            ;;
        h )
            usage
            exit
            ;;
        ? )
            echo "Unexpected argument: ${opt}!"
            usage
            exit
            ;;
    esac
done
shift "$((OPTIND -1))"

if [ "${VERBOSE}" == "true" ]; then
    display_set_vars
fi

# Display big files to stdout
if [ -n "${THRESHOLD}" ]; then
    echo "Checking if files exceed ${THRESHOLD} threshold ..."
    list_tag_files "${LOCAL_SRC}" "${THRESHOLD}"
fi

# Run cart_logtest.py on LOCAL_SRC
if [ "${CART_LOGTEST}" == "true" ]; then
    if [ -f "${CART_LOGTEST_PATH}" ]; then
        echo "Running cart_logtest.py on ${LOCAL_SRC}"
        get_cartlogtest_files "${LOCAL_SRC}"
    else
        echo "${CART_LOGTEST_PATH} does not exist!"
    fi
fi

# Compress files in LOCAL_SRC if not running on VM host.
set +x
if check_hw; then
    echo "Running on VM system, skipping compression ..."
    COMPRESS="false"
fi

if [ "${COMPRESS}" == "true" ]; then
    echo "Compressing files ..."
    compressed_out=$(compress_files "${LOCAL_SRC}" 2>&1)

    if [ -n "${compressed_out}" ] && [ "${VERBOSE}" == "true" ]; then
        echo "${compressed_out}"
    fi
fi

# Scp files specified in LOCAL_SRC to REMOTE_DEST
if [ -n "${REMOTE_DEST}" ]; then
    echo "Archiving logs in ${LOCAL_SRC} to ${REMOTE_DEST}"
    scp_files "${LOCAL_SRC}" "${REMOTE_DEST}"
fi
