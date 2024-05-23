# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RHEL variant docker image of a DAOS client node
#
# This Dockerfile accept the following input build arguments:
# - LINUX_DISTRO                   Linux distribution identifier (mandatory)
# - DAOS_DOCKER_IMAGE_NSP          Namespace identifier of the base DAOS docker image (mandatory)
# - DAOS_DOCKER_IMAGE_TAG          Tag identifier of the DAOS client docker image (mandatory)
# - DAOS_VERSION                   Version of DAOS to use (mandatory)
# - DAOS_AUTH                      Enable DAOS authentication when set to "yes" (mandatory)
# - DAOS_AGENT_IFACE_CFG           Enable manual configuration of the interface to use by the agent
#                                  (mandatory)
# - DAOS_AGENT_IFACE_NUMA_NODE     Numa node of the interface to use by the agent (mandatory)
# - DAOS_AGENT_IFACE_NAME          Name of the interface to use by the agent (mandatory)
# - DAOS_AGENT_IFACE_DOMAIN_NAME   Domain name of the interface to use by the agent (mandatory)

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
	dnf install                                                                                \
		daos-client-${DAOS_VERSION} &&                                                     \
	dnf clean all &&                                                                           \
	systemctl enable daos_agent

# Install certificates
ARG	DAOS_AUTH=""
ARG	DAOS_AGENT_IFACE_CFG=""
ARG	DAOS_AGENT_IFACE_NUMA_NODE=""
ARG	DAOS_AGENT_IFACE_NAME=""
ARG	DAOS_AGENT_IFACE_DOMAIN_NAME=""
COPY	daos_agent.yml.in /tmp/daos_agent.yml.in
RUN	for it in DAOS_AUTH DAOS_AGENT_IFACE_CFG DAOS_AGENT_IFACE_NUMA_NODE DAOS_AGENT_IFACE_NAME  \
		  DAOS_AGENT_IFACE_DOMAIN_NAME ; do                                                \
		if eval "[[ -z \$$it ]]" ; then                                                    \
			echo "[ERROR] Docker build argument $it is not defined" ;                  \
			exit 1 ;                                                                   \
		fi ;                                                                               \
	done &&                                                                                    \
	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_NOAUTH_BEGIN@$/,/^@DAOS_NOAUTH_END@/d'               \
			--expression '/(^@DAOS_AUTH_BEGIN@$)|(^@DAOS_AUTH_END@$)/d'                \
			/tmp/daos_agent.yml.in &&                                                  \
		chmod 644 /root/daosCA/certs/daosCA.crt &&                                         \
		chmod 644 /root/daosCA/certs/agent.crt &&                                          \
		chmod 400 /root/daosCA/certs/agent.key &&                                          \
		chown root:root /root/daosCA/certs/daosCA.crt &&                                   \
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
