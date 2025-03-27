#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

#
# This Makefile performs basic build test of OCF with posix environment.
# It doesn't generate any final executable, but just compiles every
# single *.c file into *.o object, to check if compilation succeeds.
#
# It's intended to be used as part of CI process.
#

OCFDIR=../../
SRCDIR=src/
INCDIR=include/

SRC=$(shell find ${SRCDIR} -name \*.c)
OBJS = $(patsubst %.c, %.o, $(SRC))
CFLAGS = -Wall -Werror -I${INCDIR} -I${SRCDIR}/ocf/env/

all: sync
	$(MAKE) build

build: $(OBJS)

sync:
	@$(MAKE) -C ${OCFDIR} inc O=$(PWD)
	@$(MAKE) -C ${OCFDIR} src O=$(PWD)
	@$(MAKE) -C ${OCFDIR} env O=$(PWD) OCF_ENV=posix

clean:
	@rm -rf $(OBJS)

distclean:
	@rm -rf $(OBJS)
	@rm -rf src/ocf
	@rm -rf include/ocf

.PHONY: all build sync clean distclean
