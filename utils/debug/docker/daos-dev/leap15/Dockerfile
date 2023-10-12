# Copyright 2021-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for building a rpms leap15 DAOS builder

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
			numatop \
			systemd \
			tree \
			libcapstone-devel \
			libbz2-devel \
			clustershell \
			cmake \
			wget \
			rpmdevtools \
			rpmlint \
			rpm-build

ARG	DAOS_REPOS=""
ARG	DAOS_GPG_KEYS=""
RUN	wget -O /etc/yum.repos.d/daos-packages.repo "${DAOS_REPOS}" && \
	rpm --import "${DAOS_GPG_KEYS}" && \
	dnf update && \
	dnf install -y spdk-devel

ARG	DAOS_GIT_REPOS=""
ARG	DAOS_GIT_BRANCH=""
USER	daos_server:daos_server
RUN	git clone --recurse-submodules "${DAOS_GIT_REPOS}" daos-src
RUN	if [ "${DAOS_GIT_BRANCH}" != "master" ] ; then \
		cd daos-src && \
		git switch --recurse-submodules --create="${DAOS_GIT_BRANCH}" "origin/${DAOS_GIT_BRANCH}" ; \
	fi

USER	root:root
RUN	cd daos-src/src/rdb/raft && dnf build-dep --spec raft.spec
USER	daos_server:daos_server
RUN	cd daos-src/src/rdb/raft && make -f Makefile-rpm.mk
USER	root:root
RUN	cd daos-src/src/rdb/raft && dnf install -y _topdir/RPMS/x86_64/*.rpm

# FIXME Some packages such as mercury are not available as it
# RUN	cd daos-src/utils/rpms && dnf build-dep --spec daos.spec

ARG	DAOS_VENV_DIR=""
USER	daos_server:daos_server
RUN	rm -frv venv bin && mkdir -pv "${DAOS_VENV_DIR}" && python3 -m venv "${DAOS_VENV_DIR}"
RUN	source "${DAOS_VENV_DIR}/bin/activate" && \
	pip3 install --upgrade pip && \
	pip3 install	-r daos-src/requirements-build.txt \
			-r daos-src/requirements-utest.txt \
			-r daos-src/requirements-ftest.txt
