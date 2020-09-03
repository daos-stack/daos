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

This script will clush to provided servers and print to stdout the files on
the provided remote directory path. Files that  exceed the provided user set
threshold value will have a BIG_FILE tag at the beginning of the line.

Example:
    3903 /path/to/1.log
    BIG_FILE 9991 /path/to/big/2.log
    2298 /path/to/small/3.log
    BIG_FILE 6523 /path/to/another/big/4.log

Once the big files have been tagged to stdout, the script will check if local
server is a on HW or a VM, if VM=True, then compression will be skipped and the
script will continue to scping the files from the remote directory to local test
runner server. Otherwise, if HW=True, then files will be compressed remotely and
afterwards scp'd to local test runner server.

 Options:
      -s        Comma-separated list of hosts to get remote files from
      -d        Local path to copy remote files to
      -r        Remote path to get files from
      -t        Threshold value to determine classification of big logs
      -v        Display commands being executed
      -h        Display this help and exit
"
}

#######################################
# Print set GLOBAL variables.
#######################################
display_set_vars() {
    echo "Set Variables:
    SERVERS = ${SERVERS}
    REMOTE_SRC = ${REMOTE_SRC}
    LOCAL_SRC = ${LOCAL_SRC}
    THRESHOLD = ${THRESHOLD}
    VERBOSE = ${VERBOSE}"
}

#######################################
# Clush in remote host to access .log files, print
# the size of the files and conditionally compress
# log files remotely depending on check_hw output.
# Arguments:
#   hosts: list of hosts to remotely access
#   rlogs_dir: path to remote .log files
#   logs_dir: path to local logs dir where to store files.
#   threshold: value to use as threhold to classify BIG_FILEs
#   hw_flag: bool value
# Returns:
#   stdout output specifying the files
#######################################
get_remote_files() {
    awk_cmd="
    {if (${4} <= \$1) {
        print \"BIG_FILE\",hostname,\$1,\$2
    }
    else {
        print \$1,\$2
    }}"

    check_hw="
    dmesg | grep \"Hypervisor detected\";
    echo $?"

    comp_cmd="
    find ${2} -maxdepth 0 -type f -size +1M -print0 | xargs -r0 lbzip2"

    copy_cmds="set -eu;
    rc=0;
    copied=();
    for file in \$(ls ${2});
    do ls -sh \$file;
    if scp -p \$file ${THIS_HOST}:${3}/\${file##*/}-\$(hostname -s);
    then copied+=(\$file);
    if ! sudo rm -fr \$file;
    then ((rc++));
    ls -al \$file;
    fi;
    fi;
    done;
    echo Copied \${copied[@]:-no files};
    exit \$rc;"

    list_cmd="
    du -ab -d 1 ${2} | awk -v hostname=\"$(hostname)\" '${awk_cmd}'"

    clush_cmd="
    if [ ${4} -gt 0 ]; then
        echo \"LISTING FILES\";${list_cmd};
    fi;
    if [ \$(${check_hw}) -eq 0 ]; then
        echo \"COMPRESSING\"; ${comp_cmd};
    fi;
    echo \"COPYING FILES\";${copy_cmds}"

    if [[ "${VERBOSE}"  == "true" ]]; then
        echo "Running: clush -B -w ${1}  ${clush_cmd}";
    fi
    clush -B -l jenkins -R ssh -S -w ${1} "${clush_cmd}"
}

####### MAIN #######
if [[ $# -eq 0 ]] ; then
    usage
    exit 1
fi

# Setup defaults
VERBOSE="false"
THRESHOLD=0

# Step through arguments
while getopts "vhs:r:d:t:" opt; do
    case ${opt} in
        s )
            SERVERS=$OPTARG
            ;;
        r )
            REMOTE_SRC=$OPTARG
            ;;
        d )
            LOCAL_SRC=$OPTARG
            ;;
        t )
            THRESHOLD=$OPTARG
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
shift "$(($OPTIND -1))"

# Run
THIS_HOST=$(hostname -s)
display_set_vars
get_remote_files ${SERVERS} "${REMOTE_SRC}" "${LOCAL_SRC}" ${THRESHOLD}
