# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a RPMs leap15 DAOS builder

ARG	LINUX_DISTRO=""
ARG	DAOS_DOCKER_IMAGE_NSP=""
ARG	DAOS_DOCKER_IMAGE_TAG=""
FROM	"$DAOS_DOCKER_IMAGE_NSP/daos-builder-$LINUX_DISTRO:$DAOS_DOCKER_IMAGE_TAG"
LABEL	maintainer="daos@daos.groups.io"

USER	root:root
RUN	dnf clean all && \
	dnf install -y	capstone \
			gdb \
			jq \
			lbzip2 \
			numactl \
			numactl-libs \
			numatop \
			platform-python \
			systemd \
			tree \
			python36 \
			capstone-devel \
			bzip2-devel \
			clustershell \
			cmake \
			wget \
			rpmdevtools

ARG	DAOS_REPOS=""
ARG	DAOS_GPG_KEYS=""
RUN	wget -O /etc/yum.repos.d/daos-packages.repo "${DAOS_REPOS}" && \
	rpm --import "${DAOS_GPG_KEYS}" && \
	dnf update && \
	dnf install -y spdk-devel argobots-devel

ARG	DAOS_GIT_REPOS=""
ARG	DAOS_GIT_BRANCH=""
USER	daos_server:daos_server
RUN	git clone --recurse-submodules "${DAOS_GIT_REPOS}" daos-build
RUN	if [ "${DAOS_GIT_BRANCH}" != "master" ] ; then \
		cd daos-build && \
		git switch --recurse-submodules --create="${DAOS_GIT_BRANCH}" "origin/${DAOS_GIT_BRANCH}" ; \
	fi

USER	root:root
RUN	cd daos-build/src/rdb/raft && dnf build-dep --spec raft.spec
USER	daos_server:daos_server
RUN	cd daos-build/src/rdb/raft && make -f Makefile-rpm.mk
USER	root:root
RUN	cd daos-build/src/rdb/raft && dnf install -y _topdir/RPMS/x86_64/*.rpm

ARG	DAOS_VENV_DIR=""
USER	daos_server:daos_server
RUN	mkdir -p "${DAOS_VENV_DIR}" && python3 -m venv "${DAOS_VENV_DIR}"
RUN	source "${DAOS_VENV_DIR}/bin/activate" && \
	pip3 install --upgrade pip && \
	pip3 install	-r daos-build/requirements-build.txt \
			-r daos-build/requirements-utest.txt \
			-r daos-build/requirements-ftest.txt
