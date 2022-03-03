# Copyright 2021-2022 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS server node
#
# This Dockerfile accept the following input build arguments:
# - DAOS_BASE_IMAGE      Base docker image to use (default "daos-base")
# - DAOS_BASE_VERSION    Version of the base docker image to use (default "rocky8.4")
# - DAOS_AUTH            Enable DAOS authentication when set to "yes" (default "yes")
# - DAOS_HUGEPAGES_NBR   Number of huge pages to allocate for SPDK (default 4096)
# - DAOS_IFACE_NAME      Fabric network interface used by the DAOS engine (default "eth0")
# - DAOS_SCM_SIZE        Size in GB of the RAM emulating SCM devices (default 4)
# - DAOS_BDEV_SIZE       Size in GB of the file created to emulate NVMe devices (derault 16)

# Pull base image
ARG	DAOS_BASE_IMAGE=daos-base
ARG	DAOS_BASE_VERSION=rocky8.4
FROM	$DAOS_BASE_IMAGE:$DAOS_BASE_VERSION
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS server package
RUN	dnf install daos-server &&                                                                 \
	dnf clean all &&                                                                           \
	systemctl enable daos_server

# Configuration of the server
ARG	DAOS_AUTH=yes
ARG	DAOS_HUGEPAGES_NBR=4096
ARG	DAOS_IFACE_NAME=eth0
ARG	DAOS_SCM_SIZE=4
ARG	DAOS_BDEV_SIZE=16
# XXX NOTE XXX With a production platform, this configuration file should be provided with a volume
# XXX NOTE XXX (or ConfigMaps for K8S).
COPY	daos_server.yml.in /tmp/daos_server.yml.in
RUN	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed --regexp-extended                                                              \
		    --expression "s/@DAOS_HUGEPAGES_NBR@/${DAOS_HUGEPAGES_NBR}/"                   \
		    --expression "s/@DAOS_IFACE_NAME@/${DAOS_IFACE_NAME}/"                         \
		    --expression "s/@DAOS_SCM_SIZE@/${DAOS_SCM_SIZE}/"                             \
		    --expression "s/@DAOS_BDEV_SIZE@/${DAOS_BDEV_SIZE}/"                           \
		    --expression '/^@DAOS_NOAUTH_SECTION_BEGIN@$/,/^@DAOS_NOAUTH_SECTION_END@/d'   \
		    --expression '/(^@DAOS_AUTH_SECTION_BEGIN@$)|(^@DAOS_AUTH_SECTION_END@$)/d'    \
		    /tmp/daos_server.yml.in > /etc/daos/daos_server.yml &&                         \
# XXX WARNING XXX With a production platform, these certificates should be provided with a volume
# XXX WARNING XXX (or Secrets with K8S).
		chmod 644 /root/daosCA/certs/daosCA.crt &&                                         \
		chmod 644 /root/daosCA/certs/server.crt &&                                         \
		chmod 600 /root/daosCA/certs/server.key &&                                         \
		chmod 644 /root/daosCA/certs/agent.crt &&                                          \
		chown daos_server:daos_server /root/daosCA/certs/daosCA.crt &&                     \
		chown daos_server:daos_server /root/daosCA/certs/server.crt &&                     \
		chown daos_server:daos_server /root/daosCA/certs/server.key &&                     \
		chown daos_server:daos_server /root/daosCA/certs/agent.crt &&                      \
		mv /root/daosCA/certs/daosCA.crt /etc/daos/certs/. &&                              \
		mv /root/daosCA/certs/server.crt /etc/daos/certs/. &&                              \
		mv /root/daosCA/certs/server.key /etc/daos/certs/. &&                              \
		mv /root/daosCA/certs/agent.crt /etc/daos/certs/clients/. &&                       \
		rm -fr /root/daosCA ;                                                              \
	else                                                                                       \
		sed --regexp-extended                                                              \
		    --expression "s/@DAOS_HUGEPAGES_NBR@/${DAOS_HUGEPAGES_NBR}/"                   \
		    --expression "s/@DAOS_IFACE_NAME@/${DAOS_IFACE_NAME}/"                         \
		    --expression "s/@DAOS_SCM_SIZE@/${DAOS_SCM_SIZE}/"                             \
		    --expression "s/@DAOS_BDEV_SIZE@/${DAOS_BDEV_SIZE}/"                           \
		    --expression '/^@DAOS_AUTH_SECTION_BEGIN@$/,/^@DAOS_AUTH_SECTION_END@/d'       \
		    --expression '/(^@DAOS_NOAUTH_SECTION_BEGIN@$)|(^@DAOS_NOAUTH_SECTION_END@$)/d'\
		    /tmp/daos_server.yml.in > /etc/daos/daos_server.yml ;                          \
	fi &&                                                                                      \
	rm -f /tmp/daos_server.yml.in
