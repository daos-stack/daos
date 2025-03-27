dnl
dnl Copyright (c) 2016 Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2019 Intel, Inc.  All rights reserved.
dnl Copyright (c) 2019-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
dnl
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([libpsm3-fi], m4_normalize(m4_esyscmd([sed 's/_/./g' VERSION])))
AC_CONFIG_SRCDIR([src/psmx3.h])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR(config)
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([1.11 dist-bzip2 no-dist-gzip foreign -Wall -Werror subdir-objects parallel-tests tar-pax])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])])
dnl --- m4_include(config/fi_check_package.m4)
m4_include(config/fi_strip_optflags.m4)

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
   AC_MSG_ERROR([libfabric only builds on Linux, OS X, and FreeBSD])
   ;;
esac

AM_CONDITIONAL([MACOS], [test "x$macos" = "x1"])
AM_CONDITIONAL([LINUX], [test "x$linux" = "x1"])
AM_CONDITIONAL([FREEBSD], [test "x$freebsd" = "x1"])

# Remove -Wundef as workaround for CUDA __cplusplus bug
#base_c_warn_flags="-Wall -Wundef -Wpointer-arith"
base_c_warn_flags="-Wall -Wpointer-arith"
debug_c_warn_flags="-Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"
debug_c_other_flags="-fstack-protector-strong"
picky_c_warn_flags="-Wno-long-long -Wmissing-prototypes -Wstrict-prototypes -Wcomment -pedantic"

AC_ARG_WITH([build_id],
    [AS_HELP_STRING([--with-build_id],
        [Enable build_id annotation @<:@default=no@:>@])],
    [],[with_build_id=no])
AS_IF([test x"$with_build_id" = x"no"], [with_build_id=""])
AC_DEFINE_UNQUOTED([BUILD_ID],["$with_build_id"],
                   [adds build_id to version if it was defined])

# Override autoconf default CFLAG settings (e.g. "-g -O2") while still
# allowing the user to explicitly set CFLAGS=""
: ${CFLAGS="${base_c_warn_flags}"}

# AM_PROG_AS would set CFLAGS="-g -O2" by default if not set already so it
# should not be called earlier
AM_PROG_AS()

# AM PROG_AR did not exist pre AM 1.11.x (where x is somewhere >0 and
# <3), but it is necessary in AM 1.12.x.
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_DEFINE([HAVE_PSM3], [1], [Build libfabric PSM3 provider])
AC_DEFINE([HAVE_PSM3_DL], [1], [Build libfabric PSM3 provider])

dnl Default flags
CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE -D_SVID_SOURCE -D_BSD_SOURCE"

AC_ARG_ENABLE([psm3-src],
    [AS_HELP_STRING([--enable-psm3-src],
        [Enable Monolithic provider @<:@default=yes@:>@])
    ],[],[enable_psm3_src=yes])
AC_ARG_WITH([psm3-headers],
    [AS_HELP_STRING([--with-psm3-headers=DIR],
        [Provide path to where the psm headers are installed for split build. @<:@default=no@:>@])
    ],[], [with_psm3_headers="no"])
AS_IF([test "x$enable_psm3_src" != "xno"], [psm3_src=1], [psm3_src=0])
AM_CONDITIONAL([HAVE_PSM3_SRC], [test "x$enable_psm3_src" != "xno"], [build PSM3 src into provider])
AC_DEFINE_UNQUOTED([HAVE_PSM3_SRC], $psm3_src, [PSM3 source is built-in])

AS_IF([test "$psm3_src" -eq 0],
      [
        AS_IF([test "$with_psm3_headers" != "" && test "$with_psm3_headers" != "no"],
              [
                CPPFLAGS="$CPPFLAGS -I$with_psm3_headers"
                AC_CHECK_HEADER(psm2.h, [],
                AC_MSG_ERROR([PSM3 Headers requested but <psm2.h> not found.]))
              ])
      ])

dnl ------------ HALs
AC_ARG_ENABLE([psm3-verbs],
    [AS_HELP_STRING([--enable-psm3-verbs],
        [Enable PSM3 support on Verbs HAL (UD QPs) @<:@default=yes@:>@])
    ],[],[enable_psm3_verbs=yes])
AC_ARG_ENABLE([psm3-sockets],
    [AS_HELP_STRING([--enable-psm3-sockets],
        [Enable PSM3 support on Sockets HAL (TCP) @<:@default=yes@:>@])
    ],[],[enable_psm3_sockets=yes])

PSM3_HAL_INST=""
PSM3_HAL_CNT=0
AS_IF([test "x$enable_psm3_verbs" = "xyes"],
      [
        PSM3_HAL_INST="$PSM3_HAL_INST verbs"
        PSM3_HAL_CNT=$((PSM3_HAL_CNT+1))
        CPPFLAGS="$CPPFLAGS -DPSM_VERBS"
      ])
AS_IF([test "x$enable_psm3_sockets" = "xyes"],
      [
        PSM3_HAL_INST="$PSM3_HAL_INST sockets"
        PSM3_HAL_CNT=$((PSM3_HAL_CNT+1))
        CPPFLAGS="$CPPFLAGS -DPSM_SOCKETS"
      ])
