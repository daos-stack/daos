# Copyright 2021-2023 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL DAOS client docker image
#
# This Dockerfile accept the following input build arguments:
# - DAOS_DOCKER_IMAGE_TAG    Tag identifier of the DAOS client docker image (default "rocky8.6")
# - DAOS_ACCESS_POINTS       Hostname list of the DAOS access points (mandatory)
# - DAOS_PORT                DAOS access point port number to connect (default "1001")
# - DAOS_IFACE_CFG           Enable manual configuration of the interface to use by the agent
#                            (default "yes")
# - DAOS_IFACE_NUMA_NODE     Numa node of the interface to use by the agent (default "0")
# - DAOS_IFACE_NAME          Name of the interface to use by the agent (default "eth0")
# - DAOS_IFACE_DOMAIN_NAME   Domain name of the interface to use by the agent (default "eth0")
# - DAOS_AUTH                Enable DAOS authentication when set to "yes" (default "yes")

# Pull base image
ARG	DAOS_DOCKER_IMAGE_TAG=rocky8.6
FROM	daos-base:$DAOS_DOCKER_IMAGE_TAG
LABEL	maintainer="daos@daos.groups.io"

# Install DAOS agent configuration file
ARG	DAOS_ACCESS_POINTS=""
ARG	DAOS_PORT="10001"
ARG	DAOS_AUTH="yes"
ARG	DAOS_IFACE_CFG="yes"
ARG	DAOS_IFACE_NUMA_NODE="0"
ARG	DAOS_IFACE_NAME="eth0"
ARG	DAOS_IFACE_DOMAIN_NAME="eth0"
COPY	daos_agent.yml.in /tmp/daos_agent.yml.in
RUN	if [[ -z "${DAOS_ACCESS_POINTS}" ]] ; then                                                 \
		echo "[ERROR] Docker build argument DAOS_ACCESS_POINTS is not defined" ;           \
		exit 1 ;                                                                           \
	fi ;                                                                                       \
        sed --in-place --regexp-extended                                                           \
		--expression "s/@DAOS_ACCESS_POINTS@/${DAOS_ACCESS_POINTS}/"                       \
		--expression "s/@DAOS_PORT@/${DAOS_PORT}/"                                         \
		/tmp/daos_agent.yml.in &&                                                          \
	if [[ "${DAOS_AUTH}" == yes ]] ; then                                                      \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_NOAUTH_SECTION_BEGIN@$/,/^@DAOS_NOAUTH_SECTION_END@/d' \
			--expression '/(^@DAOS_AUTH_SECTION_BEGIN@$)|(^@DAOS_AUTH_SECTION_END@$)/d' \
			/tmp/daos_agent.yml.in ;                                                   \
	else                                                                                       \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_AUTH_SECTION_BEGIN@$/,/^@DAOS_AUTH_SECTION_END@/d'   \
			--expression '/(^@DAOS_NOAUTH_SECTION_BEGIN@$)|(^@DAOS_NOAUTH_SECTION_END@$)/d'\
			/tmp/daos_agent.yml.in ;                                                   \
	fi &&                                                                                      \
	if [[ "${DAOS_IFACE_CFG}" == yes ]] ; then                                                 \
		for it in DAOS_IFACE_NUMA_NODE DAOS_IFACE_NAME DAOS_IFACE_DOMAIN_NAME ; do         \
			if eval "[[ -z \"\$$it\" ]]" ; then                                        \
				echo "[ERROR] Docker build argument $it is not defined" ;          \
				exit 1 ;                                                           \
			fi ;                                                                       \
		done ;                                                                             \
		sed --in-place --regexp-extended                                                   \
			--expression '/(^@DAOS_IFACE_SECTION_BEGIN@$)|(^@DAOS_IFACE_SECTION_END@$)/d' \
			--expression "s/@DAOS_IFACE_NUMA_NODE@/${DAOS_IFACE_NUMA_NODE}/"           \
			--expression "s/@DAOS_IFACE_NAME@/${DAOS_IFACE_NAME}/"                     \
			--expression "s/@DAOS_IFACE_DOMAIN_NAME@/${DAOS_IFACE_DOMAIN_NAME}/"       \
			/tmp/daos_agent.yml.in ;                                                   \
	else                                                                                       \
		sed --in-place --regexp-extended                                                   \
			--expression '/^@DAOS_IFACE_SECTION_BEGIN@$/,/^@DAOS_IFACE_SECTION_END@/d' \
			/tmp/daos_agent.yml.in ;                                                   \
	fi &&                                                                                      \
	mv -f /tmp/daos_agent.yml.in /etc/daos/daos_agent.yml


# Install DAOS agent launcher
COPY	run-daos_agent.in /tmp/run-daos_agent.in
RUN	if [[ "${DAOS_AUTH}" == yes ]] ; then                                                      \
		dnf --assumeyes install tar xz sudo &&                                             \
		sed --regexp-extended                                                              \
			--expression '/(^@DAOS_AUTH_SECTION_BEGIN@$)|(^@DAOS_AUTH_SECTION_END@$)/d' \
			/tmp/run-daos_agent.in > /usr/local/sbin/run-daos_agent ;              \
	else                                                                                       \
		dnf --assumeyes install sudo &&                                                    \
		sed --regexp-extended                                                              \
			--expression '/^@DAOS_AUTH_SECTION_BEGIN@$/,/^@DAOS_AUTH_SECTION_END@/d'   \
			/tmp/run-daos_agent.in > /usr/local/sbin/run-daos_agent ;              \
	fi &&                                                                                      \
	chmod 755 /usr/local/sbin/run-daos_agent &&                                              \
	rm -f /tmp/run-daos_agent.in

# Define entrypoint and cmd:
# - ENTRYPOINT for the command to run
# - CMD for the default arguments
ENTRYPOINT ["/usr/local/sbin/run-daos_agent"]
CMD ["start"]
