# Copyright 2021-2022 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a base RHEL variant docker image of a DAOS node
#
# This Dockerfile accept the following input build arguments:
# - RHEL_BASE_IMAGE     Base docker image to use (default "rockylinux/rockylinux")
# - RHEL_BASE_VERSION   Version of the base docker image to use (default "8.4")
# - BUST_CACHE          Manage docker building cache (default undefined).  To invalidate the cache,
#                       a random value such as the date of day shall be given.
# - DAOS_AUTH           Enable DAOS authentication when set to "yes" (default "yes")
# - DAOS_REPOS          Space separated list of repos needed to install DAOS (default
#                       "https://packages.daos.io/v2.0/CentOS8/packages/x86_64/")
# - DAOS_GPG_KEYS       Space separated list of GPG keys associated with DAOS repos (default
#                       "https://packages.daos.io/RPM-GPG-KEY")
# - DAOS_REPOS_NOAUTH   Space separated list of repos to use without GPG authentication
#                       (default "")

# Pull base image
ARG	RHEL_BASE_IMAGE=rockylinux/rockylinux
ARG	RHEL_BASE_VERSION=8.4
FROM	$RHEL_BASE_IMAGE:$RHEL_BASE_VERSION
LABEL	maintainer="daos@daos.groups.io"

# Configure systemd: more details could be found at following URL:
# https://markandruth.co.uk/2020/10/10/running-systemd-inside-a-centos-8-docker-container
# XXX FIXME XXX Should be removed in production with application dedicated entry point
VOLUME	[ "/sys/fs/cgroup" ]
RUN	systemctl mask systemd-remount-fs.service graphical.target kdump.service                   \
	               systemd-logind.service dev-hugepages.mount &&                               \
	pushd /lib/systemd/system/sysinit.target.wants &&                                          \
	for item in * ; do                                                                         \
		[ "$item" == systemd-tmpfiles-setup.service ] || rm -f "$item" ;                   \
	done &&                                                                                    \
	popd &&                                                                                    \
	rm -f /lib/systemd/system/multi-user.target.wants/* &&                                     \
	rm -f /etc/systemd/system/*.wants/* &&                                                     \
	rm -f /lib/systemd/system/local-fs.target.wants/* &&                                       \
	rm -f /lib/systemd/system/sockets.target.wants/*udev* &&                                   \
	rm -f /lib/systemd/system/sockets.target.wants/*initctl* &&                                \
	rm -f /lib/systemd/system/basic.target.wants/* &&                                          \
	rm -f /lib/systemd/system/anaconda.target.wants/*
STOPSIGNAL SIGRTMIN+3
ENTRYPOINT [ "/sbin/init" ]

# Base configuration of dnf and system update
RUN	dnf clean all &&                                                                           \
	dnf makecache &&                                                                           \
	dnf --assumeyes install dnf-plugins-core &&                                                \
	dnf config-manager --save --setopt=assumeyes=True &&                                       \
	dnf config-manager --save --setopt=fastestmirror=True &&                                   \
	dnf config-manager --set-enabled powertools &&                                             \
	dnf install epel-release &&                                                                \
	dnf update &&                                                                              \
	dnf clean all

# Install DAOS package
# XXX NOTE XXX Variable allowing to build the image without using global --no-cache option and thus
# XXX NOTE XXX to not update all rpms.  To work properly a random value such as the date of the day
# XXX NOTE XXX should be given.
ARG	BUST_CACHE
ARG	DAOS_REPOS="https://packages.daos.io/v2.0/CentOS8/packages/x86_64/"
ARG	DAOS_GPG_KEYS="https://packages.daos.io/RPM-GPG-KEY"
ARG	DAOS_REPOS_NOAUTH=""
RUN	if [ -n "$BUST_CACHE" ] ; then                                                             \
		echo "[INFO] Busting cache" &&                                                     \
		dnf update ;                                                                       \
	fi &&                                                                                      \
	for repo in ${DAOS_REPOS} ; do                                                             \
		echo "[INFO] Adding rpm repository: $repo" &&                                      \
		dnf config-manager --add-repo "$repo" ;                                            \
	done &&                                                                                    \
	for gpg_key in ${DAOS_GPG_KEYS} ; do                                                       \
		echo "[INFO] Adding repositories gpg key: $gpg_key" &&                             \
		rpmkeys --import "$gpg_key" ;                                                      \
	done &&                                                                                    \
	for repo in ${DAOS_REPOS_NOAUTH} ; do                                                      \
		echo "[INFO] Disabling authentication for repository: $repo" &&                    \
		dnf config-manager --save --setopt="${repo}.gpgcheck=0" ;                          \
	done &&                                                                                    \
	echo "[INFO] Installing DAOS" &&                                                           \
	dnf install daos &&                                                                        \
	dnf clean all

# Generate GPG authentication certificates for using DAOS authentication
ARG	DAOS_AUTH=yes
RUN	if [ "$DAOS_AUTH" == yes ] ; then                                                          \
		echo "[INFO] Generating authentication certificates" &&                            \
		if [ ! -d /etc/daos/certs ] ; then                                                 \
			mkdir -d /etc/daos/certs &&                                                \
			chown root:root /etc/daos/certs &&                                         \
			chmod 755 /etc/daos/certs ;                                                \
		fi &&                                                                              \
# XXX WARNING XXX With a production platform, these certificates should be provided with a volume
# XXX WARNING XXX (or Secrets with K8S).
		cd /root && /usr/lib64/daos/certgen/gen_certificates.sh ;                          \
	fi
