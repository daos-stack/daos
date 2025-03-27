EXTRA_DIST = autogen.sh Makefile.unx make.inc Makefile.nmake isa-l_crypto.def LICENSE README.md Doxyfile
CLEANFILES =
LDADD =
AM_MAKEFLAGS = --no-print-directory
noinst_HEADERS =
pkginclude_HEADERS = include/test.h include/types.h
noinst_LTLIBRARIES =
INCLUDE = -I $(srcdir)/include/

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libisal_crypto.pc
EXTRA_DIST += libisal_crypto.pc.in
CLEANFILES += libisal_crypto.pc

lsrc=
src_include=
extern_hdrs=
other_src=
check_tests=
unit_tests=
perf_tests=
unit_tests_extra=
perf_tests_extra=
examples=
other_tests=
lsrc32=
lsrc_x86_64=
lsrc_x86_32=
lsrc_aarch64=
lsrc_base_aliases=
unit_tests32=
perf_tests32=

# Include units
include sha1_mb/Makefile.am
include mh_sha1/Makefile.am
include md5_mb/Makefile.am
include sha256_mb/Makefile.am
include sha512_mb/Makefile.am
include mh_sha1_murmur3_x64_128/Makefile.am
include mh_sha256/Makefile.am
include rolling_hash/Makefile.am
include sm3_mb/Makefile.am

if CPU_X86_64
include aes/Makefile.am
endif

if CPU_X86_32
include aes/Makefile.am
endif

# LIB version info not necessarily the same as package version
LIBISAL_CURRENT=2
LIBISAL_REVISION=23
LIBISAL_AGE=0

lib_LTLIBRARIES = libisal_crypto.la
pkginclude_HEADERS += $(sort ${extern_hdrs})
libisal_crypto_la_SOURCES = ${lsrc}
if CPU_X86_64
libisal_crypto_la_SOURCES += ${lsrc_x86_64}
endif

if CPU_X86_32
libisal_crypto_la_SOURCES += ${lsrc_x86_32}
endif

if CPU_AARCH64
libisal_crypto_la_SOURCES += ${lsrc_aarch64}
endif

if CPU_UNDEFINED
libisal_crypto_la_SOURCES += ${lsrc_base_aliases}
endif

nobase_include_HEADERS = isa-l_crypto.h
libisal_crypto_la_LDFLAGS = $(AM_LDFLAGS) \
	-version-info $(LIBISAL_CURRENT):$(LIBISAL_REVISION):$(LIBISAL_AGE)
libisal_crypto_la_LIBADD = ${noinst_LTLIBRARIES}

EXTRA_DIST += ${other_src}
EXTRA_DIST += Release_notes.txt

# For tests
LDADD += libisal_crypto.la
check_PROGRAMS = ${check_tests}
TESTS = ${check_tests}

# For additional tests
EXTRA_PROGRAMS = ${unit_tests}
EXTRA_PROGRAMS += ${perf_tests}
EXTRA_PROGRAMS += ${other_tests}
EXTRA_PROGRAMS += ${examples}
CLEANFILES += ${EXTRA_PROGRAMS}

perfs: ${perf_tests}
tests: ${unit_tests}
checks: ${check_tests}
other: ${other_tests}
perf: $(addsuffix .run,$(perf_tests))
ex: ${examples}
test: $(addsuffix .run,$(unit_tests))

# Build rule to run tests
%.run: %
	$<
	@echo Completed run: $<

# Support for yasm/nasm
if INTEL_CET_ENABLED
  export CET_LD=$(LD)
endif
if USE_YASM
if INTEL_CET_ENABLED
  as_filter = ${srcdir}/tools/yasm-cet-filter.sh
else
  as_filter = ${srcdir}/tools/yasm-filter.sh
endif
endif
if USE_NASM
if INTEL_CET_ENABLED
  as_filter = ${srcdir}/tools/nasm-cet-filter.sh
else
  as_filter = ${srcdir}/tools/nasm-filter.sh
endif
endif
if CPU_AARCH64
  as_filter = $(CC) -D__ASSEMBLY__
endif
CCAS = $(as_filter)
EXTRA_DIST += tools/yasm-filter.sh tools/nasm-filter.sh

AM_CFLAGS = ${my_CFLAGS} ${INCLUDE} $(src_include) ${D}
AM_CCASFLAGS = ${yasm_args} ${INCLUDE} $(src_include) ${DEFS} ${D}

.asm.s:
	@echo "  MKTMP   " $@;
	@cp $< $@

# Generate isa-l_crypto.h
BUILT_SOURCES = isa-l_crypto.h
CLEANFILES += isa-l_crypto.h
isa-l_crypto.h:
	@echo 'Building $@'
	@echo ''			>> $@
	@echo '/**'			>> $@
	@echo ' *  @file isa-l_crypto.h'>> $@
	@echo ' *  @brief Include for ISA-L_crypto library'	>> $@
	@echo ' */'			>> $@
	@echo ''			>> $@
	@echo '#ifndef _ISAL_CRYPTO_H_'	>> $@
	@echo '#define _ISAL_CRYPTO_H_'	>> $@
	@echo ''			>> $@
	@echo '#define.ISAL_CRYPTO_MAJOR_VERSION.${VERSION}' |  ${AWK} -F . '{print $$1, $$2, $$3}' >> $@
	@echo '#define.ISAL_CRYPTO_MINOR_VERSION.${VERSION}' |  ${AWK} -F . '{print $$1, $$2, $$4}' >> $@
	@echo '#define.ISAL_CRYPTO_PATCH_VERSION.${VERSION}' |  ${AWK} -F . '{print $$1, $$2, $$5}' >> $@
	@echo '#define ISAL_CRYPTO_MAKE_VERSION(maj, min, patch)  ((maj) * 0x10000 + (min) * 0x100 + (patch))' >> $@
	@echo '#define ISAL_CRYPTO_VERSION ISAL_CRYPTO_MAKE_VERSION(ISAL_CRYPTO_MAJOR_VERSION, ISAL_CRYPTO_MINOR_VERSION, ISAL_CRYPTO_PATCH_VERSION)' >> $@
	@echo ''			>> $@
	@for unit in $(sort $(extern_hdrs)); do echo "#include <isa-l_crypto/$$unit>" | sed -e 's;include/;;' >> $@; done
	@echo '#endif //_ISAL_CRYPTO_H_'	>> $@

doc: isa-l_crypto.h
	(cat Doxyfile; echo 'PROJECT_NUMBER=${VERSION}') | doxygen -
	$(MAKE) -C generated_doc/latex &> generated_doc/latex_build_api.log
	cp generated_doc/latex/refman.pdf isa-l_crypto_api_${VERSION}.pdf