PSM3_HAL_INST=${PSM3_HAL_INST# }

dnl ------------- HAL Extensions
AC_ARG_ENABLE([psm3-udp],
    [AS_HELP_STRING([--enable-psm3-udp],
        [EXPERIMENTAL: Enable UDP on applicable HALs @<:@default=no@:>@])
    ],[],[enable_psm3_udp=no])
AC_ARG_ENABLE([psm3-rc],
    [AS_HELP_STRING([--enable-psm3-rc],
        [EXPERIMENTAL: Enable User Space RC QPs on applicable HALs @<:@default=check [check means match --enable-psm3-verbs option]@:>@])
    ],[],[enable_psm3_rc=check])
AS_IF([test "x$enable_psm3_udp" = "xyes"],
      [
       AS_IF([test "x$enable_psm3_sockets" = "xyes"],
             [CPPFLAGS="$CPPFLAGS -DUSE_UDP"],
             [AC_MSG_ERROR([UDP requires Scokets HAL active])])
      ])
dnl RC requires the Verbs HAL, so if Verbs HAL is disabled, then this should default to match verbs HAL
AS_IF([test "x$enable_psm3_rc" = "xcheck"],
      [enable_psm3_rc=$enable_psm3_verbs])
AS_IF([test "x$enable_psm3_rc" = "xyes"],
      [
       AS_IF([test "x$enable_psm3_verbs" = "xyes"],
             [CPPFLAGS="$CPPFLAGS -DUSE_RC"],
             [AC_MSG_ERROR([User RC QPs requires Verbs HAL active])])
      ])
AS_IF([test "x$enable_psm3_src" = "xyes"],
      [
        AC_SEARCH_LIBS([shm_open], [rt], [], [AC_MSG_ERROR([unable to find shm_open() in librt])])
        AC_SEARCH_LIBS([dlopen], [dl], [], [AC_MSG_ERROR([unable to find dlopen() in libdl])])
        AC_SEARCH_LIBS([numa_node_of_cpu], [numa], [], [AC_MSG_ERROR([unable to find numa_node_of_cpu() in libnuma])])
        AS_IF([test "x$enable_psm3_verbs" = "xyes"],
              [AC_SEARCH_LIBS([ibv_get_device_list], [ibverbs], [], [AC_MSG_ERROR([unable to find ibv_get_device_list() in libibverbs])])])
        AC_SEARCH_LIBS([uuid_parse], [uuid], [], [AC_MSG_ERROR([unable to find uuid_parse() in libuuid])])


        AS_IF([test ! -z "$PSM2_MOCK_TESTING"], [CPPFLAGS="$CPPFLAGS -DPSM2_MOCK_TESTING=1"])
        AS_IF([test ! -z "$PSM_FI"], [CPPFLAGS="$CPPFLAGS -DPSM_FI"])
        AS_IF([test ! -z "$PSM_DEBUG"],
              [
                CFLAGS="$CFLAGS -O0 -g3"
                CPPFLAGS="$CPPFLAGS -DPSM_DEBUG -D_HFI_DEBUGGING -funit-at-a-time"
              ],[CFLAGS="$CFLAGS -O3 -g3"])

        AS_IF([test ! -z "$PSM_COVERAGE"],
              [
                CFLAGS="$CFLAGS -O -fprofile-arcs -ftest-coverage"
                LDFLAGS="$LDFLAGS -fprofile-arcs"
              ])

        AS_IF([test ! -z "$PSM_LOG"],
              [
                CPPFLAGS="$CPPFLAGS -DPSM_LOG"
                AS_IF([test ! -z "$PSM_LOG_FAST_IO"], [CPPFLAGS="$CPPFLAGS -DPSM_LOG"])
              ])
        AS_IF([test ! -z "$PSM_PERF"], [CPPFLAGS="$CPPFLAGS -DRDPMC_PERF_FRAMEWORK"])
        AS_IF([test ! -z "$PSM_HEAP_DEBUG"], [CPPFLAGS="$CPPFLAGS -DPSM_HEAP_DEBUG"])
        AS_IF([test ! -z "$PSM_PROFILE"], [CPPFLAGS="$CPPFLAGS -DPSM_PROFILE"])

        AS_IF([test ! -z "$PSM_HAL_INST"], [PSM3_HAL_INST="$PSM_HAL_INST"])
        AS_IF([test ! -z "$PSM_HAL_CNT"], [PSM3_HAL_CNT="$PSM_HAL_CNT"])
      ])

AM_CONDITIONAL([HAVE_PSM3_ADDITIONAL_GLOBALS], [test ! -z "$PSM3_ADDITIONAL_GLOBALS"])
AM_COND_IF([HAVE_PSM3_ADDITIONAL_GLOBALS], [PSM3_ADDITIONAL_GLOBALS="$PSM3_ADDITIONAL_GLOBALS"])


psm3_happy=1
AC_ARG_WITH([valgrind],
    AS_HELP_STRING([--with-valgrind],
        [Enable valgrind annotations @<:@default=no@:>@]))
if test "$with_valgrind" != "" && test "$with_valgrind" != "no"; then
   AC_DEFINE([INCLUDE_VALGRIND], 1,
        [Define to 1 to enable valgrind annotations])
   if test -d $with_valgrind; then
      CPPFLAGS="$CPPFLAGS -I$with_valgrind/include"
   fi
fi

AC_ARG_ENABLE([atomics],
   [AS_HELP_STRING([--enable-atomics],
      [Enable atomics support @<:@default=yes@:>@])
   ],
   [],
   [enable_atomics=yes])

dnl Checks for programs
AC_PROG_CC_C99
AS_IF([test "$ac_cv_prog_cc_c99" = "no"],
      [AC_MSG_WARN([Libfabric requires a C99-compliant compiler])
       AC_MSG_ERROR([Cannot continue])])
AM_PROG_CC_C_O
AC_PROG_CPP

AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],[Enable debugging @<:@default=no@:>@])],
    [],[enable_debug=no])

AS_IF([test x"$enable_debug" != x"no"],
      [dbg=1
       # See if the flags in $debug_c_warn_flags and $debug_c_other_flags work
       good_flags=
       CFLAGS_save="$CFLAGS"
       for flag in $debug_c_warn_flags $debug_c_other_flags; do
           AC_MSG_CHECKING([to see if compiler supports $flag])
           CFLAGS="$flag $CFLAGS_save"
           AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[int i = 3;]])],
                        [AC_MSG_RESULT([yes])
               good_flags="$flag $good_flags"],
              [AC_MSG_RESULT([no])])
       done
       debug_c_flags=$good_flags
       unset good_flags

       CFLAGS="-g -O0 ${base_c_warn_flags} ${debug_c_flags} ${CFLAGS_save}"
       unset CFLAGS_save],
      [dbg=0
       CFLAGS="-DNDEBUG $CFLAGS"])

AC_DEFINE_UNQUOTED([ENABLE_DEBUG],[$dbg],
                   [defined to 1 if libfabric was configured with --enable-debug, 0 otherwise])

AC_ARG_ENABLE([profile],
              [AS_HELP_STRING([--enable-profile],
                              [Enable profiling @<:@default=no@:>@])],
              [],
              [enable_profile=no])

AS_IF([test x"$enable_profile" != x"no"],
      [AC_DEFINE([HAVE_FABRIC_PROFILE], [1],
                 [defined to 1 if libfabric was configured with --enable-profile, 0 otherwise])
])

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

m4_map([FI_ARG_ENABLE_SANITIZER],[
       [asan, address, Address],
       [lsan, leak, Leak],
       [tsan, thread, Thread],
       [ubsan, undefined, UndefinedBehavior]
])

dnl Checks for header files.
AC_HEADER_STDC

dnl Check for compiler features
AC_C_TYPEOF

LT_INIT
LT_OUTPUT

