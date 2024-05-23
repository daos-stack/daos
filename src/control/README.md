# DAOS Control Plane

DAOS operates over two, closely integrated planes, Control and Data.  The Data
plane handles the heavy lifting transport operations while the Control plane
orchestrates process and storage management, facilitating the operation of the
Data plane.

The DAOS Control Plane is written in Go and runs as the DAOS Server
(`daos_server`) process. It is tasked with network and storage hardware
provisioning and allocation in addition to instantiation and management of the
DAOS Data Plane (Engine) processes that run on the same host.

## Code Organization

The control directory contains a "cmd" subdirectory for server, agent, ddb, and
dmg applications. These applications import the control API
(`src/control/lib/control`) or server packages along with peripheral shared
packages common, drpc, fault, logging, and security where necessary to provide
the given features.

Specific library packages can be found in lib/ which provide access to native
storage libraries through language bindings, e.g. lib/spdk or specific
formatting capabilities e.g., lib/hostlist or lib/txtfmt.

The events package provides the golang component of the RAS framework for
receipt of events over dRPC from the DAOS Engine and forwarding of management
service actionable events to the MS leader.

The pbin package provides a framework for forwarding of requests to be executed
by the privileged binary `daos_server_helper` on behalf of `daos_server`.

The provider package contains interface shims to the external environment,
initially just to the Linux operating system.

The system package encapsulates the concept of the DAOS system, and its
associated membership.

## Developer Documentation

Please refer to package-specific README's.

- [server](/src/control/server/README.md)
- [godoc reference](https://godoc.org/github.com/daos-stack/daos/src/control)

## User Documentation

- [online documentation](https://docs.daos.io/latest/)
