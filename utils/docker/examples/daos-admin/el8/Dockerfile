# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS admin image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (default "rocky8.6")
# - DAOS_VERSION             Version of DAOS to use (default "2.4.0-2.el8")

# Pull base image
ARG	DAOS_DOCKER_IMAGE_TAG=rocky8.6
FROM	daos-base:$DAOS_DOCKER_IMAGE_TAG
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS package
ARG	DAOS_VERSION="2.4.0-2.el8"
RUN	echo "[INFO] Installing DAOS" ;                                                            \
	dnf install                                                                                \
		daos-admin-${DAOS_VERSION} &&                                                      \
	dnf clean all

# Install DAOS Bash launcher
COPY	daos-bash /usr/local/sbin/daos-bash
RUN	echo "[INSTALL] Installing DAOS Agent entry point" ;                                       \
	chmod 755 /usr/local/sbin/daos-bash

# Configuration of DMG
COPY    daos_control.yml /etc/daos/daos_control.yml

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/local/sbin/daos-bash"]
CMD ["-i"]
