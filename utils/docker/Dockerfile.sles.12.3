#
# Copyright 2019, Intel Corporation
#
# 'recipe' for Docker to build an image of Leap-based
# environment for building the DAOS project.
#

# Pull base image
FROM suse/sles:12.3
MAINTAINER Johann Lombardi <johann.lombardi@intel.com>

# Build arguments can be set via -build-arg
# use same UID as host and default value of 1000 if not specified
ARG UID=1000

# Update distribution
# It's better to put the zypper update in the same "cache layer" as the
# zypper install command so that the database is updated if/when the
# installed packages list below is updated
RUN zypper --non-interactive update
RUN zypper --non-interactive install                                   \
           boost-devel ca-certificates-mozilla cmake cunit-devel curl  \
           doxygen flex gcc gcc-c++ git go graphviz                    \
           libaio-devel libevent-devel libiscsi-devel libltdl7 libtool \
           libnuma-devel libopenssl-devel libuuid-devel libyaml-devel  \
           make man nasm ninja pandoc patch                            \
           python python-xml python3                                   \
           readline-devel rsync yasm valgrind

RUN curl -ksS "https://bootstrap.pypa.io/get-pip.py" -o "/tmp/git-pip.py"; \
    python /tmp/git-pip.py pip; \
    python3 /tmp/git-pip.py pip; \
    cp /usr/bin/pip2 /usr/bin/pip; \
    rm /tmp/git-pip.py
RUN pip install -U setuptools
RUN pip install -U wheel
RUN pip install scons==3.0.1; pip3 install -U "meson<0.45.0"

# No clang libcmocka-devel meson python2-pip sg3_utils
RUN zypper install -y gzip which

RUN curl -fsSL -o /tmp/libcmocka0-1.0.1-9.3.x86_64.rpm \
  http://download.opensuse.org/distribution/leap/42.3/repo/oss/suse/x86_64/libcmocka0-1.0.1-9.3.x86_64.rpm
RUN curl -fsSL -o /tmp/libcmocka-devel-1.0.1-9.3.x86_64.rpm \
  http://download.opensuse.org/distribution/leap/42.3/repo/oss/suse/x86_64/libcmocka-devel-1.0.1-9.3.x86_64.rpm
RUN zypper --non-interactive --no-gpg-checks install \
  /tmp/libcmocka0-1.0.1-9.3.x86_64.rpm \
  /tmp/libcmocka-devel-1.0.1-9.3.x86_64.rpm; \
  rm /tmp/libcmocka0-1.0.1-9.3.x86_64.rpm \
    /tmp/libcmocka-devel-1.0.1-9.3.x86_64.rpm

RUN curl -fsSL -o /tmp/go-1.10.4-40.1.x86_64.rpm \
  http://download.opensuse.org/update/leap/42.3/oss/x86_64/go-1.10.4-40.1.x86_64.rpm
RUN curl -fsSL -o /tmp/go1.10-1.10.4-2.2.x86_64.rpm \
  http://download.opensuse.org/update/leap/42.3/oss/x86_64/go1.10-1.10.4-2.2.x86_64.rpm
RUN curl -fsSL -o /tmp/go1.10-doc-1.10.4-2.2.x86_64.rpm \
  http://download.opensuse.org/update/leap/42.3/oss/x86_64/go1.10-doc-1.10.4-2.2.x86_64.rpm
RUN curl -fsSL -o /tmp/go1.10-race-1.10.4-2.2.x86_64.rpm \
  http://download.opensuse.org/update/leap/42.3/oss/x86_64/go1.10-race-1.10.4-2.2.x86_64.rpm
RUN curl -fsSL -o /tmp/go-doc-1.10.4-40.1.x86_64.rpm \
  http://download.opensuse.org/update/leap/42.3/oss/x86_64/go-doc-1.10.4-40.1.x86_64.rpm
RUN zypper --non-interactive --no-gpg-checks install \
  /tmp/go-1.10.4-40.1.x86_64.rpm /tmp/go1.10-1.10.4-2.2.x86_64.rpm \
  /tmp/go1.10-doc-1.10.4-2.2.x86_64.rpm \
  /tmp/go1.10-race-1.10.4-2.2.x86_64.rpm; \
  rm /tmp/go-1.10.4-40.1.x86_64.rpm /tmp/go1.10-1.10.4-2.2.x86_64.rpm \
    /tmp/go1.10-doc-1.10.4-2.2.x86_64.rpm \
    /tmp/go1.10-race-1.10.4-2.2.x86_64.rpm

RUN curl -fsSL -o /tmp/jhli.key https://download.opensuse.org/repositories/home:/jhli/SLE_15/repodata/repomd.xml.key
RUN rpm --import /tmp/jhli.key
RUN zypper --non-interactive addrepo https://download.opensuse.org/repositories/home:/jhli/SLE_15/home:jhli.repo
RUN zypper --non-interactive refresh
RUN zypper --non-interactive install -y ipmctl-devel

# Add DAOS user
ENV USER daos
ENV PASSWD daos
RUN useradd -u $UID -ms /bin/bash $USER

# Create directory for DAOS backend storage
RUN mkdir /mnt/daos
RUN chown daos.daos /mnt/daos || { cat /etc/passwd; cat /etc/group; cat /etc/shadow; chown daos /mnt/daos; chgrp daos /mnt/daos; ls -ld /mnt/daos; }

# Switch to new user
USER $USER
WORKDIR /home/$USER

# set NOBUILD to disable git clone & build
ARG NOBUILD

# Fetch DAOS code
RUN if [ "x$NOBUILD" = "x" ] ; then git clone https://github.com/daos-stack/daos.git; fi
WORKDIR /home/$USER/daos

# Build DAOS & dependencies
RUN if [ "x$NOBUILD" = "x" ] ; then git submodule init && git submodule update; fi
RUN if [ "x$NOBUILD" = "x" ] ; then scons --build-deps=yes USE_INSTALLED=all install; fi

# Set environment variables
ENV PATH=/home/daos/daos/install/bin:$PATH
ENV LD_LIBRARY_PATH=/home/daos/daos/install/lib:/home/daos/daos/install/lib/daos_srv:$LD_LIBRARY_PATH
ENV CPATH=/home/daos/daos/install/include:$CPATH
ENV CRT_PHY_ADDR_STR="ofi+sockets"
ENV OFI_INTERFACE=eth0
