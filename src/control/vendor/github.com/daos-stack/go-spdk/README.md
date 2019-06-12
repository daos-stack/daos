[![GoDoc](https://godoc.org/github.com/daos-stack/go-spdk/spdk?status.svg)](https://godoc.org/github.com/daos-stack/go-spdk/spdk)

# Go language bindings for the SPDK API

This is a Go interface for
[SPDK](https://github.com/spdk/spdk) which is also a work in progress. Building this requires a
local SPDK (version 18.07) build, so start there first.

To clone the 18.07 SPDK branch:

    git clone --single-branch --branch v18.07.x git@github.com:spdk/spdk.git

## Current Status
  * Initial support will be for NVMe driver utilities.

## How to Build

This is a [Go](https://golang.orghttps://golang.org/doc/install)
project, so the Go development tools are naturally required. We
recommend the most current Go release available. As of July 2018, the project has been built and tested with Go 1.9.

The following steps assume SPDK shared lib is installed in `${SPDK_REPO}/build/lib/libspdk.so`.
In order to use some of the SPDK API, please also follow [Hugepages and Device Binding](https://github.com/spdk/spdk#hugepages-and-device-binding).

Clone the repo:

    git clone git@github.com:daos-stack/go-spdk.git
    export GOSPDK_REPO=${HOME}/go-spdk

Setup environment:

    export GOPATH=${HOME}/go
    export SPDK_REPO=${HOME}/spdk
    export LD_LIBRARY_PATH=${HOME}/${SPDK_REPO}/build/lib:${HOME}/go-spdk/spdk:${LD_LIBRARY_PATH}
    export CGO_CPPFLAGS="-I${SPDK_REPO}/include"
    export CGO_LDFLAGS=-"-L${SPDK_REPO}/build/lib -lspdk"

Install go-task:

    go get -u -v github.com/go-task/task/cmd/task
    export PATH=${PATH}:${GOPATH}/bin/

Install [golint] (https://github.com/golang/lint):

    go get -u -v golang.org/x/lint/golint

Install [goimport] (https://godoc.org/golang.org/x/tools/cmd/goimports)

    go get -u -v golang.org/x/tools/cmd/goimports

### Build with Taskfile

Run task:

    task main-task

### How to build manually (without Taskfile)

Build NVMe libs:
    
    cd ${GOSPDK_REPO}/spdk
    gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -Werror -g -Wshadow -Wall -Wno-missing-braces -c -fpic -Iinclude src/*.c -lspdk
    gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -shared -o libnvme_control.so *.o

Build go spdk bindings:

    cd ${GOSPDK_REPO}/spdk
    sudo CGO_LDFLAGS=${CGO_LDFLAGS} CGO_CFLAGS=${CGO_CFLAGS} go build -v -i
