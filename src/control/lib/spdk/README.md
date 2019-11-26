# Go language bindings for the SPDK API

This is a Go interface for [SPDK](https://github.com/spdk/spdk) which is also a work in progress.

To clone the 18.07 SPDK branch:

    git clone --single-branch --branch v18.07.x git@github.com:spdk/spdk.git

## Current Status
  * Initial support will be for NVMe driver utilities.

Using these bindings assumes SPDK shared lib is installed in `${SPDK_REPO}/install/lib/libspdk.so`.
In order to use some of the SPDK API, please also follow [Hugepages and Device Binding](https://github.com/spdk/spdk#hugepages-and-device-binding).

### How to build binding requirements

Setup environment:

    export GOSPDK=${DAOS_ROOT}/src/control/lib/spdk
    export SPDK_LIB=${DAOS_ROOT}/opt/spdk
    export LD_LIBRARY_PATH=${SPDK_LIB}/build/lib:${SPDK_LIB}/include:${GOSPDK}/spdk:${LD_LIBRARY_PATH}
    export CGO_CFLAGS="-I${SPDK_LIB}/include"
    export CGO_LDFLAGS="-L${SPDK_LIB}/build/lib -lspdk"

Build NVMe libs:

    cd ${GOSPDK}
    gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -Werror -g -Wshadow -Wall -Wno-missing-braces -c -fpic -Iinclude src/*.c -lspdk
    gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -shared -o libnvme_control.so *.o

Build go spdk bindings:

    cd ${GOSPDK}
    sudo CGO_LDFLAGS=${CGO_LDFLAGS} CGO_CFLAGS=${CGO_CFLAGS} go build -v -i

Run Go Unit Tests:

    cd ${GOSPDK}
    go test -v

cmocka Test Env:

    export DISCOVER_WRAPS="-Wl,--wrap=spdk_nvme_probe -Wl,--wrap=collect -Wl,--wrap=cleanup -Wl,--wrap=init_ret"
    export TESTLIBS="-lspdk -lcmocka -lnvme_control"
    export GCCFLAGS="-g -fpic -Wall -Werror -Wshadow -Wno-missing-braces"
    export TESTFLAGS="${CGO_LDFLAGS} ${CGO_CFLAGS} ${GCCFLAGS} ${TESTLIBS}"

Build cmocka Tests:

    cd ${GOSPDK}/ctests
    gcc ${TESTFLAGS} ${DISCOVER_WRAPS} -I../include -L../. nvme_discover_test.c ../src/*.c -o discover_ctest
