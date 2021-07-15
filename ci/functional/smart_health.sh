#!/bin/sh
set -x

which smartctl || return

smartctl -i /dev/pmem0 || return

smartctl -t short -a /dev/pmem0 || return

sleep 180

smartctl -a /dev/pmem0
