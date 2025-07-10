NAME    := daos
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)
TOPDIR  := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/../..)

#https://github.com/rpm-software-management/mock/issues/384
MOCK_OPTIONS += --disablerepo=sclo*

DEBUG           := $(shell set -x; id >&2; pwd >&2; ls -ld . ../.. >&2; git status >&2)
GIT_SHORT       := $(shell git rev-parse --short=8 HEAD)
GIT_NUM_COMMITS := $(shell git rev-list HEAD --count)
ON_TAG          := $(shell if git diff-index --name-only HEAD^ | grep -q TAG; then \
	                       echo "true"; else echo "false"; fi)
TOPDIR := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/../..)

PACKAGING_CHECK_DIR := ../../../rpm/packaging/

ifeq ($(ON_TAG),false)
BUILD_DEFINES     := --define "relval .$(GIT_NUM_COMMITS).g$(GIT_SHORT)"
endif

RPM_BUILD_OPTIONS := $(BUILD_DEFINES)

dist: $(SOURCES)

include packaging/Makefile_packaging.mk

PACKAGING_CHECK_DIR ?= ../../../rpm/packaging

$(NAME)-$(VERSION).tar.gz: $(shell git ls-files :/:)
	echo Creating $@
	echo $(basename $@)
	cd ../../ && utils/rpms/archive.sh utils/rpms "$(NAME)" "$(VERSION)" tar
	rm -f $@
	gzip $(basename $@)

tarball: $(NAME)-$(VERSION).tar.gz
