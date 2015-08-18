AM_CFLAGS=$(DAOS_CFLAGS)
AM_LDFLAGS := -L$(top_builddir)/common -L$(top_builddir)/dsr/placement

LIBDAOS := $(top_builddir)/common/libdaos_common.a \
	   $(top_builddir)/dsr/placement/libdaos_pl.a
DAOS_TEST_SHARE =

noinst_PROGRAMS = pseudo_cluster other
pseudo_cluster_SOURCES = pseudo_cluster.c $(DAOS_TEST_SHARE)
pseudo_cluster_LDFLAGS =
pseudo_cluster_LDADD := $(LIBDAOS)
pseudo_cluster_DEPENDENCIES := $(LIBDAOS)

other_SOURCES = other.c $(DAOS_TEST_SHARE)
other_LDFLAGS =
other_LDADD := $(LIBDAOS)
other_DEPENDENCIES := $(LIBDAOS)
