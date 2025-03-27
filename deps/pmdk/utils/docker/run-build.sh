#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2016-2023, Intel Corporation

#
# run-build.sh - is called inside a Docker container; prepares the environment
#                and starts a build of PMDK project.
#

set -e

# Prepare build environment
./prepare-for-build.sh

# Build all and run tests
cd $WORKDIR
if [ "$SRC_CHECKERS" != "0" ]; then
	make -j$(nproc) check-license
	make -j$(nproc) cstyle
fi

echo "## Running make"
make -j$(nproc)
echo ""
echo "## Running make test"
make -j$(nproc) test
echo ""
echo "## Running make pcheck"
# do not change -j1 to -j$(nproc) in case of tests (make check/pycheck)
make -j1 pcheck TEST_BUILD=$TEST_BUILD
echo ""
echo "## Running make pycheck"
# do not change -j1 to -j$(nproc) in case of tests (make check/pycheck)
make -j1 pycheck
echo ""
echo "## Running make source"
make -j$(nproc) DESTDIR=/tmp source
