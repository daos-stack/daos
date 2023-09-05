# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - RHEL_BASE_IMAGE_NAME     Base docker image name to use (default "rockylinux/rockylinux")
# - RHEL_BASE_IMAGE_TAG      Tag identifier of the base docker image to use (default "8.6")
# - BUST_CACHE               Manage docker building cache (default undefined).  To invalidate the
#                            cache, a random value such as the date of day shall be given.
# - DAOS_REPOS               Space separated list of repos needed to install DAOS (default
#                            "https://packages.daos.io/v2.4/EL8/packages/x86_64/")
# - DAOS_GPG_KEYS            Space separated list of GPG keys associated with DAOS repos (default
#                            "https://packages.daos.io/RPM-GPG-KEY")
# - DAOS_REPOS_NOAUTH        Space separated list of repos to use without GPG authentication
#                            (optional)
# - DAOS_VERSION             Version of DAOS to use (default "2.4.0-1.el8")
# - DAOS_SERVER_HOSTLIST     Hostlist of the DAOS server nodes (mandatory)
# - DAOS_ACCESS_POINTS       Hostlist of the quorum of DAOS access pointa nodes (mandatory)
# - DAOS_HUGEPAGES_NBR       Number of huge pages to allocate for SPDK (default 4096)
# - DAOS_CTRL_METADATA       Storage location of the control plane metadata (default
#                            "/tmp/daos_server")
# - DAOS_TARGETS             Number of I/O service threads (default 1)
# - DAOS_HELPERS             Number of additional offload service threads (default 0)
# - DAOS_ENGINE_IFACE_NAME   Fabric network interface used by the DAOS engine (default "eth0")
# - DAOS_SCM_SIZE            Size in GB of the RAM emulating SCM devices (mandatory)
# - DAOS_DATA_BDEVS          List of NVMe IDs used for storing data (mandatory)
# - DAOS_METADATA_BDEVS      List of NVMe IDs used for storing metadata such as vos, wal and rdb
#                            (mandatory)

# Pull base image
ARG	RHEL_BASE_IMAGE_NAME=rockylinux/rockylinux
ARG	RHEL_BASE_IMAGE_TAG=8.6
FROM	$RHEL_BASE_IMAGE_NAME:$RHEL_BASE_IMAGE_TAG
LABEL	maintainer="daos@daos.groups.io"

# Base configuration of dnf and system update
RUN	dnf clean all &&                                                                           \
	dnf makecache &&                                                                           \
	dnf --assumeyes install dnf-plugins-core &&                                                \
	dnf config-manager --save --setopt=assumeyes=True &&                                       \
	dnf config-manager --save --setopt=fastestmirror=True &&                                   \
	dnf config-manager --set-enabled powertools &&                                             \
	dnf install epel-release &&                                                                \
	dnf update &&                                                                              \
	dnf clean all

# Install DAOS package
# XXX NOTE XXX Variable allowing to build the image without using global --no-cache option and thus
# XXX NOTE XXX to not update all rpms.  To work properly a random value such as the date of the day
# XXX NOTE XXX should be given.
ARG	BUST_CACHE=""
ARG	DAOS_REPOS="https://packages.daos.io/v2.4/EL8/packages/x86_64/"
ARG	DAOS_GPG_KEYS="https://packages.daos.io/RPM-GPG-KEY"
ARG	DAOS_REPOS_NOAUTH=""
ARG	DAOS_VERSION="2.4.0-1.el8"
RUN	if [ -n "$BUST_CACHE" ] ; then                                                             \
		echo "[INFO] Busting cache" ;                                                      \
		dnf update ;                                                                       \
	fi &&                                                                                      \
	for repo in ${DAOS_REPOS} ; do                                                             \
		echo "[INFO] Adding rpm repository: $repo" ;                                       \
		dnf config-manager --add-repo "$repo" ;                                            \
	done &&                                                                                    \
	for gpg_key in ${DAOS_GPG_KEYS} ; do                                                       \
		echo "[INFO] Adding repositories gpg key: $gpg_key" ;                              \
		rpmkeys --import "$gpg_key" ;                                                      \
	done &&                                                                                    \
	for repo in ${DAOS_REPOS_NOAUTH} ; do                                                      \
		echo "[INFO] Disabling authentication for repository: $repo" ;                     \
		dnf config-manager --save --setopt="${repo}.gpgcheck=0" ;                          \
	done &&                                                                                    \
	echo "[INFO] Installing DAOS" ;                                                            \
	dnf install vim pciutils nvme-cli                                                          \
		sudo                                                                               \
		daos-${DAOS_VERSION}                                                               \
		daos-admin-${DAOS_VERSION}                                                         \
		daos-server-${DAOS_VERSION} &&                                                     \
	dnf clean all