dnl dlopen support is optional
AC_ARG_WITH([dlopen],
    AS_HELP_STRING([--with-dlopen],
        [dl-loadable provider support @<:@default=yes@:>@]),
    )

if test "$freebsd" = "0"; then
AS_IF([test x"$with_dlopen" != x"no"], [
AC_CHECK_LIB(dl, dlopen, [],
    AC_MSG_ERROR([dlopen not found.  libfabric requires libdl.]))
])
fi

dnl handle picky option
AC_ARG_ENABLE([picky],
    [AS_HELP_STRING([--enable-picky],
                    [Enable developer-level compiler pickyness when building @<:@default=no@:>@])])
AS_IF([test x"$enable_picky" = x"yes" && test x"$GCC" = x"yes"],
      [AS_IF([test x"$enable_debug" = x"no"],
             [CFLAGS="${base_c_warn_flags} ${debug_c_flags} ${picky_c_warn_flags} $CFLAGS"],
             [CFLAGS="${picky_c_warn_flags} $CFLAGS"])
      ])

dnl Checks for libraries
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread_mutex_init() not found.  libfabric requires libpthread.]))

AC_CHECK_FUNC([pthread_spin_init],
   [have_spinlock=1],
   [have_spinlock=0])

dnl shm_open not used in the common code on os-x

AC_DEFINE_UNQUOTED([PT_LOCK_SPIN], [$have_spinlock],
   [Define to 1 if pthread_spin_init is available.])

AC_ARG_ENABLE([epoll],
    [AS_HELP_STRING([--disable-epoll],
        [Disable epoll if available@<:@default=no@:>@])],
    [],
    [enable_epoll=auto]
)

AS_IF([test x"$enable_epoll" != x"no"],
    [AC_CHECK_FUNCS([epoll_create])
     if test "$ac_cv_func_epoll_create" = yes; then
        AC_DEFINE([HAVE_EPOLL], [1], [Define if you have epoll support.])
     fi]
)

AC_CHECK_HEADER([linux/perf_event.h],
    [AC_CHECK_DECL([__builtin_ia32_rdpmc],
        [
            AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <linux/perf_event.h>]],
                [[__builtin_ia32_rdpmc(0);]])],
                [linux_perf_rdpmc=1],
                [linux_perf_rdpmc=0])
   ],
        [linux_perf_rdpmc=0],
        [#include <linux/perf_event.h>])],
    [linux_perf_rdpmc=0])
AC_DEFINE_UNQUOTED(HAVE_LINUX_PERF_RDPMC, [$linux_perf_rdpmc],
    [Whether we have __builtin_ia32_rdpmc() and linux/perf_event.h file or not])
AM_CONDITIONAL([HAVE_LINUX_PERF_RDPMC], [test "x$linux_perf_rdpmc" = "x1"])

dnl Check for gcc atomic intrinsics
AS_IF([test x"$enable_atomics" != x"no"],
    AC_MSG_CHECKING(compiler support for c11 atomics)
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdatomic.h>]], [[atomic_int a;
         atomic_init(&a, 0);
         #ifdef __STDC_NO_ATOMICS__
           #error c11 atomics are not supported
         #else
           return 0;
         #endif
        ]])],[
        AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_ATOMICS, 1, [Set to 1 to use c11 atomic functions])
        ],[AC_MSG_RESULT(no)])


    AC_MSG_CHECKING(compiler support for c11 atomic `least` types)
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdatomic.h>]],[[atomic_int_least32_t a;
         atomic_int_least64_t b;
        ]])],[
            AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_ATOMICS_LEAST_TYPES, 1,
                      [Set to 1 to use c11 atomic `least` types])
        ],[
            AC_MSG_RESULT(no)
        ]),
[
    AC_MSG_RESULT(configure: atomics support for c11 is disabled)
])

dnl Check for gcc built-in atomics
AS_IF([test x"$enable_atomics" != x"no"],
    AC_MSG_CHECKING(compiler support for built-in atomics)
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdint.h>]],[[int32_t a;
         __sync_add_and_fetch(&a, 0);
         __sync_sub_and_fetch(&a, 0);
         #if defined(__PPC__) && !defined(__PPC64__)
           #error compiler built-in atomics are not supported on PowerPC 32-bit
         #else
           return 0;
         #endif
        ]])],[
        AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_BUILTIN_ATOMICS, 1, [Set to 1 to use built-in intrincics atomics])
        ],[AC_MSG_RESULT(no)]),
[
    AC_MSG_RESULT(configure: atomics support built-in is disabled)
])

dnl Check for gcc memory model aware built-in atomics
dnl If supported check to see if not internal to compiler
LIBS_save=$LIBS
AC_SEARCH_LIBS([__atomic_load_8], [atomic])
AS_IF([test x"$enable_atomics" != x"no"],
    AC_MSG_CHECKING(compiler support for built-in memory model aware atomics)
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdint.h>]],
        [[uint64_t d;
         uint64_t s;
         uint64_t c;
         uint64_t r;
          r = __atomic_fetch_add(&d, s, __ATOMIC_SEQ_CST);
          r = __atomic_load_8(&d, __ATOMIC_SEQ_CST);
          __atomic_exchange(&d, &s, &r, __ATOMIC_SEQ_CST);
          __atomic_compare_exchange(&d,&c,&s,0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
         #if defined(__PPC__) && !defined(__PPC64__)
           #error compiler built-in memory model aware atomics are not supported on PowerPC 32-bit
         #else
           return 0;
         #endif
        ]])],
        [
            AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_BUILTIN_MM_ATOMICS, 1, [Set to 1 to use built-in intrinsics memory model aware atomics])
        ],
        [
            AC_MSG_RESULT(no)
            LIBS=$LIBS_save
        ]),
[
    AC_MSG_RESULT(configure: -latomic key is disabled)
    LIBS=$LIBS_save
])
unset LIBS_save

dnl Check for 128-bit integer support
AC_CHECK_TYPE([__int128],
	[AC_DEFINE(HAVE___INT128, 1, [Set to 1 to use 128-bit ints])])

dnl Check for gcc cpuid intrinsics
AC_MSG_CHECKING(compiler support for cpuid)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
     #include <stddef.h>
     #include <cpuid.h>]], [[
     int a, b, c, d;
     __cpuid_count(0, 0, a, b, c, d);
    ]])],[
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_CPUID, 1, [Set to 1 to use cpuid])
    ],[AC_MSG_RESULT(no)])

