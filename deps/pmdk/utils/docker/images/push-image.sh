#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2016-2023, Intel Corporation

#
# push-image.sh - pushes the Docker image to $DOCKER_REPO.
#
# The script utilizes $GH_CR_USER and $GH_CR_PAT variables
# to log in to $DOCKER_REPO. These variables can be set
# in the project's CI configuration for automated builds.
#

set -e

source $(dirname $0)/../set-ci-vars.sh

if [[ -z "$OS" ]]; then
	echo "OS environment variable is not set"
	exit 1
fi

if [[ -z "$OS_VER" ]]; then
	echo "OS_VER environment variable is not set"
	exit 1
fi

if [[ -z "$CI_CPU_ARCH" ]]; then
	echo "CI_CPU_ARCH environment variable is not set"
	exit 1
fi

if [[ -z "${DOCKER_REPO}" ]]; then
	echo "DOCKER_REPO environment variable is not set"
	exit 1
fi

if [[ -z "$IMG_VER" ]]; then
	echo "IMG_VER environment variable is not set"
	exit 1
fi

if [[ -z "${GH_CR_USER}" || -z "${GH_CR_PAT}" ]]; then
	echo "ERROR: variables GH_CR_USER=\"${GH_CR_USER}\" and GH_CR_PAT=\"${GH_CR_PAT}\"" \
		"have to be set properly to allow login to the $DOCKER_REPO."
	exit 1
fi

TAG="${IMG_VER}-${OS}-${OS_VER}-${CI_CPU_ARCH}"

# Check if the image tagged with $TAG exists locally
if [[ ! $(docker images -a | awk -v pattern="^${DOCKER_REPO}:${TAG}\$" \
	'$1":"$2 ~ pattern') ]]
then
	echo "ERROR: Docker image tagged ${DOCKER_REPO}:${TAG} does not exists locally."
	exit 1
fi

# Log in to $DOCKER_REPO
echo "${GH_CR_PAT}" | docker login "${DOCKER_REPO}" -u="${GH_CR_USER}" --password-stdin

echo "Push the image '${DOCKER_REPO}:${TAG}' to the $DOCKER_REPO."
docker push ${DOCKER_REPO}:${TAG}
echo "Image pushed."
