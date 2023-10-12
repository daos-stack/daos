# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a rpms el8 DAOS builder

ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"$DAOS_DOCKER_IMAGE_NSP/daos-dev-$LINUX_DISTRO:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

# Base configuration of dnf and system update
USER	root:root
RUN	dnf clean all &&                                                                           \
	dnf makecache &&                                                                           \
	dnf --assumeyes install dnf-plugins-core &&                                                \
	dnf config-manager --save --setopt=assumeyes=True &&                                       \
	dnf config-manager --save --setopt=fastestmirror=True &&                                   \
	dnf config-manager --set-enabled powertools &&                                             \
	dnf install epel-release systemd vim openssh-server &&                                     \
	dnf update --exclude=spdk,spdk-devel,dpdk-devel,dpdk,mercury-devel,mercury &&              \
	dnf clean all

# Install and configuration of ansible
USER	daos_dev:daos_dev
RUN	source daos-venv/bin/activate &&                                                           \
	pip3 install --upgrade --requirement daos-src/utils/ansible/ftest/requirements.txt
COPY	inventory.yml daos-ftest/inventory.yml

# Install and configuration of ssh
RUN	sudo systemctl disable sshd &&                                                             \
	ssh-keygen -q -t rsa -N '' -f /home/daos/.ssh/id_rsa &&                                    \
	cp /home/daos/.ssh/id_rsa.pub /home/daos/.ssh/authorized_keys &&                           \
	chmod 600 /home/daos/.ssh/authorized_keys &&                                               \
	sudo mkdir /root/.ssh &&                                                                   \
	sudo cp /home/daos/.ssh/id_rsa.pub /root/.ssh/authorized_keys &&                           \
	sudo chmod 600 /root/.ssh/authorized_keys &&                                               \
	sudo rm -f /run/nologin

# Configure systemd: more details could be found at following URL:
# https://markandruth.co.uk/2020/10/10/running-systemd-inside-a-centos-8-docker-container
# XXX FIXME XXX Should be removed in production with application dedicated entry point
USER	root:root
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
