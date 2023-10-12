#!/bin/bash

set -eu -o pipefail
# set -x

# Build the daos_el9 image
# Usage: build-daos_el9.sh <image-tag>
# Example: build-daos_el9.sh daos_el9:latest

pushd $HOME/work/daos
source utils/sl/setup_local.sh
 docker build .  \
	-f utils/docker/Dockerfile.ubuntu  \
	--tag daos/ubuntu-24.04:2.8  \
	--build-arg BASE_DISTRO=ubuntu:24.04  \
	--build-arg DAOS_JAVA_BUILD=no  \
	--build-arg COMPILER=gcc  \
	--build-arg DAOS_KEEP_SRC=yes  \
	--build-arg DAOS_DEPS_BUILD=yes  \
	--build-arg DAOS_BUILD=yes  \
	--build-arg DAOS_TARGET_TYPE=release  \
	--build-arg DAOS_PACKAGES_BUILD=yes   \
	--build-arg DEPS_JOBS=$(nproc) \
	--build-arg JOBS=$(nproc) \
	--build-arg DAOS_HTTP_PROXY="http://proxy.houston.hpecorp.net:8080" \
	--build-arg DAOS_HTTPS_PROXY="http://proxy.houston.hpecorp.net:8080"
popd
