dnl Process this file with autoconf to produce a configure script.
AC_INIT([daos], [1.0.0], [liang.zhen@intel.com])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_LANG([C])

AC_PROG_CC
AM_PROG_CC_C_O

AC_PROG_RANLIB
AC_PROG_MAKE_SET
AC_HEADER_STDC
AC_HEADER_STAT
AC_HEADER_TIME

DAOS_PROG_CC
DAOS_CONFIG_HEADERS

AC_CONFIG_FILES([
	Makefile
	common/Makefile
	dsr/Makefile
	dsr/include/Makefile
	dsr/placement/Makefile
	dsr/tests/Makefile
	include/Makefile
])
AC_OUTPUT
