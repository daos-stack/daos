# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (default "rocky8.6")

# Pull base image
ARG	DAOS_DOCKER_IMAGE_TAG=rocky8.6
FROM	daos-base:$DAOS_DOCKER_IMAGE_TAG
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS agent configuration file
COPY	daos_agent.yml /etc/daos/daos_agent.yml

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/bin/bash"]
CMD ["-i"]
