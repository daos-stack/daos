NAME    := daos
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)
PATCHES  = scons_local-$(VERSION).tar.$(SRC_EXT)

#https://github.com/rpm-software-management/mock/issues/384
MOCK_OPTIONS := --disablerepo=sclo*

# override allows the caller to set a value and the we append to it
#override PR_REPOS         +=
GIT_SHORT       := $(shell git rev-parse --short HEAD)
GIT_NUM_COMMITS := $(shell git rev-list HEAD --count)
ON_TAG          := $(shell if git diff-index --name-only HEAD^ | grep -q TAG; then \
	                       echo "true"; else echo "false"; fi)

ifeq ($(ON_TAG),false)
BUILD_DEFINES     := --define "%relval .$(GIT_NUM_COMMITS).g$(GIT_SHORT)"
endif

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

repo: rpms
	cd _topdir/RPMS/ && createrepo
