# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS server node
#
# This Dockerfile accept the following input build arguments:
# - LINUX_DISTRO             Linux distribution identifier (mandatory)
# - DAOS_DOCKER_IMAGE_NSP    Namespace identifier of the base DAOS docker image (mandatory)
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (mandatory)
# - DAOS_VERSION             Version of DAOS to use (mandatory)
# - DAOS_AUTH                Enable DAOS authentication when set to "yes" (mandatory)
# - DAOS_HUGEPAGES_NBR       Number of huge pages to allocate for SPDK (mandatory)
# - DAOS_IFACE_NAME          Fabric network interface used by the DAOS engine (mandatory)
# - DAOS_SCM_SIZE            Size in GB of the RAM emulating SCM devices (mandatory)
# - DAOS_BDEV_SIZE           Size in GB of the file created to emulate NVMe devices (mandatory)
# - DAOS_MD_ON_SSD           Enable DAOS MD-on-SSD feature when set to "yes" (mandatory)

# Pull base image
ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"${DAOS_DOCKER_IMAGE_NSP}/daos-base-${LINUX_DISTRO}:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS server package
ARG	DAOS_VERSION=""
RUN	for it in DAOS_VERSION ; do                                                                \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	dnf install daos-server-${DAOS_VERSION} &&                                                 \
	dnf clean all &&                                                                           \
	systemctl enable daos_server

# Configuration of the server
COPY	daos_server.yml.in /tmp/daos_server.yml.in
ARG	DAOS_AUTH=""
ARG	DAOS_HUGEPAGES_NBR=""
ARG	DAOS_IFACE_NAME=""
ARG	DAOS_SCM_SIZE=""
ARG	DAOS_BDEV_SIZE=""
ARG	DAOS_MD_ON_SSD=""
RUN	for it in DAOS_AUTH DAOS_HUGEPAGES_NBR DAOS_IFACE_NAME DAOS_SCM_SIZE DAOS_BDEV_SIZE        \
                  DAOS_MD_ON_SSD ; do                                                              \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
# XXX NOTE XXX HugePages and vfio seem to not be well managed with docker and latest version of the
# XXX NOTE XXX kernel.  More details could be found at:
# XXX NOTE XXX - https://stackoverflow.com/questions/64707324/dpdk-sample-application-aborts-after-eal-couldnt-get-fd-on-hugepage-file
# XXX NOTE XXX - https://doc.dpdk.org/guides/linux_gsg/enable_func.html
	sed --regexp-extended --in-place                                                           \
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
