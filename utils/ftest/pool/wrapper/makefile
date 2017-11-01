BASE = ../../../..

CC = gcc
CFLAGS = -g -Wall
INCLUDES = -I$(BASE)/install/include -I$(BASE)/src/include \
	-I$(BASE)/install -I$(BASE)/sys_test/util
LIBS = -L$(BASE)/install/lib -L. -ldaos -ldaos_common -ldaos_tier -lmpi -luuid \
	 -lcart -lpmem -lpmemobj
UTIL_SOURCES = Setup.c Done.c
UTIL_OBJECTS = $(UTIL_SOURCES:.c=.o)
POOL_SOURCES = SimplePoolTest.c
POOL_OBJECTS = $(POOL_SOURCES:.c=.o)

all: libtestutil SimplePoolTests

libtestutil: $(UTIL_OBJECTS)
	ar  -cr libtestutil.a $(UTIL_OBJECTS)

SimplePoolTests: $(POOL_OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) $(POOL_OBJECTS) libtestutil.a -o $@
.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -I. -c $< -o $@

clean:
	rm -f SimplePoolTests
	rm -f libtestutil.a
	rm -f *.o
	rm -f *.*~
	rm -f *~
