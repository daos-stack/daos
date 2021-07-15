#!/bin/sh
set -x

which smartctl &&
    smartctl -i /dev/pmem0 &&
    smartctl -t short -a /dev/pmem0 &&
    sleep 180 &&
    smartctl -a /dev/pmem0
