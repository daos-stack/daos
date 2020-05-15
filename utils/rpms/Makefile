NAME    := daos
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)

#https://github.com/rpm-software-management/mock/issues/384
MOCK_OPTIONS := --disablerepo=sclo*

PR_REPOS := $(shell set -x; git show -s --format=%B | sed -ne 's/^PR-repos: *\(.*\)/\1/p')

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

$(NAME)-$(VERSION).tar.gz: $(shell git ls-files :/:)
	echo Creating $@
	echo $(basename $@)
	cd ../../ &&                                          \
	git archive --format tar --prefix $(NAME)-$(VERSION)/ \
	            -o $$OLDPWD/$(basename $@) HEAD ./
	rm -f $@
	gzip $(basename $@)
