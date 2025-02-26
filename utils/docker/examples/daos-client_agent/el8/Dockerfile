# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - LINUX_DISTRO             Linux distribution identifier (mandatory)
# - DAOS_DOCKER_IMAGE_NSP    Namespace identifier of the base DAOS docker image (mandatory)
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (mandatory)
# - DAOS_VERSION             Version of DAOS to use (mandatory)
# - DAOS_CLIENT_UNAME        User name of the client (mandatory)
# - DAOS_CLIENT_GNAME        Group name of the client (mandatory)

# Pull base image
ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"$DAOS_DOCKER_IMAGE_NSP/daos-agent-$LINUX_DISTRO:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install base DAOS package
ARG	DAOS_VERSION=""
RUN	for it in DAOS_VERSION ; do                                                                \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	echo "[INFO] Installing DAOS containerization dependencies" ;                              \
	dnf install                                                                                \
		sudo                                                                               \
		xz &&                                                                              \
	dnf clean all

# Install DAOS Bash launcher
ARG	DAOS_CLIENT_UNAME=""
ARG	DAOS_CLIENT_GNAME=""
COPY	daos-bash.sh /tmp/daos-bash.in
RUN	echo "[INSTALL] Installing DAOS Agent entry point" ;                                       \
	for it in DAOS_CLIENT_UNAME DAOS_CLIENT_GNAME ; do                                         \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	sed --regexp-extended                                                                      \
		--expression "s/@DAOS_CLIENT_UNAME@/${DAOS_CLIENT_UNAME}/"                         \
		--expression "s/@DAOS_CLIENT_GNAME@/${DAOS_CLIENT_GNAME}/"                         \
		/tmp/daos-bash.in > /usr/local/sbin/daos-bash &&                                   \
	chmod 755 /usr/local/sbin/daos-bash &&                                                     \
	rm -f /tmp/daos-bash.in

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/local/sbin/daos-bash"]
CMD ["-i"]
