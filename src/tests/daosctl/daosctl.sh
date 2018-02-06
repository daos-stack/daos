#!/bin/sh
# this script is for convenience, edit as appropriate for your
# installation location

BASE=set-daos-base-path-here
$BASE/install/bin/orterun --np 1 \
    --ompi-server file:$BASE/install/tmp/urifile \
    $BASE/install/bin/daosctl $argv
