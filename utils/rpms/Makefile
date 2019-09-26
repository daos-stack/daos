NAME    := daos
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)
PATCHES  = scons_local-$(VERSION).tar.$(SRC_EXT)

sle12_PR_REPOS := scons
sl42_PR_REPOS  := scons
PR_REPOS       := openpa libfabric pmix ompi@PR-7 mercury spdk isa-l fio dpdk \
	          protobuf-c fuse pmdk argobots raft cart@daos_devel1
el7_REPOS      := https://copr-be.cloud.fedoraproject.org/results/jhli/ipmctl/epel-7-x86_64/   \
	          https://copr-be.cloud.fedoraproject.org/results/jhli/safeclib/epel-7-x86_64/
sl42_REPOS     := https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_42.3/        \
	          http://download.opensuse.org/repositories/devel:/languages:/go/openSUSE_Leap_42.3/ \
	          https://download.opensuse.org/repositories/home:/jhli/SLE_15/
sle12_REPOS    := https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_42.3/           \
	          https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/          \
	          http://download.opensuse.org/repositories/devel:/languages:/go/SLE_12_SP3_Backports/  \
	          https://download.opensuse.org/repositories/home:/jhli/SLE_15/                         \
	          https://download.opensuse.org/repositories/science:/HPC:/SLE12SP3_Missing/SLE_12_SP3/ \
	          https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/

GIT_SHORT       := $(shell git rev-parse --short HEAD)
GIT_NUM_COMMITS := $(shell git rev-list HEAD --count)

BUILD_DEFINES := --define "%relval .$(GIT_NUM_COMMITS).g$(GIT_SHORT)"
RPM_BUILD_OPTIONS := $(BUILD_DEFINES)

dist: $(SOURCES)

include packaging/Makefile_packaging.mk

PACKAGING_CHECK_DIR ?= ../../../rpm/packaging

scons_local-$(VERSION).tar.gz:
	cd ../../scons_local &&                        \
	git archive --format tar --prefix scons_local/ \
	            -o $$OLDPWD/$(basename $@) HEAD ./
	rm -f $@
	gzip $(basename $@)

$(NAME)-$(VERSION).tar.gz:
	echo $@
	echo $(basename $@)
	cd ../../ &&                                          \
	git archive --format tar --prefix $(NAME)-$(VERSION)/ \
	            -o $$OLDPWD/$(basename $@) HEAD ./
	rm -f $@
	gzip $(basename $@)