if test "$with_valgrind" != "" && test "$with_valgrind" != "no"; then
AC_CHECK_HEADER(valgrind/memcheck.h, [],
    AC_MSG_ERROR([valgrind requested but <valgrind/memcheck.h> not found.]))
fi

AC_CACHE_CHECK(whether ld accepts --version-script, ac_cv_version_script,
    [if test -n "`$LD --help < /dev/null 2>/dev/null | grep version-script`"; then
        ac_cv_version_script=yes
    else
        ac_cv_version_script=no
    fi])

AC_ARG_ENABLE([embedded],
         [AS_HELP_STRING([--enable-embedded],
               [Enable embedded support (turns off symbol versioning) @<:@default=no@:>@])
         ],
         [ac_asm_symver_support=0
               icc_symver_hack=1],
         [enable_embedded=no])
AM_CONDITIONAL([EMBEDDED], [test x"$enable_embedded" = x"yes"])

AM_CONDITIONAL(HAVE_LD_VERSION_SCRIPT, test "$ac_cv_version_script" = "yes")

dnl Disable symbol versioning when -ipo is in CFLAGS or ipo is disabled by icc.
dnl The gcc equivalent ipo (-fwhole-program) seems to work fine.
AS_CASE([$CFLAGS],
   [*-ipo*],[
      AC_MSG_NOTICE([disabling symbol versioning support with -ipo CFLAG])
      icc_symver_hack=1
      ac_asm_symver_support=0
   ],
   []
)

dnl Check for symbol versioning compiler + linker support.
dnl If icc + ipo, then print disabled and skip check
AC_MSG_CHECKING(for .symver assembler support)
AS_IF([test "$icc_symver_hack"],
   [AC_MSG_RESULT(disabled)],
[

AC_LINK_IFELSE([AC_LANG_PROGRAM([[__asm__(".symver main_, main@ABIVER_1.0");]], [[]])],[
      AC_MSG_RESULT(yes)
      ac_asm_symver_support=1
   ],[
      AC_MSG_RESULT(no)
      ac_asm_symver_support=0
   ])

]) dnl AS_IF icc_symver_hack

dnl Disable in psm3 to include all symbols without symver
dnl AC_DEFINE_UNQUOTED([HAVE_SYMVER_SUPPORT], [$ac_asm_symver_support],
AC_DEFINE_UNQUOTED([HAVE_SYMVER_SUPPORT], [0],
           [Define to 1 if compiler/linker support symbol versioning.])

AC_MSG_CHECKING(for __alias__ attribute support)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
      int foo(int arg);
      int foo(int arg) { return arg + 3; };
      int foo2(int arg) __attribute__ (( __alias__("foo")));
   ]], [[ foo2(1); ]])],[
      AC_MSG_RESULT(yes)
      ac_prog_cc_alias_symbols=1
   ],[
      AC_MSG_RESULT(no)
      ac_prog_cc_alias_symbols=0
   ])
dnl Disable in psm3 to include all symbols without symver
dnl AC_DEFINE_UNQUOTED([HAVE_ALIAS_ATTRIBUTE], [$ac_prog_cc_alias_symbols],
AC_DEFINE_UNQUOTED([HAVE_ALIAS_ATTRIBUTE], [0],
           [Define to 1 if the linker supports alias attribute.])
AC_CHECK_FUNCS([getifaddrs])

dnl Check for ethtool support
AC_MSG_CHECKING(ethtool support)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <net/if.h>
    #include <sys/types.h>
    #include <linux/ethtool.h>
    #include <linux/sockios.h>
    #include <sys/ioctl.h>]], [[
        unsigned long ioctl_req = SIOCETHTOOL;
        struct ethtool_cmd cmd = {
            .cmd = ETHTOOL_GSET,
        };
        long speed = cmd.speed;
    ]])],[
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_ETHTOOL, 1, [Set to 1 to use ethtool])

    ],[AC_MSG_RESULT(no)])

dnl Check for ethtool SPEED_UNKNOWN macro (suppoirted in the linux
dnl kernel >= 3.2) and ethtool_cmd_speed function declarations
dnl supported in the linux kernel >= 2.6.26
AC_CHECK_DECLS([ethtool_cmd_speed, SPEED_UNKNOWN], [], [],
               [#include <linux/ethtool.h>])

dnl Check for userfault fd support
have_uffd=0
AC_CHECK_HEADERS([linux/userfaultfd.h],
   [AC_CHECK_DECL([__NR_userfaultfd],
      [have_uffd=1],
      [],
      [[#include <sys/syscall.h>]])],
   [], [])

AS_IF([test $have_uffd -eq 1],
   [AC_MSG_CHECKING([for userfaultfd unmap support])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
         #include <sys/types.h>
         #include <linux/userfaultfd.h>
         #include <unistd.h>
         #include <sys/syscall.h>
         #include <fcntl.h>
         #include <sys/ioctl.h>
      ]],
      [[
         int fd;
         struct uffdio_api api_obj;
         api_obj.api = UFFD_API;
         api_obj.features = UFFD_FEATURE_EVENT_UNMAP |
               UFFD_FEATURE_EVENT_REMOVE |
               UFFD_FEATURE_EVENT_REMAP;
         fd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
         return ioctl(fd, UFFDIO_API, &api_obj);
      ]])
   ],
   [AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])
      have_uffd=0])])

AC_DEFINE_UNQUOTED([HAVE_UFFD_UNMAP], [$have_uffd],
   [Define to 1 if platform supports userfault fd unmap])

dnl Check support to intercept syscalls
AC_CHECK_HEADERS_ONCE(elf.h sys/auxv.h)

dnl Check support to clock_gettime
have_clock_gettime=0

AC_SEARCH_LIBS([clock_gettime],[rt],
         [have_clock_gettime=1],
         [])

AC_DEFINE_UNQUOTED(HAVE_CLOCK_GETTIME, [$have_clock_gettime],
       [Define to 1 if clock_gettime is available.])
AM_CONDITIONAL(HAVE_CLOCK_GETTIME, [test $have_clock_gettime -eq 1])

dnl Check for CUDA runtime libraries
AC_ARG_WITH([cuda],
    [AS_HELP_STRING([--with-cuda=DIR],
        [Enable CUDA build and fail if not found.
         Optional=<Path to where the CUDA libraries
         and headers are installed.>])],
    [],[with_cuda=no])
