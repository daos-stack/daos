# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS administrator node
#
# - LINUX_DISTRO             Linux distribution identifier (mandatory)
# - DAOS_DOCKER_IMAGE_NSP    Namespace identifier of the base DAOS docker image (mandatory)
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (mandatory)
# - DAOS_VERSION             Version of DAOS to use (mandatory)
# - DAOS_AUTH                Enable DAOS authentication when set to "yes" (mandatory)

# Pull base image
ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"${DAOS_DOCKER_IMAGE_NSP}/daos-base-${LINUX_DISTRO}:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS client package
ARG	DAOS_VERSION=""
RUN	for it in DAOS_VERSION ; do                                                                \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	dnf install daos-admin-${DAOS_VERSION} &&                                                  \
	dnf clean all

# Install certificates
COPY	daos_control.yml.in /tmp/daos_control.yml.in
ARG	DAOS_AUTH=""
RUN	for it in DAOS_AUTH ; do                                                                   \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed --regexp-extended                                                              \
		    --expression '/^@DAOS_NOAUTH_BEGIN@$/,/^@DAOS_NOAUTH_END@/d'                   \
		    --expression '/(^@DAOS_AUTH_BEGIN@$)|(^@DAOS_AUTH_END@$)/d'                    \
		    /tmp/daos_control.yml.in > /etc/daos/daos_control.yml &&                       \
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
