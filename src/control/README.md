# DAOS Control Plane

DAOS operates over two, closely integrated planes, Control and Data.
The Data plane handles the heavy lifting transport operations while
the Control plane orchestrates process and storage management,
facilitating the operation of the Data plane.

The DAOS Control Plane is written in Go. It is tasked with network
and storage hardware provisioning and allocation in addition to
instantiation and management of the DAOS I/O Server processes that
run on the same host.

## Code Organization

The control directory contains a "cmd" subdirectory for server, agent,
and dmg applications. These applications import the client or server
packages along with peripheral shared packages common, drpc, fault,
logging, and security where necessary to provide the given features.

Specific library packages can be found in lib/ which provide access
to native storage libraries through language bindings, e.g. lib/spdk
or specific formatting capabilities e.g., lib/hostlist or lib/txtfmt.

The pbin package provides a framework for forwarding of requests to be
executed by the privileged binary `daos_admin` on behalf of `daos_server`.

The provider package contains interface shims to the external environment,
initially just to the Linux operating system.

The system package encapsulates the concept of the DAOS system, and
it's associated membership.

## Developer Documentation

Please refer to package-specific README's.

- [server](/src/control/server/README.md)
- [godoc reference](https://godoc.org/github.com/daos-stack/daos/src/control)

## User Documentation

- [online documentation](https://daos-stack.github.io/)
