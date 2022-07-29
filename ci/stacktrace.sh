#!/bin/bash

stacktrace() {
    local msg=${1:-"Unchecked error condition at"}
    local i=${2:-0}

    while true; do
        read -r line func file < <(caller "$i")
        if [ -z "$line" ]; then
            # prevent cascading ERRs
            trap '' ERR
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