# Configuration of DMG
ARG	DAOS_SERVER_HOSTLIST=""
COPY    daos_control.yml.in /tmp/daos_control.yml.in
RUN	if [[ -z "${DAOS_SERVER_HOSTLIST}" ]] ; then                                               \
		echo "[ERROR] Docker build argument DAOS_SERVER_HOSTLIST is not defined" ;         \
		exit 1 ;                                                                           \
	fi ;                                                                                       \
	sed_opts="--regexp-extended" ;                                                             \
	sed_opts+=" --expression s/@DAOS_SERVER_HOSTLIST@/${DAOS_SERVER_HOSTLIST}/" ;              \
	sed $sed_opts /tmp/daos_control.yml.in > /etc/daos/daos_control.yml &&                     \
	rm -f /tmp/daos_control.yml.in

# Configuration of the server
ARG	DAOS_ACCESS_POINTS=""
ARG	DAOS_HUGEPAGES_NBR=4096
ARG	DAOS_CTRL_METADATA="/tmp/daos_server"
ARG	DAOS_NUMA_NODE=0
ARG	DAOS_TARGETS=1
ARG	DAOS_HELPERS=0
ARG	DAOS_ENGINE_IFACE_NAME=eth0
ARG	DAOS_SCM_SIZE=""
ARG	DAOS_DATA_BDEVS=""
ARG	DAOS_METADATA_BDEVS=""
# XXX NOTE XXX With a production platform, this configuration file should be provided with a volume
# XXX NOTE XXX (or ConfigMaps for K8S).
COPY	daos_server.yml.in /tmp/daos_server.yml.in
# XXX NOTE XXX HugePages and vfio seem to not be well managed with docker and latest version of the
# XXX NOTE XXX kernel.  More details could be found at:
# XXX NOTE XXX - https://stackoverflow.com/questions/64707324/dpdk-sample-application-aborts-after-eal-couldnt-get-fd-on-hugepage-file
# XXX NOTE XXX - https://doc.dpdk.org/guides/linux_gsg/enable_func.html
RUN	for it in DAOS_ACCESS_POINTS DAOS_SCM_SIZE DAOS_DATA_BDEVS DAOS_METADATA_BDEVS ; do        \
		if eval "[[ -z \"\$$it\" ]]" ; then                                                \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done ;                                                                                     \
	sed_opts="--regexp-extended" ;                                                             \
	sed_opts+=" --expression s/@DAOS_ACCESS_POINTS@/${DAOS_ACCESS_POINTS}/" ;                  \
	sed_opts+=" --expression s/@DAOS_HUGEPAGES_NBR@/${DAOS_HUGEPAGES_NBR}/" ;                  \
	sed_opts+=" --expression s#@DAOS_CTRL_METADATA@#${DAOS_CTRL_METADATA}#" ;                  \
	sed_opts+=" --expression s/@DAOS_TARGETS@/${DAOS_TARGETS}/" ;                              \
	sed_opts+=" --expression s/@DAOS_HELPERS@/${DAOS_HELPERS}/" ;                              \
	sed_opts+=" --expression s/@DAOS_ENGINE_IFACE_NAME@/${DAOS_ENGINE_IFACE_NAME}/" ;          \
	sed_opts+=" --expression s/@DAOS_SCM_SIZE@/${DAOS_SCM_SIZE}/" ;                            \
	sed_opts+=" --expression s/@DAOS_DATA_BDEVS@/${DAOS_DATA_BDEVS}/" ;                        \
	sed_opts+=" --expression s/@DAOS_METADATA_BDEVS@/${DAOS_METADATA_BDEVS}/" ;                \
	sed $sed_opts /tmp/daos_server.yml.in > /etc/daos/daos_server.yml &&                       \
	rm -f /tmp/daos_server.yml.in

# Install DAOS server launcher
COPY	run-daos_server /usr/local/sbin/run-daos_server
RUN	chmod 755 /usr/local/sbin/run-daos_server

# Copy missing configuration file
COPY 50_daos_limits.conf /etc/security/limits.d/50_daos_limits.conf

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/local/sbin/run-daos_server"]
CMD ["start"]
