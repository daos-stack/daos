#
#   libconfigini - an ini formatted configuration parser library
#   Copyright (C) 2013-present Taner YILMAZ <taner44 AT gmail.com>
#

LIB = configini

LIBARCH = $(patsubst %,lib%.a,${LIB})

HDRS = src/configini.h src/queue.h
SRCS = src/configini.c

INSTALLHDRS = src/configini.h

OBJS := ${SRCS:.c=.o}

# library installation directory
INSTALLDIR = /usr/local


CFLAGS = -g -Wall -Wno-char-subscripts

CC = gcc

###################################################################################################

ifdef installdir
	INSTALLDIR = $(installdir)
endif


.PHONY: all install test clean doc help

all: $(LIBARCH)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(LIBARCH): $(OBJS) 
	ar rcs $(LIBARCH) $(OBJS)

install: all
	@mkdir -p $(INSTALLDIR)/include && cp $(INSTALLHDRS) $(INSTALLDIR)/include
	@mkdir -p $(INSTALLDIR)/lib     && cp $(LIBARCH) $(INSTALLDIR)/lib

test: $(LIBARCH)
	$(MAKE) -C tests/

clean:
	rm -f ~core~ *.stackdump $(OBJS) $(LIBARCH)
	rm -rf html/
	$(MAKE) -C tests/ clean

doc: docs/html.dox
	@mkdir -p html
	@doxygen docs/html.dox
	
	
help:
	@echo "Installdir: $(INSTALLDIR)"
	@echo "targets:"
	@echo "   all                 Build all"
	@echo "   installdir=<path>   Install library to path"
	@echo "   install             Install library to $INSTALLDIR/lib and its header to $INSTALLDIR/include"
	@echo "   test                Run unittests"
	@echo "   clean               Clean library generated files"
	@echo "   doc                 Generate documentation (as doxygen)"
	
