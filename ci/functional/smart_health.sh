#!/bin/sh
set -x

dev_to_test="/dev/sda1"

stat ${dev_to_test} &&
    which smartctl &&
    smartctl -t short -a ${dev_to_test} &&
    sleep 180 &&
    smartctl -a ${dev_to_test}
