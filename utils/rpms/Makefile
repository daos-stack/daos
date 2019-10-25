NAME    := cart
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)
PATCHES  = scons_local-$(VERSION).tar.$(SRC_EXT)

SLES_12_PR_REPOS := scons
LEAP_42_PR_REPOS := scons
LEAP_42_REPOS    := https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_42.3
SLES_12_REPOS    := https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_42.3            \
	            https://download.opensuse.org/repositories/science:/HPC:/SLE12SP3_Missing/SLE_12_SP3/ \
	            https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/

GIT_SHA1        := $(shell git rev-parse HEAD)
GIT_SHORT       := $(shell git rev-parse --short HEAD)
GIT_NUM_COMMITS := $(shell git rev-list HEAD --count)

BUILD_DEFINES := --define "%relval .$(GIT_NUM_COMMITS).g$(GIT_SHORT)" --define "%sha1 $(GIT_SHA1)"
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
