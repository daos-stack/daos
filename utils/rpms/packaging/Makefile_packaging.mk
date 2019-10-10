# Common Makefile for including
# Needs the following variables set at a minium:
# NAME :=
# SRC_EXT :=
# SOURCE =

# Put site overrides (i.e. REPOSITORY_URL, DAOS_STACK_*_LOCAL_REPO) in here
-include Makefile.local

# alternate sources
#OPENSUSE_MIRROR               ?= https://provo-mirror.opensuse.org
#OPENSUSE_REPOS_MIRROR         ?= https://opensuse.mirror.liquidtelecom.com
OPENSUSE_MIRROR               ?= https://download.opensuse.org
OPENSUSE_REPOS_MIRROR         ?= $(OPENSUSE_MIRROR)

ifeq ($(DEB_NAME),)
DEB_NAME := $(NAME)
endif

CALLING_MAKEFILE := $(word 1, $(MAKEFILE_LIST))

DOT     := .
RPM_BUILD_OPTIONS += $(EXTERNAL_RPM_BUILD_OPTIONS)
# Find out what we are
ID_LIKE := $(shell . /etc/os-release; echo $$ID_LIKE)
# Of course that does not work for SLES-12
ID := $(shell . /etc/os-release; echo $$ID)
VERSION_ID := $(shell . /etc/os-release; echo $$VERSION_ID)
ifeq ($(ID_LIKE),debian)
UBUNTU_VERS := $(shell . /etc/os-release; echo $$VERSION)
ifeq ($(VERSION_ID),19.04)
# Bug - distribution is set to "devel"
DISTRO_ID_OPT = --distribution disco
endif
DISTRO_ID := ubuntu$(VERSION_ID)
DISTRO_BASE = $(basename UBUNTU_$(VERSION_ID))
VERSION_ID_STR := $(subst $(DOT),_,$(VERSION_ID))
endif
ifeq ($(ID),centos)
DISTRO_ID := el$(VERSION_ID)
DISTRO_BASE := $(basename EL_$(VERSION_ID))
define install_repo
	if yum-config-manager --add-repo=$(1); then                  \
	    repo_file=$$(ls -tar /etc/yum.repos.d/*.repo | tail -1); \
	    sed -i -e 1d -e '$$s/^/gpgcheck=False/' $$repo_file;     \
	else                                                         \
	    exit 1;                                                  \
	fi
endef
endif
ifeq ($(findstring opensuse,$(ID)),opensuse)
ID_LIKE := suse
DISTRO_ID := sl$(VERSION_ID)
DISTRO_BASE := $(basename LEAP_$(VERSION_ID))
endif
ifeq ($(ID),sles)
# SLES-12 or 15 detected.
ID_LIKE := suse
DISTRO_ID := sle$(VERSION_ID)
DISTRO_BASE := $(basename SLES_$(VERSION_ID))
endif
ifeq ($(ID_LIKE),suse)
define install_repo
	zypper --non-interactive ar $(1)
endef
endif

BUILD_OS ?= leap.42.3
PACKAGING_CHECK_DIR ?= ../packaging
COMMON_RPM_ARGS := --define "%_topdir $$PWD/_topdir" $(BUILD_DEFINES)
DIST    := $(shell rpm $(COMMON_RPM_ARGS) --eval %{?dist})
ifeq ($(DIST),)
SED_EXPR := 1p
else
SED_EXPR := 1s/$(DIST)//p
endif
SPEC    := $(NAME).spec
VERSION := $(shell rpm $(COMMON_RPM_ARGS) --specfile --qf '%{version}\n' $(SPEC) | sed -n '1p')
DEB_VERS := $(subst rc,~rc,$(VERSION))
DEB_RVERS := $(subst $(DOT),\$(DOT),$(DEB_VERS))
DEB_BVERS := $(basename $(subst ~rc,$(DOT)rc,$(DEB_VERS)))
RELEASE := $(shell rpm $(COMMON_RPM_ARGS) --specfile --qf '%{release}\n' $(SPEC) | sed -n '$(SED_EXPR)')
SRPM    := _topdir/SRPMS/$(NAME)-$(VERSION)-$(RELEASE)$(DIST).src.rpm
RPMS    := $(addsuffix .rpm,$(addprefix _topdir/RPMS/x86_64/,$(shell rpm --specfile $(SPEC))))
DEB_TOP := _topdir/BUILD
DEB_BUILD := $(DEB_TOP)/$(NAME)-$(DEB_VERS)
DEB_TARBASE := $(DEB_TOP)/$(DEB_NAME)_$(DEB_VERS)
SOURCES := $(addprefix _topdir/SOURCES/,$(notdir $(SOURCE)) $(PATCHES))
ifeq ($(ID_LIKE),debian)
DEBS    := $(addsuffix _$(DEB_VERS)-1_amd64.deb,$(shell sed -n '/-udeb/b; s,^Package:[[:blank:]],$(DEB_TOP)/,p' debian/control))
DEB_DSC := $(DEB_NAME)_$(shell dpkg-parsechangelog -S version).dsc
#Ubuntu Containers do not set a UTF-8 environment by default.
ifndef LANG
export LANG = C.UTF-8
endif
ifndef LC_ALL
export LC_ALL = C.UTF-8
endif
TARGETS := $(DEBS)
else
# CentOS/Suse packages that want a locale set need this.
ifndef LANG
export LANG = en_US.utf8
endif
ifndef LC_ALL
export LC_ALL = en_US.utf8
endif
TARGETS := $(RPMS) $(SRPM)
endif

define install_repos
	for repo in $($(basename $(DISTRO_ID))_PR_REPOS)                    \
	            $(PR_REPOS) $(1); do                                    \
	    branch="master";                                                \
	    build_number="lastSuccessfulBuild";                             \
	    if [[ $$repo = *@* ]]; then                                     \
	        branch="$${repo#*@}";                                       \
	        repo="$${repo%@*}";                                         \
	        if [[ $$branch = *:* ]]; then                               \
	            build_number="$${branch#*:}";                           \
	            branch="$${branch%:*}";                                 \
	        fi;                                                         \
	    fi;                                                             \
	    case $(DISTRO_ID) in                                            \
	        el7) distro="centos7";                                      \
	        ;;                                                          \
	        sle12.3) distro="sles12.3";                                 \
	        ;;                                                          \
	        sl42.3) distro="leap42.3";                                  \
	        ;;                                                          \
	        sl15.1) distro="leap15.1";                                  \
	        ;;                                                          \
	    esac;                                                           \
	    baseurl=$${JENKINS_URL:-https://build.hpdd.intel.com/}job/daos-stack/job/$$repo/job/$$branch/; \
	    baseurl+=$$build_number/artifact/artifacts/$$distro/;           \
	    $(call install_repo,$$baseurl);                                 \
        done
endef

all: $(TARGETS)

%/:
	mkdir -p $@

%.gz: %
	rm -f $@
	gzip $<

_topdir/SOURCES/%: % | _topdir/SOURCES/
	rm -f $@
	ln $< $@

# At least one spec file, SLURM (sles), has a different version for the
# download file than the version in the spec file.
ifeq ($(DL_VERSION),)
DL_VERSION = $(VERSION)
endif

$(NAME)-$(DL_VERSION).tar.$(SRC_EXT).asc: $(SPEC) $(CALLING_MAKEFILE)
	rm -f ./$(NAME)-*.tar.{gz,bz*,xz}.asc
	curl -f -L -O '$(SOURCE).asc'

$(NAME)-$(DL_VERSION).tar.$(SRC_EXT): $(SPEC) $(CALLING_MAKEFILE)
	rm -f ./$(NAME)-*.tar.{gz,bz*,xz}
	curl -f -L -O '$(SOURCE)'

v$(DL_VERSION).tar.$(SRC_EXT): $(SPEC) $(CALLING_MAKEFILE)
	rm -f ./v*.tar.{gz,bz*,xz}
	curl -f -L -O '$(SOURCE)'

$(DL_VERSION).tar.$(SRC_EXT): $(SPEC) $(CALLING_MAKEFILE)
	rm -f ./*.tar.{gz,bz*,xz}
	curl -f -L -O '$(SOURCE)'

$(DEB_TOP)/%: % | $(DEB_TOP)/

$(DEB_BUILD)/%: % | $(DEB_BUILD)/

$(DEB_BUILD).tar.$(SRC_EXT): $(notdir $(SOURCE)) | $(DEB_TOP)/
	ln -f $< $@

$(DEB_TARBASE).orig.tar.$(SRC_EXT) : $(DEB_BUILD).tar.$(SRC_EXT)
	rm -f $(DEB_TOP)/*.orig.tar.*
	ln -f $< $@

deb_detar: $(notdir $(SOURCE)) $(DEB_TARBASE).orig.tar.$(SRC_EXT)
	# Unpack tarball
	rm -rf ./$(DEB_TOP)/.patched ./$(DEB_TOP)/.detar
	rm -rf ./$(DEB_BUILD)/* ./$(DEB_BUILD)/.pc
	mkdir -p $(DEB_BUILD)
	tar -C $(DEB_BUILD) --strip-components=1 -xpf $<

# Extract patches for Debian
$(DEB_TOP)/.patched: $(PATCHES) check-env deb_detar | \
	$(DEB_BUILD)/debian/
	mkdir -p ${DEB_BUILD}/debian/patches
	mkdir -p $(DEB_TOP)/patches
	for f in $(PATCHES); do \
          rm -f $(DEB_TOP)/patches/*; \
	  if git mailsplit -o$(DEB_TOP)/patches < "$$f" ;then \
	      fn=$$(basename "$$f"); \
	      for f1 in $(DEB_TOP)/patches/*;do \
	        [ -e "$$f1" ] || continue; \
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
ifeq ($(ID_LIKE),debian)
$(DEB_TOP)/.deb_files : $(shell find debian -type f) deb_detar | \
	  $(DEB_BUILD)/debian/
	find debian -maxdepth 1 -type f -exec cp '{}' '$(DEB_BUILD)/{}' ';'
	if [ -e debian/source ]; then \
	  cp -r debian/source $(DEB_BUILD)/debian; fi
	if [ -e debian/local ]; then \
	  cp -r debian/local $(DEB_BUILD)/debian; fi
	if [ -e debian/examples ]; then \
	  cp -r debian/examples $(DEB_BUILD)/debian; fi
	if [ -e debian/upstream ]; then \
	  cp -r debian/upstream $(DEB_BUILD)/debian; fi
	if [ -e debian/tests ]; then \
	  cp -r debian/tests $(DEB_BUILD)/debian; fi
	rm -f $(DEB_BUILD)/debian/*.ex $(DEB_BUILD)/debian/*.EX
	rm -f $(DEB_BUILD)/debian/*.orig
	touch $@
endif

# see https://stackoverflow.com/questions/2973445/ for why we subst
# the "rpm" for "%" to effectively turn this into a multiple matching
# target pattern rule
$(subst rpm,%,$(RPMS)): $(SPEC) $(SOURCES)
	rpmbuild -bb $(COMMON_RPM_ARGS) $(RPM_BUILD_OPTIONS) $(SPEC)

$(subst deb,%,$(DEBS)): $(DEB_BUILD).tar.$(SRC_EXT) \
	  deb_detar $(DEB_TOP)/.deb_files $(DEB_TOP)/.patched
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

$(DEB_TOP)/$(DEB_DSC): $(CALLING_MAKEFILE) $(DEB_BUILD).tar.$(SRC_EXT) \
          deb_detar $(DEB_TOP)/.deb_files $(DEB_TOP)/.patched
	rm -f $(DEB_TOP)/*.deb $(DEB_TOP)/*.ddeb $(DEB_TOP)/*.dsc \
	  $(DEB_TOP)/*.dsc $(DEB_TOP)/*.build* $(DEB_TOP)/*.changes \
	  $(DEB_TOP)/*.debian.tar.*
	rm -rf $(DEB_TOP)/*-tmp
	cd $(DEB_BUILD); dpkg-buildpackage -S --no-sign --no-check-builddeps

$(SRPM): $(SPEC) $(SOURCES)
	rpmbuild -bs $(COMMON_RPM_ARGS) $(RPM_BUILD_OPTIONS) $(SPEC)

srpm: $(SRPM)

$(RPMS): $(SRPM) $(CALLING_MAKEFILE)

rpms: $(RPMS)

$(DEBS): $(CALLING_MAKEFILE)

debs: $(DEBS)

ls: $(TARGETS)
	ls -ld $^

# *_LOCAL_* repos are locally built packages.
# *_GROUP_* repos are a local mirror of a group of upstream repos.
# *_GROUP_* repos may not supply a repomd.xml.key.
ifneq ($(REPOSITORY_URL),)
ifneq ($(DAOS_STACK_$(DISTRO_BASE)_LOCAL_REPO),)
$(DISTRO_BASE)_LOCAL_REPOS  := $(REPOSITORY_URL)$(DAOS_STACK_$(DISTRO_BASE)_LOCAL_REPO)/
endif
ifneq ($(DAOS_STACK_$(DISTRO_BASE)_GROUP_REPO),)
$(DISTRO_BASE)_LOCAL_REPOS  += $(REPOSITORY_URL)$(DAOS_STACK_$(DISTRO_BASE)_GROUP_REPO)/
endif
SUSE_REPO_KEYS := $(OPENSUSE_MIRROR)/repositories/home:/jhli/SLE_15/repodata/repomd.xml.key
# Only needed for LEAP 42.3
SUSE_REPO_KEYS += $(OPENSUSE_MIRROR)/repositories/science:/HPC/openSUSE_Leap_42.3/repodata/repomd.xml.key
endif

ifeq ($(ID_LIKE),rhel fedora)
chrootbuild: $(SRPM) $(CALLING_MAKEFILE)
	if [ -w /etc/mock/default.cfg ]; then                                        \
	    echo -e "config_opts['yum.conf'] += \"\"\"\n" >> /etc/mock/default.cfg;  \
	    for repo in $($(DISTRO_ID)_PR_REPOS) $(PR_REPOS); do                   \
	        branch="master";                                                     \
	        build_number="lastSuccessfulBuild";                                  \
	        if [[ $$repo = *@* ]]; then                                          \
	            branch="$${repo#*@}";                                            \
	            repo="$${repo%@*}";                                              \
	            if [[ $$branch = *:* ]]; then                                    \
	                build_number="$${branch#*:}";                                \
	                branch="$${branch%:*}";                                      \
	            fi;                                                              \
	        fi;                                                                  \
	        echo -e "[$$repo:$$branch:$$build_number]\n\
name=$$repo:$$branch:$$build_number\n\
baseurl=$${JENKINS_URL:-https://build.hpdd.intel.com/}job/daos-stack/job/$$repo/job/$$branch/$$build_number/artifact/artifacts/centos7/\n\
enabled=1\n\
gpgcheck = False\n" >> /etc/mock/default.cfg;                                        \
	    done;                                                                    \
	    for repo in $($(DISTRO_BASE)_LOCAL_REPOS) $($(DISTRO_BASE)_REPOS); do  \
	        repo_name=$${repo##*://};                                            \
	        repo_name=$${repo_name//\//_};                                       \
	        echo -e "[$$repo_name]\n\
name=$${repo_name}\n\
baseurl=$${repo}\n\
enabled=1\n" >> /etc/mock/default.cfg;                                               \
	    done;                                                                    \
	    echo "\"\"\"" >> /etc/mock/default.cfg;                                  \
	else                                                                         \
	    echo "Unable to update /etc/mock/default.cfg.";                          \
            echo "You need to make sure it has the needed repos in it yourself.";    \
	fi
	mock $(MOCK_OPTIONS) $(RPM_BUILD_OPTIONS) $<
else ifeq ($(ID_LIKE),debian)
ifneq ($(DAOS_STACK_REPO_SUPPORT),)
TEST_STR := $(DAOS_STACK_REPO_UBUNTU_$(VERSION_ID_STR)_LIST)
ifneq ($(TEST_STR),)
UBUNTU_REPOS := $(shell curl $(DAOS_STACK_REPO_SUPPORT)$(TEST_STR))
# Additional repos can be added but must be separated by a | character.
UBUNTU_ADD_REPOS = --othermirror "$(UBUNTU_REPOS)"
else
ifneq ($(DAOS_STACK_REPO_UBUNTU_ROLLING_LIST),)
UBUNTU_REPOS := $(shell curl $(DAOS_STACK_REPO_SUPPORT)$(DAOS_STACK_REPO_UBUNTU_ROLLING_LIST))
# Additional repos can be added but must be separated by a | character.
UBUNTU_ADD_REPOS = --othermirror "$(UBUNTU_REPOS)"
endif
endif
# Need to figure out how to support multiple keys, such as for IPMCTL
ifneq ($(DAOS_STACK_REPO_PUB_KEY),)
HAVE_DAOS_STACK_KEY := TRUE

$(DAOS_STACK_REPO_PUB_KEY):
	curl -f -L -O '$(DAOS_STACK_REPO_SUPPORT)$(DAOS_STACK_REPO_PUB_KEY)'
endif
endif

chrootbuild: $(DEB_TOP)/$(DEB_DSC) $(DAOS_STACK_REPO_PUB_KEY)
	sudo pbuilder create \
	    --extrapackages "gnupg ca-certificates" $(DISTRO_ID_OPT)
ifneq ($(HAVE_DAOS_STACK_KEY),)
	printf "apt-key add - <<EOF\n$$(cat $(DAOS_STACK_REPO_PUB_KEY))\nEOF" \
	       | sudo pbuilder --login --save-after-login
endif
	cd $(DEB_TOP); sudo pbuilder --update --override-config $(UBUNTU_ADD_REPOS)
	cd $(DEB_TOP); sudo pbuilder build $(DEB_DSC)
else
SLES_12_REPOS += http://cobbler/cobbler/repo_mirror/sdkupdate-sles12.3-x86_64/   \
	       http://cobbler/cobbler/repo_mirror/sdk-sles12.3-x86_64                \
	       $(OPENSUSE_MIRROR)/repositories/openSUSE:/Backports:/SLE-12/standard/ \
	       http://cobbler/cobbler/repo_mirror/updates-sles12.3-x86_64            \
	       http://cobbler/cobbler/pub/SLES-12.3-x86_64/

LEAP_42_REPOS += $(OPENSUSE_MIRROR)/update/leap/42.3/oss/                         \
	      $(OPENSUSE_MIRROR)/distribution/leap/42.3/repo/oss/suse/

LEAP_15_REPOS += $(OPENSUSE_MIRROR)/update/leap/15.1/oss/                         \
	      $(OPENSUSE_MIRROR)/distribution/leap/15.1/repo/oss/

define install_gpg_key
	curl -L -f -O "$(1)";                         \
	if ! sudo rpm --import repomd.xml.key; then   \
	    cat repomd.xml.key;                       \
	fi
endef

chrootbuild: $(SRPM) $(CALLING_MAKEFILE)
	add_repos="";                                                       \
	for repo in $($(DISTRO_BASE)_PR_REPOS) $(PR_REPOS); do              \
	    branch="master";                                                \
	    build_number="lastSuccessfulBuild";                             \
	    if [[ $$repo = *@* ]]; then                                     \
	        branch="$${repo#*@}";                                       \
	        repo="$${repo%@*}";                                         \
	        if [[ $$branch = *:* ]]; then                               \
	            build_number="$${branch#*:}";                           \
	            branch="$${branch%:*}";                                 \
	        fi;                                                         \
	    fi;                                                             \
	    case $(DISTRO_ID) in                                            \
	        sle12.3) distro="sles12.3";                                 \
	        ;;                                                          \
	        sl42.3) distro="leap42.3";                                  \
	        ;;                                                          \
	        sl15.1) distro="leap15";                                    \
	        ;;                                                          \
	    esac;                                                           \
	    baseurl=$${JENKINS_URL:-https://build.hpdd.intel.com/}job/daos-stack/job/$$repo/job/$$branch/; \
	    baseurl+=$$build_number/artifact/artifacts/$$distro/;           \
            add_repos+=" --repo $$baseurl";                                 \
        done;                                                               \
	distro_repos="";                                                    \
	for repo_key in $(SUSE_REPO_KEYS); do                               \
	    $(call install_gpg_key,$$repo_key);                             \
	done;                                                               \
	for repo in $($(DISTRO_BASE)_LOCAL_REPOS)                           \
	            $($(DISTRO_BASE)_REPOS); do                             \
		distro_repos+=" --repo $$repo";                             \
	    $(call install_gpg_key,$$repo/repodata/repomd.xml.key);         \
	done;                                                               \
	sudo build $(BUILD_OPTIONS) $$add_repos                             \
	     $$distro_repos                                                 \
	     --dist $(DISTRO_ID) $(RPM_BUILD_OPTIONS) $(SRPM)
endif

docker_chrootbuild:
	docker build --build-arg UID=$$(id -u) -t $(BUILD_OS)-chrootbuild \
	             -f packaging/Dockerfile.$(BUILD_OS) .
	docker run --privileged=true -w $$PWD/../.. -v=$$PWD/../..:$$PWD/../..              \
	           -it $(BUILD_OS)-chrootbuild bash -c "make -C utils/rpms chrootbuild"

rpmlint: $(SPEC)
	rpmlint $<

packaging_check:
	if grep -e --repo $(CALLING_MAKEFILE); then                                    \
	    echo "SUSE repos in $(CALLING_MAKEFILE) don't need a \"--repo\" any more"; \
	    exit 2;                                                                    \
	fi
	if ! diff --exclude \*.sw?                              \
	          --exclude debian                              \
	          --exclude .git                                \
	          --exclude Jenkinsfile                         \
	          --exclude libfabric.spec                      \
	          --exclude Makefile                            \
	          --exclude README.md                           \
	          --exclude _topdir                             \
	          --exclude \*.tar.\*                           \
	          --exclude \*.code-workspace                   \
	          --exclude install                             \
	          -bur $(PACKAGING_CHECK_DIR)/ packaging/; then \
	    exit 1;                                             \
	fi

check-env:
ifndef DEBEMAIL
	$(error DEBEMAIL is undefined)
endif
ifndef DEBFULLNAME
	$(error DEBFULLNAME is undefined)
endif

test:
	@echo "No test defined for this module"

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

show_makefiles:
	@echo $(MAKEFILE_LIST)

show_calling_makefile:
	@echo $(CALLING_MAKEFILE)

.PHONY: srpm rpms debs deb_detar ls chrootbuild rpmlint FORCE        \
        show_version show_release show_rpms show_source show_sources \
        show_targets check-env
