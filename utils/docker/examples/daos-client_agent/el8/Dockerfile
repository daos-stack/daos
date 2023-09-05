# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (default "rocky8.6")
# - DAOS_VERSION             Version of DAOS to use (default "2.4.0-2.el8")
# - DAOS_CLIENT_UNAME        User name of the client (default "root")
# - DAOS_CLIENT_GNAME        Group name of the client (default "root")

# Pull base image
ARG	DAOS_DOCKER_IMAGE_TAG=rocky8.6
FROM	daos-agent:$DAOS_DOCKER_IMAGE_TAG
LABEL	maintainer="daos@daos.groups.io"

# Install base DAOS package
ARG	DAOS_VERSION="2.4.0-2.el8"
RUN	echo "[INFO] Installing DAOS" ;                                                            \
	dnf install                                                                                \
		daos-client-tests-${DAOS_VERSION} &&                                               \
	dnf clean all

# Install DAOS Bash launcher
ARG	DAOS_CLIENT_UNAME="root"
ARG	DAOS_CLIENT_GNAME="root"
COPY	daos-bash.in /tmp/daos-bash.in
RUN	echo "[INSTALL] Installing DAOS Agent entry point" ;                                       \
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
