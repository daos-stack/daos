# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS server node
#
# This Dockerfile accept the following input build arguments:
# - DAOS_BASE_IMAGE      Base docker image to use (default "daos-base")
# - DAOS_BASE_VERSION    Version of the base docker image to use (default "rocky8.6")
# - DAOS_AUTH            Enable DAOS authentication when set to "yes" (default "yes")
# - DAOS_HUGEPAGES_NBR   Number of huge pages to allocate for SPDK (default 4096)
# - DAOS_IFACE_NAME      Fabric network interface used by the DAOS engine (default "eth0")
# - DAOS_SCM_SIZE        Size in GB of the RAM emulating SCM devices (default 4)
# - DAOS_BDEV_SIZE       Size in GB of the file created to emulate NVMe devices (derault 16)
# - DAOS_MD_ON_SSD       Enable DAOS MD-on-SSD feature when set to "yes" (default "no")

# Pull base image
ARG	DAOS_BASE_IMAGE=daos-base
ARG	DAOS_BASE_VERSION=rocky8.6
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
ARG	DAOS_MD_ON_SSD=no
# XXX NOTE XXX With a production platform, this configuration file should be provided with a volume
# XXX NOTE XXX (or ConfigMaps for K8S).
COPY	daos_server.yml.in /tmp/daos_server.yml.in
# XXX NOTE XXX HugePages and vfio seem to not be well managed with docker and latest version of the
# XXX NOTE XXX kernel.  More details could be found at:
# XXX NOTE XXX - https://stackoverflow.com/questions/64707324/dpdk-sample-application-aborts-after-eal-couldnt-get-fd-on-hugepage-file
# XXX NOTE XXX - https://doc.dpdk.org/guides/linux_gsg/enable_func.html
RUN	sed --regexp-extended --in-place                                                           \
	    --expression "s/^User=daos_server$/User=root/"                                         \
	    --expression "s/^Group=daos_server$/Group=root/"                                       \
	    /usr/lib/systemd/system/daos_server.service &&                                         \
	sed_opts="--regexp-extended" ;                                                             \
	sed_opts+=" --expression s/@DAOS_HUGEPAGES_NBR@/${DAOS_HUGEPAGES_NBR}/" ;                  \
	sed_opts+=" --expression s/@DAOS_IFACE_NAME@/${DAOS_IFACE_NAME}/" ;                        \
	sed_opts+=" --expression s/@DAOS_SCM_SIZE@/${DAOS_SCM_SIZE}/" ;                            \
	sed_opts+=" --expression s/@DAOS_BDEV_SIZE@/${DAOS_BDEV_SIZE}/" ;                          \
	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed_opts+=' --expression /^@DAOS_NOAUTH_BEGIN@$/,/^@DAOS_NOAUTH_END@/d' ;          \
		sed_opts+=' --expression /(^@DAOS_AUTH_BEGIN@$)|(^@DAOS_AUTH_END@$)/d' ;           \
	else                                                                                       \
		sed_opts+=' --expression /^@DAOS_AUTH_BEGIN@$/,/^@DAOS_AUTH_END@/d' ;              \
		sed_opts+=' --expression /(^@DAOS_NOAUTH_BEGIN@$)|(^@DAOS_NOAUTH_END@$)/d' ;       \
	fi ;                                                                                       \
	if [ "$DAOS_MD_ON_SSD" == yes ] ; then                                                     \
		sed_opts+=' --expression /(^@DAOS_MD_ON_SSD_BEGIN@$)|(^@DAOS_MD_ON_SSD_END@$)/d' ; \
	else                                                                                       \
		sed_opts+=' --expression /^@DAOS_MD_ON_SSD_BEGIN@$/,/^@DAOS_MD_ON_SSD_END@/d' ;    \
	fi ;                                                                                       \
	sed $sed_opts /tmp/daos_server.yml.in > /etc/daos/daos_server.yml &&                       \
	rm -f /tmp/daos_server.yml.in &&                                                           \
	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
# XXX WARNING XXX With a production platform, these certificates should be provided with a volume
# XXX WARNING XXX (or Secrets with K8S).
		chmod 644 /root/daosCA/certs/daosCA.crt &&                                         \
		chmod 644 /root/daosCA/certs/server.crt &&                                         \
		chmod 400 /root/daosCA/certs/server.key &&                                         \
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
	fi
