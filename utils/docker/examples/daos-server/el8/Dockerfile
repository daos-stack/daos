# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS server docker image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS base docker image (default "rocky8.6")
# - DAOS_VERSION             Version of DAOS to use (default "2.4.0-2.el8")

# Pull base image
ARG	DAOS_DOCKER_IMAGE_TAG=rocky8.6
FROM	daos-base:$DAOS_DOCKER_IMAGE_TAG
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS package
ARG	DAOS_VERSION="2.4.0-2.el8"
RUN	echo "[INFO] Installing DAOS" ;                                                            \
	dnf install                                                                                \
		daos-server-${DAOS_VERSION} &&                                                     \
	dnf clean all

# Configuration of the server
COPY    daos_server.yml /etc/daos/daos_server.yml

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
