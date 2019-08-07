NAME    := cart
SRC_EXT := gz
SOURCE   = $(NAME)-$(VERSION).tar.$(SRC_EXT)
ID_LIKE=$(shell . /etc/os-release; echo $$ID_LIKE)
ifneq ($(ID_LIKE),debian)
PATCHES  = scons_local-$(VERSION).tar.$(SRC_EXT)
endif

%.$(SRC_EXT): %
	rm -f $@
	gzip $<

dist: $(SOURCES)

GIT_SHORT       := $(shell git rev-parse --short HEAD)
GIT_NUM_COMMITS := $(shell git rev-list HEAD --count)

BUILD_DEFINES := --define "%relval .$(GIT_NUM_COMMITS).g$(GIT_SHORT)"
MOCK_OPTIONS  := $(BUILD_DEFINES)
COMMON_RPM_ARGS := --define "%_topdir $$PWD/_topdir" $(BUILD_DEFINES)

DIST    := $(shell rpm $(COMMON_RPM_ARGS) --eval %{?dist})
ifeq ($(DIST),)
SED_EXPR := 1p
else
SED_EXPR := 1s/$(DIST)//p
endif
SPEC    := $(NAME).spec
VERSION := $(shell rpm $(COMMON_RPM_ARGS) --specfile --qf '%{version}\n' $(SPEC) | sed -n '1p')
DOT     := .
DEB_VERS := $(subst rc,~rc,$(VERSION))
DEB_RVERS := $(subst $(DOT),\$(DOT),$(DEB_VERS))
DEB_BVERS := $(basename $(subst ~rc,$(DOT)rc,$(DEB_VERS)))
RELEASE := $(shell rpm $(COMMON_RPM_ARGS) --specfile --qf '%{release}\n' $(SPEC) | sed -n '$(SED_EXPR)')
SRPM    := _topdir/SRPMS/$(NAME)-$(VERSION)-$(RELEASE)$(DIST).src.rpm
RPMS    := $(addsuffix .rpm,$(addprefix _topdir/RPMS/x86_64/,$(shell rpm --specfile $(SPEC))))
DEB_TOP := _topdir/BUILD
DEB_BUILD := $(DEB_TOP)/$(NAME)-$(DEB_VERS)
DEB_TARBASE := $(DEB_TOP)/$(NAME)_$(DEB_VERS)
DEBS    := $(addsuffix _$(DEB_VERS)-1_amd64.deb,$(shell sed -n 's,^Package:[[:blank:]],$(DEB_TOP)/,p' debian/control))
SOURCES := $(addprefix _topdir/SOURCES/,$(notdir $(SOURCE)) $(PATCHES))
ifeq ($(ID_LIKE),debian)
#Ubuntu Containers do not set a UTF-8 environment by default.
ifndef LANG
export LANG = C.UTF-8
endif
ifndef LC_ALL
export LC_ALL = C.UTF-8
endif
TARGETS := $(DEBS)
else
TARGETS := $(RPMS) $(SRPM)
endif

all: $(TARGETS)

%/:
	mkdir -p $@

_topdir/SOURCES/%: % | _topdir/SOURCES/
	rm -f $@
	ln $< $@

ifeq ($(ID_LIKE),debian)
# debian wants all the submodules in the source tarball.
$(NAME)-$(VERSION).tar: $(shell find debian -type f) Makefile | $(DEB_BUILD)/
	git archive --format tar --prefix $(NAME)-$(VERSION)/ -o $@ HEAD
	p=$$(pwd) && (echo .; git submodule foreach) | \
	  while read entering path; do \
	  path1="$${path%\'}"; \
	  path2="$${path1#\'}"; \
	  [ "$$path2" = "" ] && continue; \
	  (cd $$path2 && git archive \
	    --prefix $(NAME)-$(VERSION)/$$path2/ HEAD > \
	    $$p/$(DEB_TOP)/tmp.tar && tar --concatenate \
	     --file=$$p/$@ $$p/$(DEB_TOP)/tmp.tar && \
	     rm $$p/$(DEB_TOP)/tmp.tar); \
	  done
	rm -f $(DEB_BUILD).tar.$(SRC_EXT)
	rm -f $(DEB_TARBASE).orig.tar.$(SRC_EXT)
else
scons_local-$(VERSION).tar:
	cd scons_local && \
	git archive --format tar --prefix scons_local/ -o ../$@ HEAD

$(NAME)-$(VERSION).tar:
	git archive --format tar --prefix $(NAME)-$(VERSION)/ -o $@ HEAD
endif

v$(VERSION).tar.$(SRC_EXT):
	curl -f -L -O '$(SOURCE)'

$(VERSION).tar.$(SRC_EXT):
	curl -f -L -O '$(SOURCE)'

$(DEB_TOP)/%: % | $(DEB_TOP)/

$(DEB_BUILD)/%: % | $(DEB_BUILD)/

$(DEB_BUILD).tar.$(SRC_EXT): $(notdir $(SOURCE)) | $(DEB_TOP)/
	ln -f $< $@

$(DEB_TARBASE).orig.tar.$(SRC_EXT) : $(DEB_BUILD).tar.$(SRC_EXT)
	ln -f $< $@

