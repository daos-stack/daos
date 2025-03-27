dnl
dnl Copyright (c) 2016-2017 Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2018-2021 Intel Corporation, Inc.  All rights reserved.
dnl
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([fabtests], [1.22.0], [ofiwg@lists.openfabrics.org])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR(config)
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([1.11 dist-bzip2 foreign -Wall -Werror subdir-objects tar-pax])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CANONICAL_HOST

macos=0
linux=0
freebsd=0

case $host_os in
*darwin*)
	macos=1
	;;
*linux*)
	linux=1
	;;
*freebsd*)
	freebsd=1
	;;
*)
	AC_MSG_ERROR([libfabric only builds on Linux & OS X])
	;;
esac

AM_CONDITIONAL([MACOS], [test $macos -eq 1])
AM_CONDITIONAL([LINUX], [test $linux -eq 1])
AM_CONDITIONAL([FREEBSD], [test $freebsd -eq 1])

base_c_warn_flags="-Wall -Wundef -Wpointer-arith"
debug_c_warn_flags="-Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"
debug_c_other_flags="-fstack-protector-strong"

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],
			[Enable debugging - default NO])],
	[], [enable_debug=no])

AS_IF([test x"$enable_debug" != x"no"],
	[CFLAGS="-g -O0 ${base_c_warn_flags} ${debug_c_warn_flags} ${debug_c_other_flags} $CFLAGS"
	 dbg=1],
	[dbg=0])

AC_DEFINE_UNQUOTED([ENABLE_DEBUG], [$dbg],
	[defined to 1 if configured with --enable-debug])

AC_DEFUN([FI_ARG_ENABLE_SANITIZER],[
        AC_ARG_ENABLE([$1],
                      [AS_HELP_STRING([--enable-$1],
                                      [Enable $3Sanitizer @<:@default=no@:>@])
                      ],
                      [],
                      [enable_$1=no])
        AS_IF([test x"$enable_$1" != x"no"],
              [CFLAGS="-fsanitize=$2 $CFLAGS"])
])

m4_map([FI_ARG_ENABLE_SANITIZER], [
       [asan, address, Address],
       [lsan, leak, Leak],
       [tsan, thread, Thread],
       [ubsan, undefined, UndefinedBehavior]
])

dnl Fix autoconf's habit of adding -g -O2 by default
AS_IF([test -z "$CFLAGS"],
      [CFLAGS="-O2 -DNDEBUG ${base_c_warn_flags}"])

# AM PROG_AR did not exist pre AM 1.11.x (where x is somewhere >0 and
# <3), but it is necessary in AM 1.12.x.
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AM_PROG_LIBTOOL

AC_ARG_WITH([valgrind],
    AS_HELP_STRING([--with-valgrind],
		   [Enable valgrind annotations - default NO]))

if test "$with_valgrind" != "" && test "$with_valgrind" != "no"; then
	AC_DEFINE([INCLUDE_VALGRIND], 1,
		  [Define to 1 to enable valgrind annotations])
	if test -d $with_valgrind; then
		CPPFLAGS="$CPPLFAGS -I$with_valgrind/include"
	fi
fi

dnl Checks for programs
AC_PROG_CC
AM_PROG_CC_C_O

LT_INIT

have_clock_gettime=0

AC_SEARCH_LIBS([clock_gettime],[rt],
	       [have_clock_gettime=1],
	       [])

AC_DEFINE_UNQUOTED(HAVE_CLOCK_GETTIME, [$have_clock_gettime],
		   [Define to 1 if clock_gettime is available.])
AM_CONDITIONAL(HAVE_CLOCK_GETTIME, [test $have_clock_gettime -eq 1])

AC_CHECK_LIB(pthread, pthread_create, [],
    AC_MSG_ERROR([pthread_create() not found.  fabtests requires libpthread.]))