AS_IF([test ! -z "$PSM_CUDA"], [with_cuda=/usr/local/cuda])
have_cuda=0
AS_IF([test x"$with_cuda" != x"no"],
      [
        _FI_CHECK_PACKAGE_HEADER([cuda],
                [cuda_runtime.h],
                [$with_cuda],
                [have_cuda=1],
                [AC_MSG_ERROR([CUDA headers not found. Cannot enable CUDA DL])])
      ])

AS_IF([test x"$with_cuda" != x"no" && test -n "$with_cuda" && test "$have_cuda" = "0" ],
    [AC_MSG_ERROR([CUDA support requested but CUDA runtime not available.])])

AC_DEFINE_UNQUOTED([ENABLE_CUDA_DLOPEN], [$have_cuda], [CUDA Support])
AC_DEFINE_UNQUOTED([HAVE_CUDA], [$have_cuda], [CUDA support])
AC_DEFINE_UNQUOTED([PSM3_CUDA], [$have_cuda], [CUDA support])

AS_IF([test "$have_cuda" = "1" && test x"$with_cuda" != x"yes"],
      [CPPFLAGS="$CPPFLAGS -DPSM_CUDA -DNVIDIA_GPU_DIRECT $cuda_CPPFLAGS"
       LDFLAGS="$LDFLAGS $cuda_LDFLAGS"
       LIBS="$LIBS $cuda_LIBS"])

dnl Check for Level Zero libraries.
AC_ARG_WITH([oneapi-ze],
    [AS_HELP_STRING([--with-oneapi-ze=DIR],
        [Enable OneAPI Level Zero (ZE) build and fail if not found.
         Optional=<Path to where the ZE libraries and headers are installed.>])],
    [],[with_oneapi_ze=no])
have_oneapi_ze=0
AS_IF([test x"$with_oneapi_ze" != x"no"],
      [
        _FI_CHECK_PACKAGE_HEADER([ze],
                [level_zero/ze_api.h],
                [$with_oneapi_ze],
                [have_oneapi_ze=1],
                [AC_MSG_ERROR([OneAPI ZE headers not found. Cannot enable ZE DL])])
      ])

have_drm=0
have_drm_sles=0
AS_IF([test "$have_oneapi_ze" != ""],
      [AC_CHECK_HEADER(drm/i915_drm.h, [have_drm=1])])
AS_IF([test "$have_oneapi_ze" != "" -a "$have_drm" != "1"],
      [AC_CHECK_HEADER(libdrm/i915_drm.h, [have_drm_sles=1])])

AS_IF([test x"$with_oneapi_ze" != x"no" && test -n "$with_oneapi_ze" && test "$have_oneapi_ze" = "0" ],
      [AC_MSG_ERROR([ZE support requested but ZE runtime not available.])])

dnl Check for pidfd support
have_pidfd=0
AC_MSG_CHECKING([for pidfd support])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
         #include <sys/types.h>
         #include <unistd.h>
         #include <sys/syscall.h>
      ]],
      [[
         int fd;
	 int pidfd;
	 int rem_fd;
         fd = syscall(__NR_pidfd_getfd, pidfd, rem_fd, 0);
      ]])
   ],
   [
      AC_MSG_RESULT([yes])
      have_pidfd=1
   ],
   [AC_MSG_RESULT([no])])

AC_DEFINE_UNQUOTED([HAVE_ZE], [$have_oneapi_ze], [ZE support])
AC_DEFINE_UNQUOTED([ENABLE_ZE_DLOPEN], [$have_oneapi_ze], [ZE Support])
AC_DEFINE_UNQUOTED([PSM3_ONEAPI], [$have_oneapi_ze], [ZE support])
AC_DEFINE_UNQUOTED([HAVE_DRM], [$have_drm], [i915 DRM header])
AC_DEFINE_UNQUOTED([HAVE_LIBDRM], [$have_drm_sles], [i915 DRM header (SLES)])

