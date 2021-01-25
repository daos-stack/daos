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

 1. Display big files specified by threshold value i.e. -t 5M. This option
    will list files within provided local path with -f option and will prepend
    each file with a \"Y\" if file exceeds threshold value and with \"N\" if
    it does not.

 2. Compress files specified in provided local path with -f option that exceed
    1M in size. This script will check if current local system is a VM host, if
    so, compression will not be executed.

 3. Archive files specified in provided local path with -f option to provided
    remote path destination.

 4. Create and display cart_logtest log files created by running cart_logtest.py
    script. Output of running the script will be redirected to *_ctestlog.log
    files.

 Options:
      -z        Compress files
      -t        Threshold value to determine classification of big logs
      -a        Remote path to scp files to i.e. server-A:/path/to/file
      -f        Local path to files for archiving/compressing/size checking
      -c        Enable running cart_logtest.py
      -x        Enable performing compression when running on VM system
      -v        Display commands being executed
      -h        Display this help and exit
"
}

#######################################
# Check if the files exist on this host.
# Arguments:
#   $1: local logs to process
# Returns:
#   0: Files exist
#   1: Missing the log files argument
#   2: No matches were found
#######################################
check_files_input() {
    local rc=0
    if [ -n "${1}" ]; then
        # shellcheck disable=SC2086
        if ! ls -d ${1} 1>&2; then
            rc=2
        else
            echo "Files found that match ${1}."
        fi
    else
        rc=1
    fi
    echo ${rc}
    return ${rc}
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
        print "  Y:",$1,$2} \
    else { \
        print "  N:",$1,$2}\
    }'

    # Convert to bytes and run
    bts=$(echo "${2}" | numfmt --from=iec)
    # shellcheck disable=SC2086
    du -ab -d 1 ${1} | awk -v th="${bts}" "${awk_cmd}"
}

#######################################
# Compress any files exceeding 1M.
# Arguments:
#   $1: local logs to compress
# Returns:
#   status of the lbzip2 command
#######################################
compress_files() {
    # shellcheck disable=SC2086
    find ${1} -maxdepth 0 -type f -size +1M -print0 | sudo xargs -r0 lbzip2 -v
    return $?
}

#######################################
# Archive files to the remote host.
# Arguments:
#   $1: local logs to copy
#   $2: remote location to copy files
# Returns:
#   0: All commands were successful
#   1: Failure detected with at least one command
#######################################
scp_files() {
    local rc=0
    copied=()
    # shellcheck disable=SC2045,SC2086
    for file in $(ls -d ${1})
    do
        file_name=${file##*/}
        archive_name="${file_name%%.*}.$(hostname -s).${file_name#*.}"
        if scp -r "${file}" "${2}"/"${archive_name}"; then
            copied+=("${file}")
            if ! rm -fr "${file}"; then
                echo "  Error removing ${file}"
                echo "    $(ls -al ${file})"
                rc=1
            fi
        else
            echo "  Failed to archive ${file} to ${2}"
            rc=1
        fi
    done
    echo "  The following files were archived:"
    for file in "${copied[@]:-no files}"
    do
        echo "    ${file}"
    done
    return ${rc}
}

#######################################
# Run cart_logtest.py against the logs.
# Arguments:
#   $1: local logs
# Returns:
#   0: All cart_logtest were successful
#   1: Failure detected with cart_logtest
#######################################
run_cartlogtest() {
    local rc=0
    if [ -f "${CART_LOGTEST_PATH}" ]; then
        # shellcheck disable=SC2045,SC2086
        for file in $(ls -d ${1})
        do
            if [ -f ${file} ] && [[ ! ${file} == *".cart_logtest."* ]]; then
                logtest_log="${file%.*}.cart_logtest.${file##*.}"
                if ! ${CART_LOGTEST_PATH} ${file} > ${logtest_log} 2>&1; then
                    echo "  Error: details in ${file}_cart_testlog"
                    rc=1
                fi
            fi
        done
    else
        echo "  Error: ${CART_LOGTEST_PATH} does not exist!"
        rc=1
    fi
    return ${rc}
}

####### MAIN #######
if [[ $# -eq 0 ]] ; then
    usage
    exit 1
fi

# Setup defaults
ARCHIVE_DEST=""
FILES_TO_PROCESS=""
COMPRESS="false"
VERBOSE="false"
CART_LOGTEST="false"
EXCLUDE_ZIP="false"
THRESHOLD=""
rc=0

# Step through arguments
while getopts "vhxzca:f:t:" opt; do
    case ${opt} in
        a )
            ARCHIVE_DEST=$OPTARG
            ;;
        f )
            FILES_TO_PROCESS=$OPTARG
            ;;
        t )
            THRESHOLD=$OPTARG
            ;;
        c )
            CART_LOGTEST="true"
            ;;
        z )
            COMPRESS="true"
            ;;
        x )
            EXCLUDE_ZIP="true"
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

