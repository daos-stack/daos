NAME        := cart
VERSION     := 0.0.1
RELEASE     := 2
DIST        := $(shell rpm --eval %{dist})
SRPM        := _topdir/SRPMS/$(NAME)-$(VERSION)-$(RELEASE)$(DIST).src.rpm
RPMS        := _topdir/RPMS/x86_64/$(NAME)-$(VERSION)-$(RELEASE)$(DIST).x86_64.rpm           \
	       _topdir/RPMS/x86_64/$(NAME)-devel-$(VERSION)-$(RELEASE)$(DIST).x86_64.rpm     \
	       _topdir/RPMS/x86_64/$(NAME)-tests-$(VERSION)-$(RELEASE)$(DIST).x86_64.rpm     \
	       _topdir/RPMS/x86_64/$(NAME)-debuginfo-$(VERSION)-$(RELEASE)$(DIST).x86_64.rpm
SPEC        := $(NAME).spec
SRC_EXT     := gz
SOURCE0     := $(NAME)-$(VERSION).tar.$(SRC_EXT)
SOURCE1     := scons_local-$(VERSION).tar.$(SRC_EXT)
SOURCES     := _topdir/SOURCES/$(NAME)-$(VERSION).tar.$(SRC_EXT) \
               _topdir/SOURCES/scons_local-$(VERSION).tar.$(SRC_EXT)
TARGETS     := $(RPMS) $(SRPM)

all: $(TARGETS)

%/:
	mkdir -p $@

_topdir/SOURCES/%: % | _topdir/SOURCES/
	rm -f $@
	ln $< $@

%.$(SRC_EXT): %
	rm -f $@
	gzip $<

scons_local-$(VERSION).tar:
	cd scons_local && \
	git archive --format tar --prefix scons_local/ -o ../$@ HEAD

$(NAME)-$(VERSION).tar:
	git archive --format tar --prefix $(NAME)-$(VERSION)/ -o $@ HEAD

# see https://stackoverflow.com/questions/2973445/ for why we subst
# the "rpm" for "%" to effectively turn this into a multiple matching
# target pattern rule
$(subst rpm,%,$(RPMS)): $(SPEC) $(SOURCES)
	rpmbuild -bb --define "%_topdir $$PWD/_topdir" $(SPEC)

$(SRPM): $(SPEC) $(SOURCES)
	rpmbuild -bs --define "%_topdir $$PWD/_topdir" $(SPEC)

srpm: $(SRPM)

$(RPMS): Makefile

rpms: $(RPMS)

ls: $(TARGETS)
	ls -ld $^

mockbuild: $(SRPM) Makefile
	mock $<

rpmlint: $(SPEC)
	rpmlint $<

dist: $(SOURCES)

.PHONY: srpm rpms ls mockbuild rpmlint FORCE dist
