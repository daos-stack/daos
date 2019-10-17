# Modify FIO_DIR and DAOS_DIR to the root path of fio and daos, respectively
# Assume that daos and fio are already built in the same machine
# ${FIO_DIR}/config-host.h is generated during the build process of fio

FIO_DIR := /home/mschaara/source/deps_daos/daos/_build.external/fio
DAOS_DIR := /home/mschaara/install/daos
CART_DIR := /home/mschaara/install/deps_daos/cart

SRC := daos_fio.c
BIN := daos_fio

CC := gcc
FLAGS := -rdynamic -Wl,-z,nodelete -fPIC
LDFLAGS := -shared -L${CART_DIR}/lib -L${DAOS_DIR}/lib -ldl -ldaos -ldfs -ldaos_common -lgurt -luuid
INCLUDES := -I${FIO_DIR} -include ${FIO_DIR}/config-host.h -I${DAOS_DIR}/include -I${CART_DIR}/include
DEFINES := -D_GNU_SOURCE


build_sync: 
	${CC} -c ${FLAGS} ${INCLUDES} ${DEFINES} ${SRC} -o ${BIN}.o
	${CC} ${BIN}.o ${LDFLAGS} -o ${BIN}

# async version is compiled but has not been tested yet
build_async: 
	${CC} -c ${FLAGS} ${INCLUDES} ${DEFINES} ${SRC2} -o ${BIN2}.o
	${CC} ${BIN2}.o ${LDFLAGS} -o ${BIN2}

clean: 
	rm -rf ${BIN} ${BIN2}
	rm -rf ${BIN}.o ${BIN2}.o

