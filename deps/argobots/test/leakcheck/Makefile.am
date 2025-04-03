# -*- Mode: Makefile; -*-
#
# See COPYRIGHT in top-level directory.
#

TESTS = \
	affinity \
	barrier \
	cond \
	eventual \
	future \
	init_finalize \
	key \
	mutex \
	pool \
	rwlock \
	sched \
	thread \
	timer \
	unit \
	xstream \
	xstream_barrier

check_PROGRAMS = $(TESTS)
noinst_PROGRAMS = $(TESTS)

noinst_LTLIBRARIES = librtrace.la

librtrace_la_SOURCES = rtrace.c
librtrace_la_LIBADD = @ABT_RT_LDFLAGS@

if ABT_CONFIG_DISABLE_ERROR_CHECK
# Do not compile rtrace with -DABT_RT_USE_DLVSYM
librtrace_la_CFLAGS =
else
librtrace_la_CFLAGS = @ABT_RT_CFLAGS@
endif
librtrace_la_LDFLAGS = $(all_libraries)
# librtrace_la_CFLAGS = -fpic -shared

include $(top_srcdir)/test/Makefile.mk

LDADD += librtrace.la

testing:
