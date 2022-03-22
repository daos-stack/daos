# Copyright 2021-2022 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS administrator node
#
# - DAOS_BASE_IMAGE      Base docker image to use (default "daos-base")
# - DAOS_BASE_VERSION    Version of the base docker image to use (default "rocky8.4")
# - DAOS_AUTH            Enable DAOS authentication when set to "yes" (default "yes")
# - DAOS_ADMIN_USER      Name or uid of the daos administrattor user (default "root")
# - DAOS_ADMIN_GROUP     Name or gid of the daos administrattor group (default "root")

# Pull base image
ARG	DAOS_BASE_IMAGE=daos-base
ARG	DAOS_BASE_VERSION=rocky8.4
FROM	$DAOS_BASE_IMAGE:$DAOS_BASE_VERSION
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS client package
RUN	dnf install daos-client &&                                                                 \
	dnf clean all

# Install certificates
ARG	DAOS_AUTH=yes
ARG	DAOS_ADMIN_USER=root
ARG	DAOS_ADMIN_GROUP=root
# FIXME Should be provided through volumes (or Secrets for K8S)
# XXX NOTE XXX With a production platform, this configuration file should be provided with a volume
# XXX NOTE XXX (or ConfigMaps for K8S).
COPY	daos_control.yml.in /tmp/daos_control.yml.in
RUN	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed --regexp-extended                                                              \
		    --expression '/^@DAOS_NOAUTH_SECTION_BEGIN@$/,/^@DAOS_NOAUTH_SECTION_END@/d'   \
		    --expression '/(^@DAOS_AUTH_SECTION_BEGIN@$)|(^@DAOS_AUTH_SECTION_END@$)/d'    \
		    /tmp/daos_control.yml.in > /etc/daos/daos_control.yml &&                       \
# XXX WARNING XXX With a production platform, these certificates should be provided with a volume
# XXX WARNING XXX (or Secrets with K8S).
		chmod 644 /root/daosCA/certs/daosCA.crt &&                                         \
		chmod 644 /root/daosCA/certs/admin.crt &&                                          \
		chmod 600 /root/daosCA/certs/admin.key &&                                          \
		chown "$DAOS_ADMIN_USER:$DAOS_ADMIN_GROUP" /root/daosCA/certs/daosCA.crt &&        \
		chown "$DAOS_ADMIN_USER:$DAOS_ADMIN_GROUP" /root/daosCA/certs/admin.crt &&         \
		chown "$DAOS_ADMIN_USER:$DAOS_ADMIN_GROUP" /root/daosCA/certs/admin.key &&         \
		mv /root/daosCA/certs/daosCA.crt /etc/daos/certs/. &&                              \
		mv /root/daosCA/certs/admin.crt /etc/daos/certs/. &&                               \
		mv /root/daosCA/certs/admin.key /etc/daos/certs/. &&                               \
		rm -fr /root/daosCA ;                                                              \
	else                                                                                       \
		sed --regexp-extended                                                              \
		    --expression '/^@DAOS_AUTH_SECTION_BEGIN@$/,/^@DAOS_AUTH_SECTION_END@/d'       \
		    --expression '/(^@DAOS_NOAUTH_SECTION_BEGIN@$)|(^@DAOS_NOAUTH_SECTION_END@$)/d'\
		    /tmp/daos_control.yml.in > /etc/daos/daos_control.yml ;                        \
	fi &&                                                                                      \
	rm -f /tmp/daos_control.yml.in
