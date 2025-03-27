#
# Copyright (c) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

noinst_LTLIBRARIES         = libucg_builtin.la
libucg_builtin_la_CFLAGS   = $(BASE_CFLAGS)
libucg_builtin_la_CPPFLAGS = $(BASE_CPPFLAGS)

noinst_HEADERS = \
	ops/builtin_ops.h \
	ops/builtin_comp_step.inl \
	plan/builtin_plan.h

libucg_builtin_la_SOURCES = \
	builtin.c \
	ops/builtin_pack.c \
	ops/builtin_data.c \
	ops/builtin_comp.c \
	ops/builtin_control.c \
	plan/builtin_tree.c \
	plan/builtin_neighbor.c \
	plan/builtin_recursive.c \
	plan/builtin_bruck.c \
	plan/builtin_pairwise.c
