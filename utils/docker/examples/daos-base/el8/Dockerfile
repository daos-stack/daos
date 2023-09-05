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
# - DAOS_VERSION             Version of DAOS to use (default "2.4.0-2.el8")
# - DAOS_CLIENT_UNAME        User name of the client (default "root")
# - DAOS_CLIENT_UID          User id of the client (default "0")
# - DAOS_CLIENT_GNAME        Group name of the client (default "root")
# - DAOS_CLIENT_GID          Group id of the client (default "0")

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

# Install base DAOS package
# XXX NOTE XXX Variable allowing to build the image without using global --no-cache option and thus
# XXX NOTE XXX to not update all rpms.  To work properly a random value such as the date of the day
# XXX NOTE XXX should be given.
ARG	BUST_CACHE=""
ARG	DAOS_REPOS="https://packages.daos.io/v2.4/EL8/packages/x86_64/"
ARG	DAOS_GPG_KEYS="https://packages.daos.io/RPM-GPG-KEY"
ARG	DAOS_REPOS_NOAUTH=""
ARG	DAOS_VERSION="2.4.0-2.el8"
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
	dnf install                                                                                \
		sudo                                                                               \
		daos-${DAOS_VERSION} &&                                                            \
	dnf clean all

# Install DAOS client user and group information
ARG	DAOS_CLIENT_UNAME="root"
ARG	DAOS_CLIENT_UID="0"
ARG	DAOS_CLIENT_GNAME="root"
ARG	DAOS_CLIENT_GID="0"
RUN	if ! getent group ${DAOS_CLIENT_GNAME} > /dev/null 2>&1; then                              \
		echo "[INFO] Adding DAOS client group ${DAOS_CLIENT_GNAME}" ;                      \
		groupadd -g ${DAOS_CLIENT_GID} ${DAOS_CLIENT_GNAME} ;                              \
	fi &&                                                                                      \
	if ! getent passwd ${DAOS_CLIENT_UNAME} > /dev/null 2>&1; then                             \
		echo "[INFO] Adding DAOS client user ${DAOS_CLIENT_UNAME}" ;                       \
		useradd -g ${DAOS_CLIENT_GID} -u ${DAOS_CLIENT_UID} ${DAOS_CLIENT_UNAME} ;         \
	fi

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/bin/bash"]
CMD ["-i"]
