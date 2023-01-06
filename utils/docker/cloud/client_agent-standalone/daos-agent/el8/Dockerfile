# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (default "rocky8.6")
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
# - DAOS_ACCESS_POINTS       Hostname list of the DAOS access points (mandatory)
# - DAOS_PORT                DAOS access point port number to connect (default "1001")
# - DAOS_IFACE_CFG           Enable manual configuration of the interface to use by the agent
#                            (default "yes")
# - DAOS_IFACE_NUMA_NODE     Numa node of the interface to use by the agent (default "0")
# - DAOS_IFACE_NAME          Name of the interface to use by the agent (default "eth0")
# - DAOS_IFACE_DOMAIN_NAME   Domain name of the interface to use by the agent (default "eth0")
# - DAOS_AUTH                Enable DAOS authentication when set to "yes" (default "yes")

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

# Install DAOS agent configuration file
ARG	DAOS_ACCESS_POINTS=""
ARG	DAOS_PORT="10001"
ARG	DAOS_AUTH="yes"
ARG	DAOS_IFACE_CFG="yes"
ARG	DAOS_IFACE_NUMA_NODE="0"
ARG	DAOS_IFACE_NAME="eth0"
ARG	DAOS_IFACE_DOMAIN_NAME="eth0"
COPY	daos_agent.yml.in /tmp/daos_agent.yml.in
RUN	if [[ -z "${DAOS_ACCESS_POINTS}" ]] ; then                                                 \
		echo "[ERROR] Docker build argument DAOS_ACCESS_POINTS is not defined" ;           \
		exit 1 ;                                                                           \
	fi ;                                                                                       \
        sed --in-place --regexp-extended                                                           \
		--expression "s/@DAOS_ACCESS_POINTS@/${DAOS_ACCESS_POINTS}/"                       \
		--expression "s/@DAOS_PORT@/${DAOS_PORT}/"                                         \
		/tmp/daos_agent.yml.in &&                                                          \
	if [[ "${DAOS_AUTH}" == yes ]] ; then                                                      \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_NOAUTH_SECTION_BEGIN@$/,/^@DAOS_NOAUTH_SECTION_END@/d' \
			--expression '/(^@DAOS_AUTH_SECTION_BEGIN@$)|(^@DAOS_AUTH_SECTION_END@$)/d' \
			/tmp/daos_agent.yml.in ;                                                   \
	else                                                                                       \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_AUTH_SECTION_BEGIN@$/,/^@DAOS_AUTH_SECTION_END@/d'   \
			--expression '/(^@DAOS_NOAUTH_SECTION_BEGIN@$)|(^@DAOS_NOAUTH_SECTION_END@$)/d'\
			/tmp/daos_agent.yml.in ;                                                   \
	fi &&                                                                                      \
	if [[ "${DAOS_IFACE_CFG}" == yes ]] ; then                                                 \
		for it in DAOS_IFACE_NUMA_NODE DAOS_IFACE_NAME DAOS_IFACE_DOMAIN_NAME ; do         \
			if eval "[[ -z \"\$$it\" ]]" ; then                                        \
				echo "[ERROR] Docker build argument $it is not defined" ;          \
				exit 1 ;                                                           \
			fi ;                                                                       \
		done ;                                                                             \
		sed --in-place --regexp-extended                                                   \
			--expression '/(^@DAOS_IFACE_SECTION_BEGIN@$)|(^@DAOS_IFACE_SECTION_END@$)/d' \
			--expression "s/@DAOS_IFACE_NUMA_NODE@/${DAOS_IFACE_NUMA_NODE}/"           \
			--expression "s/@DAOS_IFACE_NAME@/${DAOS_IFACE_NAME}/"                     \
			--expression "s/@DAOS_IFACE_DOMAIN_NAME@/${DAOS_IFACE_DOMAIN_NAME}/"       \
			/tmp/daos_agent.yml.in ;                                                   \
	else                                                                                       \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_IFACE_SECTION_BEGIN@$/,/^@DAOS_IFACE_SECTION_END@/d' \
			/tmp/daos_agent.yml.in ;                                                   \
	fi &&                                                                                      \
	mv -f /tmp/daos_agent.yml.in /etc/daos/daos_agent.yml


# Install DAOS agent launcher
COPY	start-daos_agent.in /tmp/start-daos_agent.in
RUN	if [[ "${DAOS_AUTH}" == yes ]] ; then                                                      \
		dnf --assumeyes install tar xz sudo &&                                             \
		sed --regexp-extended                                                              \
			--expression '/(^@DAOS_AUTH_SECTION_BEGIN@$)|(^@DAOS_AUTH_SECTION_END@$)/d' \
			/tmp/start-daos_agent.in > /usr/local/sbin/start-daos_agent ;              \
	else                                                                                       \
		dnf --assumeyes install sudo &&                                                    \
		sed --regexp-extended                                                              \
			--expression '/^@DAOS_AUTH_SECTION_BEGIN@$/,/^@DAOS_AUTH_SECTION_END@/d'   \
			/tmp/start-daos_agent.in > /usr/local/sbin/start-daos_agent ;              \
	fi &&                                                                                      \
	chmod 755 /usr/local/sbin/start-daos_agent &&                                              \
	rm -f /tmp/start-daos_agent.in

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/local/sbin/start-daos_agent"]
CMD ["start"]