if ! "${VERBOSE}"; then
    # Redirect 'set -e' output to a log file if not verbose
    script_name="${0##*/}"
    log_file="/tmp/${script_name%%.*}_$(/bin/date +%Y%m%d_%H%M%S).log"
    exec 2> "${log_file}"
    echo "Logging ${script_name} output to ${log_file}"
fi
set -ex

# Verify files have been specified and they exist on this host
if ! ret=$(check_files_input "${FILES_TO_PROCESS}"); then
    if [[ ${ret} -eq 1 ]]; then
        echo "Please specify -f option."
        exit 1
    elif [[ ${ret} -eq 2 ]]; then
        echo "No files matched ${FILES_TO_PROCESS}. Nothing to do."
        if ! "${VERBOSE}" && [ -n "${ARCHIVE_DEST}" ]; then
            if ! scp_files "${log_file}" "${ARCHIVE_DEST}"; then
                exit 1
            fi
        fi
        exit 0
    fi
fi

# Display big files to stdout
if [ -n "${THRESHOLD}" ]; then
    echo "Checking if files exceed ${THRESHOLD} threshold ..."
    list_tag_files "${FILES_TO_PROCESS}" "${THRESHOLD}"
fi

# Run cart_logtest.py on FILES_TO_PROCESS
if "${CART_LOGTEST:-false}"; then
    rel_path="/cart/cart_logtest.py"
    if ! CART_LOGTEST_PATH="$(dirname "$(readlink -f "${0}")")${rel_path}"; then
        echo "Failed to get path to cart_logtest.py ..."
        exit 1
    fi

    echo "Running ${CART_LOGTEST_PATH} ..."
    if ! run_cartlogtest "${FILES_TO_PROCESS}"; then
        rc=1
    fi
fi

# Compress files in FILES_TO_PROCESS if not running on VM host.
if "${COMPRESS:-false}"; then
    echo "Compressing files larger than 1M ..."
    check_str="Hypervisor detected"
    if output=$(dmesg); then
        if ! grep -q "${check_str}" <<< "${output}" || "${EXCLUDE_ZIP:-false}";
        then
            if ! compress_files "${FILES_TO_PROCESS}"; then
                echo "  Error detected compressing files"
                rc=1
            fi
        elif ! "${EXCLUDE_ZIP:-false}"; then
            echo "VM system detected, skipping compression ..."
        fi
    else
        echo "Error: dmesg command failure: ${output}"
        rc=1
    fi
fi

# Scp files specified in FILES_TO_PROCESS to ARCHIVE_DEST
if [ -n "${ARCHIVE_DEST}" ]; then
    echo "Archiving logs to ${ARCHIVE_DEST} ..."
    if ! scp_files "${FILES_TO_PROCESS}" "${ARCHIVE_DEST}"; then
        rc=1
    fi

    if ! "${VERBOSE}"; then
        # Finally archive this script's log file
        if ! scp_files "${log_file}" "${ARCHIVE_DEST}"; then
            rc=1
        fi
    fi
fi

exit ${rc}
