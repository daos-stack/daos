#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2016-2023, Intel Corporation

#
# build-image.sh <OS-VER> <ARCH> - prepares a Docker image with <OS>-based
#                environment intended for the <ARCH> CPU architecture
#                designed for building PMDK project, according to
#                the Dockerfile.<OS-VER> file located in the same directory.
#
# The script can be run locally.
#

set -e

OS_VER=$1
CPU_ARCH=$2

function usage {
	echo "Usage:"
	echo "    build-image.sh <OS-VER> <ARCH>"
	echo "where:"
	echo "  <OS-VER> - can be for example 'ubuntu-22.04' provided "\
		"a Dockerfile named 'Dockerfile.ubuntu-22.04' "\
		"exists in the current directory and"
	echo "  <ARCH> - is a CPU architecture, for example 'x86_64'"
}

# Check if two first arguments are not empty
if [[ -z "$2" ]]; then
	usage
	exit 1
fi

# Check if the file Dockerfile.OS-VER exists
if [[ ! -f "Dockerfile.$OS_VER" ]]; then
	echo "Error: Dockerfile.$OS_VER does not exist."
	echo
	usage
	exit 1
fi

if [[ -z "${DOCKER_REPO}" || -z "${IMG_VER}" ]]; then
	echo "Error: DOCKER_REPO (${DOCKER_REPO}) or IMG_VER (${IMG_VER}) " \
		"environment variables are not set."
	exit 1
fi

# Build a Docker image tagged as follows:
tag=${DOCKER_REPO}:${IMG_VER}-${OS_VER}-${CPU_ARCH}
docker build -t $tag \
	--build-arg http_proxy=$http_proxy \
	--build-arg https_proxy=$https_proxy \
	-f Dockerfile.$OS_VER .
