# Go language bindings for the SPDK API

Go bindings for the [SPDK](https://github.com/spdk/spdk) native-C library
to facilitate management of NVMe SSDs from an application written in Go.

The bindings require libspdk-devel (or distro equivalent) package to be
installed.
To install please follow steps in the
[SPDK github instructions](https://github.com/spdk/spdk).

These bindings are currently working against SPDK 20.01.2 and DPDK 19.11.6
which are the versions pinned in the DAOS 1.2 release.

This is not a general purpose set of SPDK go bindings but provides a set of
capabilities tailored to the specific needs of DAOS, the NVMe SSD related
features are as follows:

* device discovery (SPDK environment initialization and device probing)
* device firmware update
* VMD enablement and discovery
* format (wipe) of device namespaces

Functionality is exposed through the package's `Env` and `Nvme` public
interfaces.

### How to build these bindings

Using these bindings assumes SPDK shared lib is installed in
`${SPDK_REPO}/build/lib/libspdk.so`, please also follow
[Hugepages and Device Binding](https://github.com/spdk/spdk#hugepages-and-device-binding).

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

    export TESTLIBS="-lspdk -lcmocka -lnvme_control"
    export GCCFLAGS="-g -fpic -Wall -Werror -Wshadow -Wno-missing-braces"
    export TESTFLAGS="${CGO_LDFLAGS} ${CGO_CFLAGS} ${GCCFLAGS} ${TESTLIBS}"

Build cmocka Tests:

    cd ${GOSPDK}/ctests
    gcc ${TESTFLAGS} -I../include -L../. nvme_control_ut.c ../src/*.c -o nvme_control_ctests