have_oneapi_zeMemPutIpcHandle=0
AS_IF([test "$have_oneapi_ze" = "1"],
    [
        CPPFLAGS="$CPPFLAGS -DPSM_ONEAPI $ze_CPPFLAGS"
        AS_IF([test "$have_drm" = "1"], [CPPFLAGS="$CPPFLAGS -DHAVE_DRM"])
        AS_IF([test "$have_drm_sles" = "1"], [CPPFLAGS="$CPPFLAGS -DHAVE_LIBDRM"])
        AS_IF([test "$have_pidfd" = "1"], [CPPFLAGS="$CPPFLAGS -DPSM_HAVE_PIDFD"])

        LDFLAGS="$LDFLAGS $ze_LDFLAGS"
        LIBS="$LIBS $ze_LIBS"

        dnl - Check for zeMemPutIpcHandle after ZE added to LIBS/*FLAGS
        AC_MSG_CHECKING([for zeMemPutIpcHandle support in level-zero])
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[
                    #include <level_zero/ze_api.h>
                ]],[[
                    ze_context_handle_t hContext;
                    ze_ipc_mem_handle_t handle;
                    (void)zeMemPutIpcHandle(hContext, handle);
                ]])
            ],[
                AC_MSG_RESULT(yes)
                have_oneapi_zeMemPutIpcHandle=1
                CPPFLAGS="$CPPFLAGS -DPSM_HAVE_ONEAPI_ZE_PUT_IPCHANDLE"
            ],[
                AC_MSG_RESULT(no)
            ])
    ])

AS_IF([test "x$with_oneapi_ze" != "xno" && test "x$with_cuda" != "xno" ],
      [AC_MSG_ERROR([PSM3 only supports one accelerator API at a time:
                    oneAPI Level Zero ($with_oneapi_ze | $have_oneapi_ze) or CUDA ($with_cuda | $have_cuda])],
      [])

AC_PROG_GREP()

AC_MSG_CHECKING([for -Wno-address-of-packed-member support])
save_CFLAGS="$CFLAGS"
dnl - Invalid flag will only show on failure for some reason
CFLAGS="-Wno-address-of-packed-member -Werror"
AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM([#error "Error on purpose"])],   [],
   [
      dnl - Check if flag is in error message
      if $GREP 'Wno-address-of-packed-member' conftest.err >/dev/null; then
         AC_MSG_RESULT(no)
         CFLAGS="$save_CFLAGS"
      else
         AC_MSG_RESULT(yes)
         CFLAGS="$save_CFLAGS -Wno-address-of-packed-member"
      fi
   ])

dnl ------------- DSA
AC_ARG_ENABLE([psm3-dsa],
        [AS_HELP_STRING([--enable-psm3-dsa],
                        [Enable support for Intel Data Streaming Accelerator (DSA) @<:@default=check@:>@])],
        [], [enable_psm3_dsa=check])
psm3_dsa_happy=0
AS_IF([test "x$enable_psm3_dsa" != "xno"],
      [
        _FI_CHECK_PACKAGE_HEADER([psm3_dsa],
                                 [linux/idxd.h],
                                 [],
                                 [psm3_dsa_found=1],
                                 [psm3_dsa_found=0])
        AS_IF([test $psm3_dsa_found -ne 1 && test "x$enable_psm3_dsa" == "xyes"],
              [
                psm3_happy=0
                AC_MSG_ERROR([DSA Support requested but linux/idxd.h not found.])
              ])
        AS_IF([test "$psm3_dsa_found" -eq 1],
              [
                AC_MSG_CHECKING([for full Intel DSA support])
                AC_COMPILE_IFELSE(
                    [AC_LANG_PROGRAM(
                        [[#include <linux/idxd.h>]],[[
                        struct dsa_hw_desc desc = {};
                        struct dsa_completion_record c = {};
                        desc.opcode = DSA_OPCODE_MEMMOVE;
                        desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CC
                            | IDXD_OP_FLAG_BOF;
                        desc.xfer_size = 0;
                        desc.src_addr = 0;
                        desc.dst_addr = 0;
                        desc.completion_addr = (uintptr_t)&c;
                        while (c.status == DSA_COMP_NONE) ;
                        if (c.status != DSA_COMP_SUCCESS) {
                            if ((c.status & DSA_COMP_STATUS_MASK) != DSA_COMP_PAGE_FAULT_NOBOF) {
                                return -1;
                            } else  if (c.status & DSA_COMP_STATUS_WRITE) {
                                return 0;
                            }
                        }
                        return 0;
                        ]])
                    ],[
                        AC_MSG_RESULT(yes)
                        psm3_dsa_happy=1
                        CPPFLAGS="$CPPFLAGS -DPSM_DSA"
                    ],[
                        AC_MSG_RESULT(no)
                        AS_IF([test "x$enable_psm3_dsa" == "xyes"],
                              [
                                psm3_happy=0
                                AC_MSG_ERROR([Missing full DSA Support in linux/idxd.h.])
                              ])
                    ])
              ])
      ])
dnl ------------- UMR
psm3_umr_happy=0
AC_ARG_ENABLE([psm3-umr-cache],
    [AS_HELP_STRING([--enable-psm3-umr-cache],
                    [Enable support for Userspace Memory Region (UMR) Caching @<:@default=check@:>@])],
    [], [enable_psm3_umr_cache=check])
AS_IF([test "x$enable_psm3_umr_cache" != "xno"],
      [
        AS_IF([test $have_uffd -ne 1 && test "x$enable_psm3_umr_cache" == "xyes"],
              [
                AC_MSG_ERROR([UMR Support requested but linux/userfaultfd.h not found.])
              ])
        AS_IF([test $have_uffd -eq 1],
              [
                psm3_umr_happy=1
                CPPFLAGS="$CPPFLAGS -DUMR_CACHE"
              ])
      ])

dnl ------------- hwloc
AC_ARG_ENABLE([psm3-hwloc],
        [AS_HELP_STRING([--enable-psm3-hwloc],
                        [Enable PSM3 use of hwloc for NIC affinity selections @<:@default=check@:>@])],
        [], [enable_psm3_hwloc=check])
psm3_hwloc_happy=0
AS_IF([test "x$enable_psm3_hwloc" != "xno"],
      [
        FI_CHECK_PACKAGE([psm3_hwloc],
                         [hwloc.h],
                         [hwloc],
                         [hwloc_topology_init],
                         [],
                         [$psm3_PREFIX],
                         [$psm3_LIBDIR],
                         [psm3_hwloc_found=1],
                         [psm3_hwloc_found=0])
        AS_IF([test $psm3_hwloc_found -ne 1 && test "x$enable_psm3_hwloc" == "xyes"],
              [
                psm3_happy=0
                AC_MSG_ERROR([hwloc Support requested but hwloc headers and/or library not found.])
              ])
        AS_IF([test "$psm3_hwloc_found" -eq 1],
              [
                  psm3_hwloc_happy=1
                  CPPFLAGS="$CPPFLAGS $psm3_hwloc_CPPFLAGS -DPSM_USE_HWLOC"
                  LDFLAGS="$LDFLAGS $psm3_hwloc_LDFLAGS"
                  LIBS="$LIBS $psm3_hwloc_LIBS"
              ])
      ])

dnl ------------- Driver Modules
psm3_rv_happy=0
AC_ARG_WITH([psm3-rv],
            [AS_HELP_STRING([--with-psm3-rv],
                            [Enable PSM3 support for RV module (MR caching, GPU Direct, and Kernel RC QPs) @<:@default=check@:>@])],
       [psm3_rv_check=0],
       [psm3_rv_check=1])
AS_IF([test "x$with_psm3_rv" != "xno"],
      [
        AS_IF([test "$psm3_rv_check" -eq 1],
              [with_psm3_rv=/usr/include])
        save_CPPFLAGS=$CPPFLAGS
        CPPFLAGS="$CPPFLAGS -I$with_psm3_rv"
        _FI_CHECK_PACKAGE_HEADER([psm3_rv],
                                 [rdma/rv_user_ioctls.h],
                                 [],
                                 [psm3_rv_happy=1],
                                 [psm3_rv_happy=0])

        CPPFLAGS=$save_CPPFLAGS
        AS_IF([test "$psm3_rv_happy" -eq 0],
              [
                AS_IF([test "$psm3_rv_check" -eq 0], [
                    psm3_happy=0
                    AC_MSG_ERROR([RV Module headers requested but rv_user_ioctls.h not found.])
                      ])
              ],[
                AS_IF([test "$psm3_rv_check" -eq 1],
                      [
                        dnl Include under any of the following cases
                        dnl - Verbs
                        dnl - Sockets and CUDA
                        AS_IF([test "x$enable_psm3_sockets" = "xyes" && test "$have_cuda" -eq 1],
                              [psm3_sockets_with_cuda=1],
                              [psm3_sockets_with_cuda=0])
                        AS_IF([test "x$enable_psm3_verbs" = "xyes" || test "$psm3_sockets_with_cuda" -eq 1],
                              [CPPFLAGS="$CPPFLAGS -DRNDV_MOD -I$with_psm3_rv"],
                              [
                                AC_MSG_NOTICE([RV Module headers found, but not included, as current config does not support RV])
                                psm3_rv_happy=0
                              ])
                      ],[
                        CPPFLAGS="$CPPFLAGS -DRNDV_MOD -I$with_psm3_rv"
                      ])
              ])
        AS_IF([test "$psm3_rv_happy" -eq 1],
              [
                AC_MSG_CHECKING([for RV support for ring.overflow_cnt])
                AC_COMPILE_IFELSE(
                    [AC_LANG_PROGRAM(
                        [[#include <sys/types.h>
                          #include <stdint.h>
                          #include <rdma/rv_user_ioctls.h>
                        ]],[[struct rv_ring_header ring; ring.overflow_cnt=0;]])
                    ],[
                        AC_MSG_RESULT(yes)
                    ],[
                        AC_MSG_RESULT(no)
                        CPPFLAGS="$CPPFLAGS -DHAVE_NO_PSM3_RV_OVERFLOW_CNT"
                    ])
              ])
      ])

AS_IF([test "$psm3_rv_happy" -eq 1 && test "$have_oneapi_ze" -eq 1],
      [CPPFLAGS="$CPPFLAGS -DINTEL_GPU_DIRECT"],
      [])
dnl -- Compiler based optimizations
AS_IF([test ! -z "$CC" && ( test "x${CC%% *}" = "xicc" || test "x${CC%% *}" = "xicx" )],
      [ dnl ICC/ICX
        CFLAGS="$CFLAGS -Werror -xATOM_SSE4.2 -DPSM_AVX512 -fpic -fPIC -D_GNU_SOURCE -DPACK_STRUCT_STL=packed,"
        LDFLAGS="$LDFLAGS -Wc,-static-intel"
        PSM3_MARCH="avx2"
      ], [ dnl GCC/other
        CFLAGS="$CFLAGS -Werror -mavx2 -fpic -fPIC -funwind-tables -Wformat -Wformat-security"
        PSM3_MARCH="avx2"
      ])
AC_DEFINE_UNQUOTED([PSM3_MARCH], ["$PSM3_MARCH"], [PSM3 built with instruction set])
AS_IF([test ! -z "$PSM_CPPFLAGS"], [CPPFLAGS="$CPPFLAGS $PSM_CPPFLAGS"], [])
AS_IF([test ! -z "$PSM_CFLAGS"], [CFLAGS="$CFLAGS $PSM_CFLAGS"], [])

dnl Workaround for including fabric.c
AC_DEFINE([HOOK_NOOP_INIT], NULL, [Ignore HOOK_NOOP_INIT])
AC_DEFINE([COLL_INIT], NULL, [Ignore COLL_INIT])
dnl Defines not used in PSM3 provider
AC_DEFINE([HAVE_DMABUF_PEER_MEM], 0, [Ignore HAVE_DMABUF_PEER_MEM])
AC_DEFINE([HAVE_GDRCOPY], 0, [Ignore HAVE_GDRCOPY])
AC_DEFINE([HAVE_HOOK_DEBUG], 0, [Ignore HAVE_HOOK_DEBUG])
AC_DEFINE([HAVE_HOOK_HMEM], 0, [Ignore HAVE_HOOK_HMEM])
AC_DEFINE([HAVE_MEMHOOKS_MONITOR], 0, [Ignore HAVE_MEMHOOKS_MONITOR])
AC_DEFINE([HAVE_NEURON], 0, [Ignore HAVE_NEURON])
AC_DEFINE([HAVE_ROCR], 0, [Ignore HAVE_ROCR])
AC_DEFINE([HAVE_SYNAPSEAI], 0, [Ignore HAVE_SYNAPSEAI])
AC_DEFINE([HAVE_UFFD_MONITOR], 0, [Ignore HAVE_UFFD_MONITOR])
AC_DEFINE([HAVE_XPMEM], 0, [Ignore HAVE_XPMEM])

dnl Provider-specific checks
dnl FI_PROVIDER_INIT
AC_DEFINE([HAVE_BGQ], 0, [Ignore HAVE_BGQ])
AC_DEFINE([HAVE_BGQ_DL], 0, [Ignore HAVE_BGQ_DL])
AC_DEFINE([HAVE_EFA], 0, [Ignore HAVE_EFA])
AC_DEFINE([HAVE_EFA_DL], 0, [Ignore HAVE_EFA_DL])
AC_DEFINE([HAVE_GNI], 0, [Ignore HAVE_GNI])
AC_DEFINE([HAVE_GNI_DL], 0, [Ignore HAVE_GNI_DL])
AC_DEFINE([HAVE_MRAIL], 0, [Ignore HAVE_MRAIL])
AC_DEFINE([HAVE_MRAIL_DL], 0, [Ignore HAVE_MRAIL_DL])
AC_DEFINE([HAVE_NET], 0, [Ignore HAVE_NET])
AC_DEFINE([HAVE_NET_DL], 0, [Ignore HAVE_NET_DL])
AC_DEFINE([HAVE_PERF], 0, [Ignore HAVE_PERF])
AC_DEFINE([HAVE_PSM], 0, [Ignore HAVE_PSM])
AC_DEFINE([HAVE_PSM_DL], 0, [Ignore HAVE_PSM_DL])
AC_DEFINE([HAVE_PSM2], 0, [Ignore HAVE_PSM2])
AC_DEFINE([HAVE_PSM2_DL], 0, [Ignore HAVE_PSM2_DL])
dnl FI_PROVIDER_SETUP([psm3])
AC_DEFINE([HAVE_OPX], 0, [Ignore HAVE_OPX])
AC_DEFINE([HAVE_OPX_DL], 0, [Ignore HAVE_OPX_DL])
AC_DEFINE([HAVE_RSTREAM], 0, [Ignore HAVE_RSTREAM])
AC_DEFINE([HAVE_RSTREAM_DL], 0, [Ignore HAVE_RSTREAM_DL])
AC_DEFINE([HAVE_RXD], 0, [Ignore HAVE_RXD])
AC_DEFINE([HAVE_RXD_DL], 0, [Ignore HAVE_RXD_DL])
AC_DEFINE([HAVE_RXM], 0, [Ignore HAVE_RXM])
AC_DEFINE([HAVE_RXM_DL], 0, [Ignore HAVE_RXM_DL])
AC_DEFINE([HAVE_SHM], 0, [Ignore HAVE_SHM])
AC_DEFINE([HAVE_SHM_DL], 0, [Ignore HAVE_SHM_DL])
AC_DEFINE([HAVE_SM2], 0, [Ignore HAVE_SM2])
AC_DEFINE([HAVE_SM2_DL], 0, [Ignore HAVE_SM2_DL])
AC_DEFINE([HAVE_SOCKETS], 0, [Ignore HAVE_SOCKETS])
AC_DEFINE([HAVE_SOCKETS_DL], 0, [Ignore HAVE_SOCKETS_DL])
AC_DEFINE([HAVE_TRACE], 0, [Ignore HAVE_TRACE])
AC_DEFINE([HAVE_TRACE_DL], 0, [Ignore HAVE_TRACE_DL])
AC_DEFINE([HAVE_TCP], 0, [Ignore HAVE_TCP])
AC_DEFINE([HAVE_TCP_DL], 0, [Ignore HAVE_TCP_DL])
AC_DEFINE([HAVE_UCX], 0, [Ignore HAVE_UCX])
AC_DEFINE([HAVE_UCX_DL], 0, [Ignore HAVE_UCX_DL])
AC_DEFINE([HAVE_UDP], 0, [Ignore HAVE_UDP])
AC_DEFINE([HAVE_UDP_DL], 0, [Ignore HAVE_UDP_DL])
AC_DEFINE([HAVE_USNIC], 0, [Ignore HAVE_USNIC])
AC_DEFINE([HAVE_USNIC_DL], 0, [Ignore HAVE_USNIC_DL])
AC_DEFINE([HAVE_VERBS], 0, [Ignore HAVE_VERBS])
AC_DEFINE([HAVE_VERBS_DL], 0, [Ignore HAVE_VERBS_DL])
dnl FI_PROVIDER_FINI
dnl Configure the .pc file
#FI_PROVIDER_SETUP_PC

AC_SUBST(PSM3_HAL_CNT)
AC_SUBST(PSM3_HAL_INST)

AM_COND_IF([HAVE_PSM3_SRC],
    [
        PSM3_IEFS_VERSION="${RELEASE_TAG}"
        AS_IF([test -z "${PSM3_IEFS_VERSION}"], [PSM3_IEFS_VERSION="${PACKAGE_VERSION}$(whoami)"])
        PSM3_IEFS_VERSION=$(echo "${PSM3_IEFS_VERSION}" | tr '.' '_')
        PSM3_GIT_HASH="$(git rev-parse HEAD)"
        RPM_RELEASE=$(echo "${PSM3_IEFS_VERSION}" | cut -d'_' -f5)
        RELEASE_VER=$(echo "${PSM3_IEFS_VERSION}" | cut -d'_' -f1-4 | sed 's/_/./g')
        AS_IF([test x"${RELEASE_VER}" = x"${PACKAGE_VERSION}"], [], [
            AC_MSG_NOTICE([Release Tag does not match VERSION file])
            AC_MSG_NOTICE([${RELEASE_VER} != ${PACKAGE_VERSION}])
            RPM_RELEASE=999
        ])
        PSM3_PROV_VER_MAJOR=$(echo "${PACKAGE_VERSION}" | cut -d'.' -f1)
        PSM3_PROV_VER_MINOR=$(echo "${PACKAGE_VERSION}" | cut -d'.' -f2)
        PSM3_PROV_VER_MAINT=$(echo "${PACKAGE_VERSION}" | cut -d'.' -f3)
        PSM3_PROV_VER_PATCH=$(echo "${PACKAGE_VERSION}" | cut -d'.' -f4)
    ])
AS_IF([test $have_cuda -eq 1], [RPM_RELEASE=${RPM_RELEASE}cuda])
AS_IF([test $have_oneapi_ze -eq 1], [RPM_RELEASE=${RPM_RELEASE}oneapize])

AC_SUBST(PSM3_IEFS_VERSION)
AC_SUBST(PSM3_GIT_HASH)
AC_SUBST(RPM_RELEASE)
AC_SUBST(PSM3_PROV_VER_MAJOR)
AC_SUBST(PSM3_PROV_VER_MINOR)
AC_SUBST(PSM3_PROV_VER_MAINT)
AC_SUBST(PSM3_PROV_VER_PATCH)
AC_SUBST(PSM3_MARCH)
dnl Set during Make.
dnl AC_SUBST(PSM3_BUILD_TIMESTAMP, ["<Unknown>"])
dnl AC_SUBST(PSM3_SRC_CHECKSUM, ["<Unknown>"])

AC_SUBST(PSM3_ADDITIONAL_GLOBALS)

AC_CONFIG_FILES([Makefile libpsm3-fi.spec libpsm3-fi.map libpsm3-fi.pc])
AM_COND_IF([HAVE_PSM3_SRC],
    [AC_CONFIG_FILES([psm3/psm2_hal_inlines_i.h psm3/psm2_hal_inlines_d.h src/psm3_revision.c])])
AC_OUTPUT

dnl Summary output

echo "***"
echo "*** Version: ${PACKAGE_VERSION}-${RPM_RELEASE}"
echo "***"
hals=""
for hal in $PSM3_HAL_INST; do
    hals="$hals, $hal"
done
echo "*** Build with the following HALs: ${hals#, }"
echo "***"
afeatures=""
if test $have_cuda -eq 1; then
    afeatures="$afeatures, CUDA/GPUDirect"
fi
if test $have_oneapi_ze -eq 1; then
    afeatures="$afeatures, OneAPI/LevelZero"
    if test $have_oneapi_zeMemPutIpcHandle -eq 1; then
        afeatures="$afeatures/putIPC"
    fi
    if test $have_pidfd -eq 1; then
        afeatures="$afeatures/pidfd"
    fi
    if test $have_drm -eq 1 || test $have_drm_sles -eq 1; then
        afeatures="$afeatures/i915_drm"
    fi
fi
if test $psm3_rv_happy -eq 1; then
    afeatures="$afeatures, RV Module"
fi
if test $psm3_dsa_happy -eq 1; then
    afeatures="$afeatures, Intel DSA"
fi
if test $psm3_hwloc_happy -eq 1; then
    afeatures="$afeatures, hwloc"
fi
if test "x$enable_psm3_udp" = "xyes"; then
    afeatures="$afeatures, UDP"
fi
if test "x$enable_psm3_rc" = "xyes"; then
    afeatures="$afeatures, Userspace RC QPs"
fi
if test $psm3_umr_happy -eq 1; then
    afeatures="$afeatures, Userspace MR Cache"
fi
echo "*** Additional Features: ${afeatures#, }"
echo "***"
