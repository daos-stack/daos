#!/bin/bash

# set -x
set -eu -o pipefail

CWD="$( realpath "$( dirname "$0" )" )"

BASE_DEPS_DEBS=(
	bat
	google-perftools
	hwloc
	ipmctl
	jq
	vim
)

DAOS_SERVER_DEBS=(
	daos
	daos-debuginfo
	daos-server
	daos-server-debuginfo
	daos-spdk
	daos-spdk-debuginfo
)

DAOS_CLIENT_DEBS=(
	daos
	daos-debuginfo
	daos-client
	daos-client-debuginfo
	# daos-devel
	# daos-client-tests
	# daos-client-tests-debuginfo
	# daos-client-tests-openmpi
	# daos-client-tests-openmpi-debuginfo
)

DAOS_ADMIN_DEBS=(
	daos
	daos-debuginfo
	daos-admin
	daos-admin-debuginfo
)

MERCURY_DEBS=(
	mercury
	mercury-debuginfo
	mercury-libfabric
	mercury-libfabric-debuginfo
	mercury-ucx
	mercury-ucx-debuginfo
)

CONFLICT_DEBS=(
	spdk
	dpdk
)

echo
echo "[INFO] Remove old daos and mercury debs"
sudo apt remove -y ${DAOS_ADMIN_DEBS[@]} ${DAOS_CLIENT_DEBS[@]} ${MERCURY_DEBS[@]} ${CONFLICT_DEBS[@]}

echo
echo "[INFO] Install base dependencies"
sudo apt install -y ${BASE_DEPS_DEBS[@]}

echo
echo "[INFO] Install admin debs"
sudo apt install -y ${DAOS_ADMIN_DEBS[@]}

echo
echo "[INFO] Install daos server debs"
sudo apt install -y ${DAOS_SERVER_DEBS[@]}

echo
echo "[INFO] Install daos client debs"
sudo apt install -y ${DAOS_CLIENT_DEBS[@]}

echo
echo "[INFO] Install mercury debs"
sudo apt install -y ${MERCURY_DEBS[@]}

echo
echo "[INFO] Listing installed DAOS RPMs"
sudo dpkg -l "daos*"

echo
echo "[INFO] Listing installed Mercury RPMs"
sudo dpkg -l "mercury*"
