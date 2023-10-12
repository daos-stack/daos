DAOS_SRC    := ${HOME}/work/daos/src
TARGET_ARCH := linux

BINS := crt_opc_decode

CFLAGS := -D_GNU_SOURCE
CFLAGS += -g3 -Og -Werror -g -Wextra -Wshadow -Wall -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-ignored-attributes -Wfree-nonheap-object
CFLAGS += -fpic -fstack-usage -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv -fstack-protector-strong -fstack-clash-protection
CFLAGS += -I/home/ckochhof/work/daos/src/include -I/home/ckochhof/work/daos/build/debug/gcc/src/include -I/home/ckochhof/work/daos/src/gurt -I/home/ckochhof/work/daos/src/include/daos -I/home/ckochhof/daos/install/prereq/debug/argobots/include
CFLAGS += -I/home/ckochhof/work/daos/src/cart

LDFLAGS := -Wl,--enable-new-dtags -pie
LDFLAGS += -Wl,-rpath-link=/home/ckochhof/work/daos/build/debug/gcc/src/cart -Wl,-rpath-link=/home/ckochhof/work/daos/build/debug/gcc/src/gurt -Wl,-rpath-link=/home/ckochhof/work/daos/build/debug/gcc/src/common -Wl,-rpath-link=/home/ckochhof/daos/install/prereq/debug/argobots/lib -Wl,-rpath-link=/home/ckochhof/daos/install/lib64 -Wl,-rpath=/home/ckochhof/daos/install/prereq/debug/argobots/lib -Wl,-rpath=/home/ckochhof/daos/install/lib64
LDFLAGS += -L/home/ckochhof/work/daos/build/debug/gcc/src/cart -L/home/ckochhof/work/daos/build/debug/gcc/src/gurt -L/home/ckochhof/work/daos/build/debug/gcc/src/common -L/home/ckochhof/daos/install/prereq/debug/argobots/lib
LIBS := -lcart -lgurt -lmercury -luuid -lpthread -lm

.PHONY: all
all: ${BINS}

misc.o: ${DAOS_SRC}/common/misc.c
	${CC} -c -o $@ ${CFLAGS} $<

fail_loc.o: ${DAOS_SRC}/common/fail_loc.c
	${CC} -c -o $@ ${CFLAGS} $<

debug.o: ${DAOS_SRC}/common/debug.c
	${CC} -c -o $@ ${CFLAGS} $<

crt_opc_decode.o: crt_opc_decode.c
	${CC} -c -o $@ ${CFLAGS} $<

crt_opc_decode: crt_opc_decode.o debug.o fail_loc.o misc.o
	$(CC) -o $@ $(LDFLAGS) $^ ${LIBS}

.PHONY: clean
clean:
	/bin/rm -f $(BINS) *.o *.su core
