[![GoDoc](https://godoc.org/github.com/daos-stack/go-spdk/spdk?status.svg)](https://godoc.org/github.com/daos-stack/go-spdk/spdk)

# Go language bindings for the SPDK API

This is a Go interface for
[SPDK](https://github.com/spdk/spdk) which is also a work in progress. Building this requires a
local SPDK build, so start there first.

## Current Status
  * Initial support will be for NVMe driver utilities.

## How to Build

This is a [Go](https://golang.orghttps://golang.org/doc/install)
project, so a Go development tools are naturally required. We
recommend the most current Go release available. As of July 2018, the project has been built and tested with Go 1.9.

Setup environment and build. This assumes SPDK shared lib it installed in `/usr/local/lib/libspdk.so`.
In order to use some of the SPDK API, please also follow [Hugepages and Device Binding](https://github.com/spdk/spdk#hugepages-and-device-binding).

    export GOPATH=$HOME/go
    export LD_LIBRARY_PATH=/usr/local/lib
    export PATH=/usr/local/bin:$PATH
    export CGO_CPPFLAGS="-I/usr/local/include"
    export CGO_LDFLAGS=-"-L/usr/local/lib -lspdk"
