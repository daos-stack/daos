# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL SPDK setup script wrapper
#
# This Dockerfile accept the following input build arguments:
# - LINUX_DISTRO             Linux distribution identifier (default "el8")
# - DAOS_DOCKER_IMAGE_NSP    Namespace identifier of the base DAOS docker image (default "daos")
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS base docker image (default "v2.4.0")
# - DAOS_VERSION             Version of DAOS to use (default "2.4.0-2.el8")

# Pull base image
ARG	LINUX_DISTRO="el8"
ARG	DAOS_DOCKER_IMAGE_NSP="daos"
ARG	DAOS_DOCKER_IMAGE_TAG="v2.4.0"
FROM	"$DAOS_DOCKER_IMAGE_NSP/daos-base-$LINUX_DISTRO:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS package
ARG	DAOS_VERSION="2.4.0-2.el8"
RUN	echo "[INFO] Installing SPDK tools dependencies" ;                                         \
	dnf install                                                                                \
		kmod &&                                                                            \
	echo "[INFO] Installing SPDK tools" ;                                                      \
	dnf install                                                                                \
		spdk-tools &&                                                                      \
	dnf clean all

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/share/spdk/scripts/setup.sh"]
CMD ["status"]
