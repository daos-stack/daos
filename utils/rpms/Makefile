NAME    := daos
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)

PR_REPOS := spdk@PR-11

#https://github.com/rpm-software-management/mock/issues/384
MOCK_OPTIONS := --disablerepo=sclo*

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

$(NAME)-$(VERSION).tar.gz: $(shell git ls-files :/:)
	echo Creating $@
	echo $(basename $@)
	cd ../../ &&                                          \
	git archive --format tar --prefix $(NAME)-$(VERSION)/ \
	            -o $$OLDPWD/$(basename $@) HEAD ./
	rm -f $@
	gzip $(basename $@)
