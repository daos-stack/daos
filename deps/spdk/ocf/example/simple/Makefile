#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

OCFDIR=../../
SRCDIR=src/
INCDIR=include/

SRC=$(shell find ${SRCDIR} -name \*.c)
OBJS = $(patsubst %.c, %.o, $(SRC))
PROGRAM=simple

CC = gcc
CFLAGS = -g -Wall -I${INCDIR} -I${SRCDIR}/ocf/env/
LDFLAGS = -lm -lz -pthread

all: sync
	$(MAKE) $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

sync:
	@$(MAKE) -C ${OCFDIR} inc O=$(PWD)
	@$(MAKE) -C ${OCFDIR} src O=$(PWD)
	@$(MAKE) -C ${OCFDIR} env O=$(PWD) OCF_ENV=posix

clean:
	@rm -rf $(PROGRAM) $(OBJS)

distclean:
	@rm -rf $(PROGRAM) $(OBJS)
	@rm -rf src/ocf
	@rm -rf include/ocf

.PHONY: all clean