AC_ARG_WITH([libfabric],
            AS_HELP_STRING([--with-libfabric=DIR], [Provide a path to the libfabric installation directory,
                                                    or defaults to the library search path]),
            [AS_IF([test -d $withval/lib64], [fab_libdir="lib64"], [fab_libdir="lib"])
             CPPFLAGS="-I$withval/include $CPPFLAGS"
             LDFLAGS="-L$withval/$fab_libdir $LDFLAGS"],
            [])

dnl Check for CUDA support. Require fabtests to dlopen CUDA runtime.
have_cuda=0
have_cuda_dmabuf=0
AC_ARG_WITH([cuda],
            [AS_HELP_STRING([--with-cuda=DIR],
                            [Provide path to where the CUDA development
                             and runtime libraries are installed.])],
            [AS_IF([test "$freebsd" == "0"],
                   [AC_CHECK_LIB(dl, dlopen, [], [AC_MSG_ERROR([dlopen not found.])])],
                   [])
             CPPFLAGS="-I$withval/include $CPPFLAGS"
             AC_CHECK_HEADER([cuda_runtime.h],
                             [have_cuda=1],
                             [AC_MSG_ERROR([<cuda_runtime.h> not found])])],
            [])

AC_DEFINE_UNQUOTED([HAVE_CUDA_RUNTIME_H], [$have_cuda],
            [Define to 1 if you have <cuda_runtime.h>])

