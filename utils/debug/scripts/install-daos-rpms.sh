#!/bin/bash

# set -x
set -eu -o pipefail

CWD="$( realpath "$( dirname "$0" )" )"

BASE_DEPS_RPMS=(
	bat
	gperftools
	hwloc
	ipmctl
	jq
	vim
)

DAOS_SERVER_RPMS=(
	daos
	daos-debuginfo
	daos-server
	daos-server-debuginfo
	daos-spdk
	daos-spdk-debuginfo
)

DAOS_CLIENT_RPMS=(
	daos
	daos-debuginfo
	daos-client
	daos-client-debuginfo
	daos-devel
	# daos-client-tests
	# daos-client-tests-debuginfo
	# daos-client-tests-openmpi
	# daos-client-tests-openmpi-debuginfo
)

DAOS_ADMIN_RPMS=(
	daos
	daos-debuginfo
	daos-admin
	daos-admin-debuginfo
)

MERCURY_RPMS=(
	mercury
	mercury-debuginfo
	mercury-ucx
	mercury-ucx-debuginfo
)

CONFLICT_RPMS=(
	spdk
	dpdk
)


DNF_INSTALL_OPTS="--setopt=install_weak_deps=False"
DNF_EXCLUDE_DAOS_OPT="--exclude="
for rpm in $( echo ${DAOS_SERVER_RPMS[*]} ${DAOS_CLIENT_RPMS[*]} ${DAOS_ADMIN_RPMS[*]} | tr ' ' '\n' | sort | uniq ) ; do
	DNF_EXCLUDE_DAOS_OPT+="$rpm,"
done
DNF_EXCLUDE_DAOS_OPT="${DNF_EXCLUDE_DAOS_OPT::-1}"

echo
echo "[INFO] Remove old daos and mercury rpms"
dnf remove -y ${DAOS_ADMIN_RPMS[@]} ${DAOS_CLIENT_RPMS[@]} ${MERCURY_RPMS[@]} ${CONFLICT_RPMS[@]}

echo
echo "[INFO] Install base dependencies"
dnf install $DNF_INSTALL_OPTS -y ${BASE_DEPS_RPMS[@]}

echo
echo "[INFO] Install admin rpms"
dnf install $DNF_INSTALL_OPTS -y ${DAOS_ADMIN_RPMS[@]}

echo
echo "[INFO] Install daos server rpms"
dnf install $DNF_INSTALL_OPTS -y ${DAOS_SERVER_RPMS[@]}

echo
echo "[INFO] Install daos client rpms"
dnf install $DNF_INSTALL_OPTS -y ${DAOS_CLIENT_RPMS[@]}

echo
echo "[INFO] Install mercury rpms"
dnf install $DNF_INSTALL_OPTS -y ${MERCURY_RPMS[@]}

# echo
# echo "[INFO] Install mpich and ior rpms"
# dnf install $DNF_INSTALL_OPTS $DNF_EXCLUDE_DAOS_OPT -y mpich ior

echo
echo "[INFO] Listing installed DAOS RPMs"
rpm -qa "daos*" | sort

echo
echo "[INFO] Listing installed Mercury RPMs"
rpm -qa "mercury*" | sort
