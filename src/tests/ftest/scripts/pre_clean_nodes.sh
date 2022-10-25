#!/bin/bash
# /*
#  * (C) Copyright 2016-2022 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

set -ex
declare -a ftest_mounts
mapfile -t ftest_mounts < <(grep 'added by ftest.sh' /etc/fstab)
for n_mnt in "${ftest_mounts[@]}"; do
    mpnt=("${n_mnt}")
    sudo umount "${mpnt[1]}"
done
sudo sed -i -e "/added by ftest.sh/d" /etc/fstab
