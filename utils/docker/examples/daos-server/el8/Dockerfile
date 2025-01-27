# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS server docker image
#
# This Dockerfile accept the following input build arguments:
# - LINUX_DISTRO             Linux distribution identifier (mandatory)
# - DAOS_DOCKER_IMAGE_NSP    Namespace identifier of the base DAOS docker image (mandatory)
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (mandatory)
# - DAOS_VERSION             Version of DAOS to use (mandatory)

# Pull base image
ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"$DAOS_DOCKER_IMAGE_NSP/daos-base-$LINUX_DISTRO:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS package
ARG	DAOS_VERSION=""
RUN	for it in DAOS_VERSION ; do                                                                \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	echo "[INFO] Installing DAOS containerization dependencies" ;                              \
	dnf install                                                                                \
		kmod                                                                               \
		sudo                                                                               \
		xz &&                                                                              \
	echo "[INFO] Installing DAOS" ;                                                            \
	dnf install                                                                                \
		daos-server-${DAOS_VERSION} &&                                                     \
	dnf clean all

# Configuration of the server
COPY    daos_server.yml /etc/daos/daos_server.yml

# Install DAOS server launcher
COPY	run-daos_server.sh /usr/local/sbin/run-daos_server
RUN	chmod 755 /usr/local/sbin/run-daos_server

# Copy missing configuration file
COPY 50_daos_limits.conf /etc/security/limits.d/50_daos_limits.conf

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/local/sbin/run-daos_server"]
CMD ["start"]
