#!/bin/bash
#
#  Copyright 2020-2022 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

DAOS_TEST_SHARED_DIR=$(mktemp -d -p /mnt/share/)
trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT

export DAOS_TEST_SHARED_DIR
export TEST_RPMS=true
export REMOTE_ACCT=jenkins
export WITH_VALGRIND="$WITH_VALGRIND"
export STAGE_NAME="$STAGE_NAME"

HTTPS_PROXY="${HTTPS_PROXY:-}" /usr/lib/daos/TESTING/ftest/ftest.sh \
    "$TEST_TAG" "$TNODES" "$FTEST_ARG"