# Unpack tarball
$(DEB_TOP)/.detar: $(notdir $(SOURCE)) $(DEB_TARBASE).orig.tar.$(SRC_EXT)
	rm -rf ./$(DEB_BUILD)/*
	mkdir -p $(DEB_BUILD)
	tar -C $(DEB_BUILD) --strip-components=1 -xpf $<
	touch $@

# Extract patches for Debian
$(DEB_TOP)/.patched: $(PATCHES) | $(DEB_BUILD)/debian/
	mkdir -p ${DEB_BUILD}/debian/patches
	mkdir -p $(DEB_TOP)/patches
	for f in $(PATCHES); do \
          rm -f $(DEB_TOP)/patches/*; \
	  if git mailsplit -o$(DEB_TOP)/patches < "$$f" ;then \
	    fn=$$(basename "$$f"); \
	    for f1 in $(DEB_TOP)/patches/*; do \
	      f1n=$$(basename "$$f1"); \
	      echo "$${fn}_$${f1n}" >> $(DEB_BUILD)/debian/patches/series ; \
	      mv "$$f1" $(DEB_BUILD)/debian/patches/$${fn}_$${f1n}; \
	    done; \
	  else \
	    fb=$$(basename "$$f"); \
	    cp "$$f" $(DEB_BUILD)/debian/patches/ ; \
	    echo "$$fb" >> $(DEB_BUILD)/debian/patches/series ; \
	    if ! grep -q "^Description:\|^Subject:" "$$f" ;then \
	      sed -i '1 iSubject: Auto added patch' \
	        "$(DEB_BUILD)/debian/patches/$$fb" ;fi ; \
	    if ! grep -q "^Origin:\|^Author:\|^From:" "$$f" ;then \
	      sed -i '1 iOrigin: other' \
	        "$(DEB_BUILD)/debian/patches/$$fb" ;fi ; \
	  fi ; \
	done
	touch $@

# Move the debian files into the Debian directory.
$(DEB_TOP)/.deb_files : $(shell find debian -type f) \
	  $(DEB_TOP)/.detar | \
	  $(DEB_BUILD)/debian/
	find debian -maxdepth 1 -type f -exec cp '{}' '$(DEB_BUILD)/{}' ';'
	if [ -e debian/source ]; then \
	  cp -r debian/source $(DEB_BUILD)/debian; fi
	rm -f $(DEB_BUILD)/debian/*.ex $(DEB_BUILD)/debian/*.EX
	rm -f $(DEB_BUILD)/debian/*.orig
	touch $@

# see https://stackoverflow.com/questions/2973445/ for why we subst
# the "rpm" for "%" to effectively turn this into a multiple matching
# target pattern rule
$(subst rpm,%,$(RPMS)): $(SPEC) $(SOURCES)
	rpmbuild -bb $(COMMON_RPM_ARGS) $(RPM_BUILD_OPTIONS) $(SPEC)

$(subst deb,%,$(DEBS)): $(DEB_BUILD).tar.$(SRC_EXT) check-env \
	  $(DEB_TOP)/.deb_files $(DEB_TOP)/.detar $(DEB_TOP)/.patched
	rm -f $(DEB_TOP)/*.deb $(DEB_TOP)/*.ddeb $(DEB_TOP)/*.dsc \
	$(DEB_TOP)/*.dsc $(DEB_TOP)/*.build* $(DEB_TOP)/*.changes \
	$(DEB_TOP)/*.debian.tar.*
	rm -rf $(DEB_TOP)/*-tmp
	cd $(DEB_BUILD); debuild --no-lintian -b -us -uc
	cd $(DEB_BUILD); debuild -- clean
	git status
	rm -rf $(DEB_TOP)/$(NAME)-tmp
	lfile1=$(shell echo $(DEB_TOP)/$(NAME)[0-9]*_$(DEB_VERS)-1_amd64.deb);\
	  lfile=$$(ls $${lfile1}); \
	  lfile2=$${lfile##*/}; lname=$${lfile2%%_*}; \
	  dpkg-deb -R $${lfile} \
	    $(DEB_TOP)/$(NAME)-tmp; \
	  if [ -e $(DEB_TOP)/$(NAME)-tmp/DEBIAN/symbols ]; then \
	    sed 's/$(DEB_RVERS)-1/$(DEB_BVERS)/' \
	    $(DEB_TOP)/$(NAME)-tmp/DEBIAN/symbols \
	    > $(DEB_BUILD)/debian/$${lname}.symbols; fi
	cd $(DEB_BUILD); debuild -us -uc
	rm $(DEB_BUILD).tar.$(SRC_EXT)
	for f in $(DEB_TOP)/*.deb; do \
	  echo $$f; dpkg -c $$f; done

$(SRPM): $(SPEC) $(SOURCES)
	rpmbuild -bs $(COMMON_RPM_ARGS) $(SPEC)

srpm: $(SRPM)

$(RPMS): Makefile

rpms: $(RPMS)

$(DEBS): Makefile

debs: $(DEBS)

ls: $(TARGETS)
	ls -ld $^

mockbuild: $(SRPM) Makefile
	mock $(MOCK_OPTIONS) $<

rpmlint: $(SPEC)
	rpmlint $<

#Debian wants a distclean target
distclean:
	@echo "distclean"

check-env:
ifndef DEBEMAIL
	$(error DEBEMAIL is undefined)
endif
ifndef DEBFULLNAME
	$(error DEBFULLNAME is undefined)
endif

show_version:
	@echo $(VERSION)

show_release:
	@echo $(RELEASE)

show_rpms:
	@echo $(RPMS)

show_source:
	@echo $(SOURCE)

show_sources:
	@echo $(SOURCES)

show_targets:
	@echo $(TARGETS)

.PHONY: srpm rpms debs ls mockbuild rpmlint FORCE show_version show_release show_rpms show_source show_sources show_targets check-env distclean