AS_IF([test x"$have_cuda" = x"1"],
      [
            have_cuda_dmabuf=1
            AC_CHECK_DECL([cuMemGetHandleForAddressRange],
		      [],
		      [have_cuda_dmabuf=0],
		      [[#include <cuda.h>]])

            AC_CHECK_DECL([CU_DEVICE_ATTRIBUTE_DMA_BUF_SUPPORTED],
		      [],
		      [have_cuda_dmabuf=0],
		      [[#include <cuda.h>]])

            AC_CHECK_DECL([CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD],
		      [],
		      [have_cuda_dmabuf=0],
		      [[#include <cuda.h>]])
      ])

AC_DEFINE_UNQUOTED([HAVE_CUDA_DMABUF], [$have_cuda_dmabuf], [CUDA dmabuf support])

dnl Check for ROCR support. Require fabtests to dlopen ROCR.
AC_ARG_WITH([rocr],
            [AS_HELP_STRING([--with-rocr=DIR],
                            [Provide path to where the ROCR development
                             and runtime libraries are installed.])],
            [AS_IF([test "$freebsd" == "0"],
                   [AC_CHECK_LIB(dl, dlopen, [], [AC_MSG_ERROR([dlopen not found.])])],
                   [])
             CPPFLAGS="-I$withval/include $CPPFLAGS"
             AC_CHECK_HEADER([hsa/hsa.h],
                             [AC_DEFINE([HAVE_ROCR_RUNTIME_H], [1],
                                        [Define to 1 if you have <hsa/hsa.h>])],
                             [AC_MSG_ERROR([<hsa/hsa.h> not found])])],
            [])

dnl Check for Neuron support.
AC_ARG_WITH([neuron],
            [AS_HELP_STRING([--with-neuron=DIR],
                            [Provide path to where the Neuron development
                             and runtime libraries are installed.])],
            [AS_IF([test "$freebsd" == "0"],
                   [AC_CHECK_LIB(dl, dlopen, [], [AC_MSG_ERROR([dlopen not found.])])],
                   [])
             CPPFLAGS="-I$withval/include $CPPFLAGS"
             AC_CHECK_HEADER([nrt/nrt.h],
                             [AC_DEFINE([HAVE_NEURON], [1],
                                        [Define to 1 if you have <nrt/nrt.h>])],
                             [AC_MSG_ERROR([<nrt/nrt.h> not found])])],
            [])

dnl Check for SynapseAI support. Require fabtests to dlopen SynapseAI runtime.
AC_ARG_WITH([synapseai],
            [AS_HELP_STRING([--with-synapseai=DIR],
                            [Enable SynapseAI build and fail if not found.
                             Optional=<Path to where the SynapseAI libraries
                             and headers are installed.>])],
            [AS_IF([test "$freebsd" == "0"],
                   [AC_CHECK_LIB(dl, dlopen, [], [AC_MSG_ERROR([dlopen not found.])])],
                   [])
             CPPFLAGS="-I$withval/include -I/usr/include/drm -I/usr/include/libdrm $CPPFLAGS"
             AC_CHECK_HEADER([habanalabs/synapse_api.h],
                        [AC_CHECK_HEADER([habanalabs/hlthunk.h],
                              [AC_CHECK_HEADER([habanalabs/scal.h],
                                    [AC_DEFINE([HAVE_SYNAPSEAI], [1], [Define to 1 if you have <synapse_api.h>, <hlthunk.h>, and <scal.h>])],
                                    [AC_MSG_ERROR([<habanalabs/scal.h> not found])],
                                    [-])],
                              [AC_MSG_ERROR([<habanalabs/hlthunk.h> not found])])],
                  [AC_MSG_ERROR([<habanalabs/synapse_api.h> not found])])],
            [])

dnl Checks for libraries
AC_CHECK_LIB([fabric], fi_getinfo, [],
    AC_MSG_ERROR([fi_getinfo() not found.  fabtests requires libfabric.]))

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER([rdma/fabric.h], [],
    [AC_MSG_ERROR([<rdma/fabric.h> not found.  fabtests requires libfabric.])])

AC_ARG_WITH([ze],
            AS_HELP_STRING([--with-ze], [Use non-default ZE location - default NO]),
            AS_IF([test x"$withval" != x"no"],
		  [CPPFLAGS="-I$withval/include $CPPFLAGS"
		   LDFLAGS="-L$withval/$lib $LDFLAGS"]))

dnl Checks for ZE support. Require fabtests to dlopen ZE libraries
have_ze=0
AS_IF([test x"$with_ze" != x"no"],
      [
            AC_CHECK_LIB(dl, dlopen, [], [AC_MSG_ERROR([dlopen not found.])])
            AC_CHECK_HEADER([level_zero/ze_api.h], [have_ze=1])
      ])

AS_IF([test x"$with_ze" != x"no" && test -n "$with_ze" && test "$have_ze" = "0" ],
	[AC_MSG_ERROR([ZE support requested but ZE runtime not available.])],
	[])

AC_DEFINE_UNQUOTED([HAVE_ZE], [$have_ze], [ZE support])

dnl Checks for presence of ZE library. Needed for building dmabuf rdma component tests.
AM_CONDITIONAL([HAVE_ZE_DEVEL], [test $have_ze -eq 1])

dnl Checks for presence of Verbs. Needed for building dmabuf rdma component tests.
have_verbs_devel=0
AC_CHECK_HEADER([infiniband/verbs.h],
		[AC_CHECK_LIB(ibverbs, ibv_reg_dmabuf_mr,
                          [have_verbs_devel=1
                           LIBS="-libverbs $LIBS"])])
AM_CONDITIONAL([HAVE_VERBS_DEVEL], [test $have_verbs_devel -eq 1])

AC_MSG_CHECKING([for fi_trywait support])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <rdma/fi_eq.h>]],
	       [[fi_trywait(NULL, NULL, 0);]])],
	       [AC_MSG_RESULT([yes])],
	       [AC_MSG_RESULT([no])
	        AC_MSG_ERROR([fabtests requires fi_trywait support. Cannot continue])])

if test "$with_valgrind" != "" && test "$with_valgrind" != "no"; then
AC_CHECK_HEADER(valgrind/memcheck.h, [],
    AC_MSG_ERROR([valgrind requested but <valgrind/memcheck.h> not found.]))
fi

AC_CHECK_FUNC([epoll_create1], [have_epoll=1], [have_epoll=0])
AC_DEFINE_UNQUOTED([HAVE_EPOLL], [$have_epoll],
		   [Defined to 1 if Linux epoll is available])

dnl Check for 128-bit integer support
AC_CHECK_TYPE([__int128],
	[AC_DEFINE(HAVE___INT128, 1, [Set to 1 to use 128-bit ints])])

AC_CONFIG_FILES([Makefile fabtests.spec])

AC_OUTPUT
