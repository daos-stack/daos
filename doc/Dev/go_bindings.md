# Go language bindings for the SPDK API

This is a Go interface for [SPDK](https://github.com/spdk/spdk) which is also a work in progress.

To clone the 18.07 SPDK branch:

    git clone --single-branch --branch v18.07.x git@github.com:spdk/spdk.git

## Current Status
  * Initial support will be for NVMe driver utilities.

Using these bindings assumes SPDK shared lib is installed in `${SPDK_REPO}/build/lib/libspdk.so`.
In order to use some of the SPDK API, please also follow [Hugepages and Device Binding](https://github.com/spdk/spdk#hugepages-and-device-binding).

### How to build binding requirements

Build NVMe libs:

    cd ${DAOS_ROOT}/src/control/lib/spdk
    gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -Werror -g -Wshadow -Wall -Wno-missing-braces -c -fpic -Iinclude src/*.c -lspdk
    gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -shared -o libnvme_control.so *.o

Build go spdk bindings:

    cd ${DAOS_ROOT}/src/control/lib/spdk
    sudo CGO_LDFLAGS=${CGO_LDFLAGS} CGO_CFLAGS=${CGO_CFLAGS} go build -v -i
