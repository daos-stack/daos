#
# Copyright 2018-2019, Intel Corporation
#
# 'recipe' for Docker to build an RPM
#

# Pull base image
FROM centos:7
MAINTAINER Brian J. Murrell <brian.murrell@intel.com>

# use same UID as host and default value of 1000 if not specified
ARG UID=1000

# Update distribution
#Nothing to do for CentOS

# Install basic tools
RUN yum -y install epel-release
RUN yum -y install mock make rpm-build curl createrepo rpmlint git

# Add build user (to keep rpmbuild happy)
ENV USER build
ENV PASSWD build
RUN useradd -u $UID -ms /bin/bash $USER
RUN echo "$USER:$PASSWD" | chpasswd
# add the user to the mock group so it can run mock
RUN usermod -a -G mock $USER

# mock in Docker needs to use the old-chroot option
RUN echo "config_opts['use_nspawn'] = False" >> /etc/mock/site-defaults.cfg

ARG JENKINS_URL=""

RUN echo -e "config_opts['yum.conf'] += \"\"\"\n" >> /etc/mock/default.cfg;  \
    for repo in openpa libfabric pmix ompi mercury spdk isa-l fio dpdk       \
                protobuf-c fuse pmdk argobots raft cart@daos_devel; do       \
        if [[ $repo = *@* ]]; then                                           \
            branch="${repo#*@}";                                             \
            repo="${repo%@*}";                                               \
        else                                                                 \
            branch="master";                                                 \
        fi;                                                                  \
        echo -e "[$repo:$branch:lastSuccessful]\n\
name=$repo:$branch:lastSuccessful\n\
baseurl=${JENKINS_URL}job/daos-stack/job/$repo/job/$branch/lastSuccessfulBuild/artifact/artifacts/centos7/\n\
enabled=1\n\
gpgcheck = False\n" >> /etc/mock/default.cfg;                           \
    done;                                                               \
    echo -e "[jhli-ipmctl]\n\
name=Copr repo for ipmctl owned by jhli\n\
baseurl=https://copr-be.cloud.fedoraproject.org/results/jhli/ipmctl/epel-7-\$basearch/\n\
type=rpm-md\n\
skip_if_unavailable=True\n\
gpgcheck=1\n\
gpgkey=https://copr-be.cloud.fedoraproject.org/results/jhli/ipmctl/pubkey.gpg\n\
repo_gpgcheck=0\n\
enabled=1\n\
enabled_metadata=1\n\n\
[jhli-safeclib]\n\
name=Copr repo for safeclib owned by jhli\n\
baseurl=https://copr-be.cloud.fedoraproject.org/results/jhli/safeclib/epel-7-\$basearch/\n\
type=rpm-md\n\
skip_if_unavailable=True\n\
gpgcheck=1\n\
gpgkey=https://copr-be.cloud.fedoraproject.org/results/jhli/safeclib/pubkey.gpg\n\
repo_gpgcheck=0\n\
enabled=1\n\
enabled_metadata=1\n\"\"\"" >> /etc/mock/default.cfg
