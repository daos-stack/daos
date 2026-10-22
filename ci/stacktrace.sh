#!/bin/bash

#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#

stacktrace() {
    local msg=${1:-"Unchecked error condition at"}
    local i=${2:-0}

    # prevent re-triggering the trap
    trap '' ERR

    while true; do
        read -r line func file < <(caller "$i") || true
        if [ -z "$line" ]; then
            break
        fi
        if [ "$i" -eq 0 ]; then
            echo -e "$msg: \c"
        else
            echo -e "Called from: \c"
        fi
        echo "$file:$line:$func()"
        (( i++ )) || true
    done
}

trap 'stacktrace' ERR
