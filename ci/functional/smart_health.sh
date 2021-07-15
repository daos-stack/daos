#!/bin/sh
set -x

dev_to_test="/dev/sda"

stat ${dev_to_test} &&
    which smartctl &&
    smartctl -i ${dev_to_test} &&
    smartctl -t short -a ${dev_to_test} &&
    sleep 180 &&
    smartctl -a ${dev_to_test}
