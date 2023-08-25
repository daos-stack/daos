# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS client node
#
# This Dockerfile accept the following input build arguments:
# - DAOS_BASE_IMAGE                Base docker image to use (default "daos-base")
# - DAOS_BASE_VERSION              Version of the base docker image to use (default "rocky8.6")
# - DAOS_AUTH                      Enable DAOS authentication when set to "yes" (default "yes")
# - DAOS_ADMIN_USER                Name or uid of the daos administrattor user (default "root")
# - DAOS_ADMIN_GROUP               Name or gid of the daos administrattor group (default "root")
# - DAOS_AGENT_IFACE_CFG           Enable manual configuration of the interface to use by the agent
#                                  (default "yes")
# - DAOS_AGENT_IFACE_NUMA_NODE     Numa node of the interface to use by the agent (default "0")
# - DAOS_AGENT_IFACE_NAME          Name of the interface to use by the agent (default "eth0")
# - DAOS_AGENT_IFACE_DOMAIN_NAME   Domain name of the interface to use by the agent (default "eth0")

# Pull base image
ARG	DAOS_BASE_IMAGE=daos-base
ARG	DAOS_BASE_VERSION=rocky8.6
FROM	$DAOS_BASE_IMAGE:$DAOS_BASE_VERSION
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS client package
RUN	dnf install daos-client daos-tests &&                                                      \
	dnf clean all &&                                                                           \
	systemctl enable daos_agent

# Install certificates
ARG	DAOS_AUTH=yes
ARG	DAOS_ADMIN_USER=root
ARG	DAOS_ADMIN_GROUP=root
ARG	DAOS_AGENT_IFACE_CFG="yes"
ARG	DAOS_AGENT_IFACE_NUMA_NODE="0"
ARG	DAOS_AGENT_IFACE_NAME="eth0"
ARG	DAOS_AGENT_IFACE_DOMAIN_NAME="eth0"
COPY	daos_agent.yml.in /tmp/daos_agent.yml.in
RUN	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_NOAUTH_BEGIN@$/,/^@DAOS_NOAUTH_END@/d'               \
			--expression '/(^@DAOS_AUTH_BEGIN@$)|(^@DAOS_AUTH_END@$)/d'                \
			/tmp/daos_agent.yml.in &&                                                  \
		chmod 644 /root/daosCA/certs/daosCA.crt &&                                         \
		chmod 644 /root/daosCA/certs/agent.crt &&                                          \
		chmod 400 /root/daosCA/certs/agent.key &&                                          \
		chown "$DAOS_ADMIN_USER:$DAOS_ADMIN_GROUP" /root/daosCA/certs/daosCA.crt &&        \
		chown daos_agent:daos_agent /root/daosCA/certs/agent.crt &&                        \
		chown daos_agent:daos_agent /root/daosCA/certs/agent.key &&                        \
		mv /root/daosCA/certs/daosCA.crt /etc/daos/certs/. &&                              \
		mv /root/daosCA/certs/agent.crt /etc/daos/certs/. &&                               \
		mv /root/daosCA/certs/agent.key /etc/daos/certs/. &&                               \
		rm -fr /root/daosCA ;                                                              \
	else                                                                                       \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_AUTH_BEGIN@$/,/^@DAOS_AUTH_END@/d'                   \
			--expression '/(^@DAOS_NOAUTH_BEGIN@$)|(^@DAOS_NOAUTH_END@$)/d'            \
			/tmp/daos_agent.yml.in ;                                                   \
	fi &&                                                                                      \
	if [[ "${DAOS_AGENT_IFACE_CFG}" == yes ]] ; then                                           \
		args=(                                                                             \
			DAOS_AGENT_IFACE_NUMA_NODE                                                 \
			DAOS_AGENT_IFACE_NAME                                                      \
			DAOS_AGENT_IFACE_DOMAIN_NAME                                               \
		) ;                                                                                \
		for it in $args; do                                                                \
			if eval "[[ -z \"\$$it\" ]]" ; then                                        \
				echo "[ERROR] Docker build argument $it is not defined" ;          \
				exit 1 ;                                                           \
			fi ;                                                                       \
		done ;                                                                             \
		sed --in-place --regexp-extended                                                   \
			--expression '/(^@DAOS_IFACE_BEGIN@$)|(^@DAOS_IFACE_END@$)/d'              \
			--expression "s/@DAOS_IFACE_NUMA_NODE@/${DAOS_AGENT_IFACE_NUMA_NODE}/"     \
			--expression "s/@DAOS_IFACE_NAME@/${DAOS_AGENT_IFACE_NAME}/"               \
			--expression "s/@DAOS_IFACE_DOMAIN_NAME@/${DAOS_AGENT_IFACE_DOMAIN_NAME}/" \
			/tmp/daos_agent.yml.in ;                                                   \
	else                                                                                       \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_IFACE_BEGIN@$/,/^@DAOS_IFACE_END@/d'                 \
			/tmp/daos_agent.yml.in ;                                                   \
	fi &&                                                                                      \
	mv -f /tmp/daos_agent.yml.in /etc/daos/daos_agent.yml
