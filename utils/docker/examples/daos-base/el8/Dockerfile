# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_CLIENT_UNAME   User name of the client (mandatory)
# - DAOS_CLIENT_UID     User id of the client (mandatory)
# - DAOS_CLIENT_GNAME   Group name of the client (mandatory)
# - DAOS_CLIENT_GID     Group id of the client (mandatory)
# - LINUX_IMAGE_NAME    Base docker image name to use (mandatory)
# - LINUX_IMAGE_TAG     Tag identifier of the base docker image to use (mandatory)
# - BUST_CACHE          Manage docker building cache (default undefined).  To invalidate the
#                       cache, a random value such as the date of day shall be given.
# - DAOS_REPOS          Space separated list of repos needed to install DAOS (mandatory)
# - DAOS_GPG_KEYS       Space separated list of GPG keys associated with DAOS repos mandatory)
# - DAOS_REPOS_NOAUTH   Space separated list of repos to use without GPG authentication (optional)
# - DAOS_VERSION        Version of DAOS to use (mandatory)

# Pull base image
ARG	LINUX_IMAGE_NAME=""
ARG	LINUX_IMAGE_TAG=""
FROM	"$LINUX_IMAGE_NAME:$LINUX_IMAGE_TAG"
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
# XXX NOTE XXX The variable §BUST_CACHE allows to build the image without using global --no-cache
# XXX NOTE XXX option and thus to not update all rpms.  To work properly a random value such as the
# XXX NOTE XXX date of the day should be given.
ARG	BUST_CACHE=""
ARG	DAOS_REPOS=""
ARG	DAOS_GPG_KEYS=""
ARG	DAOS_REPOS_NOAUTH=""
ARG	DAOS_VERSION=""
RUN	for it in DAOS_REPOS DAOS_GPG_KEYS DAOS_VERSION ; do                                       \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	if [ -n "$BUST_CACHE" ] ; then                                                             \
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
		daos-${DAOS_VERSION} &&                                                            \
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
	done &&                                                                                    \
	if ! getent group ${DAOS_CLIENT_GNAME} > /dev/null 2>&1; then                              \
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
