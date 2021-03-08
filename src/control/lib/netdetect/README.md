# Netdetect library

This library provides capability to interact with network devices and retrieve
system hardware topologies through interaction with native-C
[hwloc](https://github.com/open-mpi/hwloc) and
[libfabric](https://ofiwg.github.io/libfabric) libraries from an application
written in Go.

The bindings require hwloc-devel and libfabric-devel (or distro equivalent)
package to be installed.

See the exported methods in
[`netdetect.go`](/src/control/lib/netdetect/netdetect.go) for the public API.
