NAME    := daos
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)
PATCHES  = scons_local-$(VERSION).tar.$(SRC_EXT)

#https://github.com/rpm-software-management/mock/issues/384
MOCK_OPTIONS := --disablerepo=sclo*

# override allows the caller to set a value and the we append to it
#override PR_REPOS         +=
override SLES_12_PR_REPOS += scons
override LEAP_42_PR_REPOS += scons
ifneq ($(REPOSITORY_URL),)
ifneq ($(DAOS_STACK_$(DISTRO_BASE)_GROUP_REPO),)
EL_7_REPOS     = https://copr-be.cloud.fedoraproject.org/results/jhli/ipmctl/epel-7-x86_64/   \
	         https://copr-be.cloud.fedoraproject.org/results/jhli/safeclib/epel-7-x86_64/
endif
endif
LEAP_42_REPOS  = https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_42.3/        \
	         https://download.opensuse.org/repositories/devel:/languages:/go/openSUSE_Leap_42.3/
LEAP_42_REPOS += https://download.opensuse.org/repositories/home:/jhli/SLE_15/
SLES_12_REPOS  = https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_42.3/           \
	         https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/          \
	         https://download.opensuse.org/repositories/devel:/languages:/go/SLE_12_SP3_Backports/
SLES_12_REPOS += https://download.opensuse.org/repositories/home:/jhli/SLE_15/
SLES_12_REPOS += https://download.opensuse.org/repositories/science:/HPC:/SLE12SP3_Missing/SLE_12_SP3/ \
	         https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/

GIT_SHORT       := $(shell git rev-parse --short HEAD)
GIT_NUM_COMMITS := $(shell git rev-list HEAD --count)
CART_SHA1       := $(shell sed -ne 's/CART *= *\(.*\)/\1/p' ../../utils/build.config)
ON_TAG          := $(shell if git diff-index --name-only HEAD^ | grep -q TAG; then \
	                       echo "true"; else echo "false"; fi)

ifeq ($(ON_TAG),false)
BUILD_DEFINES     := --define "%relval .$(GIT_NUM_COMMITS).g$(GIT_SHORT)"
endif

BUILD_DEFINES     += --define "%cart_sha1 $(CART_SHA1)"
RPM_BUILD_OPTIONS := $(BUILD_DEFINES)

dist: $(SOURCES)

include packaging/Makefile_packaging.mk

PACKAGING_CHECK_DIR ?= ../../../rpm/packaging

scons_local-$(VERSION).tar.gz: $(shell git ls-files :/:)
	echo Creating $@
	cd ../../scons_local &&                        \
	git archive --format tar --prefix scons_local/ \
	            -o $$OLDPWD/$(basename $@) HEAD ./
	rm -f $@
	gzip $(basename $@)

$(NAME)-$(VERSION).tar.gz: $(shell git ls-files :/:)
	echo Creating $@
	echo $(basename $@)
	cd ../../ &&                                          \
	git archive --format tar --prefix $(NAME)-$(VERSION)/ \
	            -o $$OLDPWD/$(basename $@) HEAD ./
	rm -f $@
	gzip $(basename $@)
