# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS administrator node
#
#
# - LINUX_DISTRO             Linux distribution identifier (default "el8")
# - DAOS_DOCKER_IMAGE_NSP    Namespace identifier of the base DAOS docker image (default "daos")
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS base docker image (default "v2.4.1")
# - DAOS_VERSION             Version of DAOS to use (default "2.4.1-2.el8")
# - DAOS_AUTH                Enable DAOS authentication when set to "yes" (default "yes")

# Pull base image
ARG	LINUX_DISTRO="el8"
ARG	DAOS_DOCKER_IMAGE_NSP="daos"
ARG	DAOS_DOCKER_IMAGE_TAG="v2.4.1"
FROM	"${DAOS_DOCKER_IMAGE_NSP}/daos-base-${LINUX_DISTRO}:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS client package
ARG	DAOS_VERSION="2.4.1-2.el8"
RUN	dnf install daos-admin-${DAOS_VERSION} &&                                                  \
	dnf clean all

# Install certificates
ARG	DAOS_AUTH=yes
# FIXME Should be provided through volumes (or Secrets for K8S)
# XXX NOTE XXX With a production platform, this configuration file should be provided with a volume
# XXX NOTE XXX (or ConfigMaps for K8S).
COPY	daos_control.yml.in /tmp/daos_control.yml.in
RUN	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed --regexp-extended                                                              \
		    --expression '/^@DAOS_NOAUTH_BEGIN@$/,/^@DAOS_NOAUTH_END@/d'                   \
		    --expression '/(^@DAOS_AUTH_BEGIN@$)|(^@DAOS_AUTH_END@$)/d'                    \
		    /tmp/daos_control.yml.in > /etc/daos/daos_control.yml &&                       \
# XXX WARNING XXX With a production platform, these certificates should be provided with a volume
# XXX WARNING XXX (or Secrets with K8S).
		chmod 644 /root/daosCA/certs/daosCA.crt &&                                         \
		chmod 644 /root/daosCA/certs/admin.crt &&                                          \
		chmod 400 /root/daosCA/certs/admin.key &&                                          \
		chown root:root /root/daosCA/certs/daosCA.crt &&                                   \
		chown root:root /root/daosCA/certs/admin.crt &&                                    \
		chown root:root /root/daosCA/certs/admin.key &&                                    \
		mv /root/daosCA/certs/daosCA.crt /etc/daos/certs/. &&                              \
		mv /root/daosCA/certs/admin.crt /etc/daos/certs/. &&                               \
		mv /root/daosCA/certs/admin.key /etc/daos/certs/. &&                               \
		rm -fr /root/daosCA ;                                                              \
	else                                                                                       \
		sed --regexp-extended                                                              \
		    --expression '/^@DAOS_AUTH_BEGIN@$/,/^@DAOS_AUTH_END@/d'       \
		    --expression '/(^@DAOS_NOAUTH_BEGIN@$)|(^@DAOS_NOAUTH_END@$)/d'\
		    /tmp/daos_control.yml.in > /etc/daos/daos_control.yml ;                        \
	fi &&                                                                                      \
	rm -f /tmp/daos_control.yml.in
