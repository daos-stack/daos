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
#                            "https://packages.daos.io/v2.2/EL8/packages/x86_64/")
# - DAOS_GPG_KEYS            Space separated list of GPG keys associated with DAOS repos (default
#                            "https://packages.daos.io/RPM-GPG-KEY")
# - DAOS_REPOS_NOAUTH        Space separated list of repos to use without GPG authentication
#                            (optional)
# - DAOS_VERSION             Version of DAOS to use (default "2.2.0-4.el8")
# - DAOS_CLIENT_UNAME        User name of the client (mandatory)
# - DAOS_CLIENT_UID          User id of the client (mandatory)
# - DAOS_CLIENT_GNAME        Group name of the client (mandatory)
# - DAOS_CLIENT_GID          Group id of the client (mandatory)

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
ARG	DAOS_REPOS="https://packages.daos.io/v2.2/EL8/packages/x86_64/"
ARG	DAOS_GPG_KEYS="https://packages.daos.io/RPM-GPG-KEY"
ARG	DAOS_REPOS_NOAUTH=""
ARG	DAOS_VERSION="2.2.0-4.el8"
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
		daos-${DAOS_VERSION}                                                               \
		daos-client-${DAOS_VERSION}                                                        \
		daos-client-tests-${DAOS_VERSION} &&                                               \
	dnf clean all

# Install DAOS client user and group information
ARG	DAOS_CLIENT_UNAME=""
ARG	DAOS_CLIENT_UID=""
ARG	DAOS_CLIENT_GNAME=""
ARG	DAOS_CLIENT_GID=""
RUN	for it in DAOS_CLIENT_UNAME DAOS_CLIENT_UID DAOS_CLIENT_GNAME DAOS_CLIENT_GID ; do         \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done ;                                                                                     \
	echo "[INFO] Adding DAOS client user ${DAOS_CLIENT_UNAME}" ;                               \
	groupadd -g ${DAOS_CLIENT_GID} ${DAOS_CLIENT_GNAME} &&                                     \
	useradd -g ${DAOS_CLIENT_GID} -u ${DAOS_CLIENT_UID} ${DAOS_CLIENT_UNAME}

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/bin/bash"]
CMD ["-i"]
