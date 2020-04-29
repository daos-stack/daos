#!/bin/bash

rpc() {
    local clush_options="$1"
    local nodes="$2"
    local rpc="$3"
    shift 3

    clush $clush_options -B -S -w "$nodes" "set -e
$(cat ci/rpc_lib.sh)
# there should be a way to use \"$*\" for the below list, but can't
# seem to quite work it out yet
$rpc \"$1\" \"$2\" \"$3\" \"$4\" \"$5\" \"$6\" \"$7\" \"$8\" \"$9\""
}