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

# Pull base image
ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"$DAOS_DOCKER_IMAGE_NSP/daos-base-$LINUX_DISTRO:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install base DAOS package
ARG	DAOS_VERSION=""
RUN	for it in DAOS_VERSION ; do                                                                \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	echo "[INFO] Installing DAOS" ;                                                            \
	dnf install                                                                                \
		daos-client-${DAOS_VERSION} &&                                                     \
	dnf clean all

# Install minimal DAOS agent configuration file
COPY	daos_agent.yml /etc/daos/daos_agent.yml

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/bin/bash"]
CMD ["-i"]
