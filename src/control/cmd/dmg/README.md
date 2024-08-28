# DAOS Management Tool (dmg)

The DAOS Management Tool is a system administration application for DAOS.
Unlike other DAOS client applications that require authentication through
DAOS agent, `dmg` is capable of authenticating directly through local
certificates.

The management tool has limited software dependencies as tasks are
performed on remote servers over RPC and as such is suitable to be run
from a login node.

## Commandline interface

The [go-flags](https://github.com/jessevdk/go-flags) package is used
to implement a commandline interface. Command tree is built starting
with top-level commands defined in the `cliOptions` struct defined in
`main.go`, subcommands are then defined in functionality specific
files as described below. Desired behavior on (sub-)command execution
is then defined in the subcommand type's `Execute` method.

## Certificates

Local certificate locations can be specified in the
[client config file](/utils/config/daos_control.yml).

For more details on how certificates are used within DAOS, see the
[Security documentation](/src/control/security/README.md#certificate-usage-in-daos).

## Communications

The management tool is a gRPC client and uses the
[control API](/src/control/lib/control) to interact with multiple
[`daos_server`](/src/control/cmd/daos_server/README.md) processes
in parallel. The control API invokes RPCs to `daos_server` instances,
each of which run a listening gRPC server that hosts the control
service.

Communications take place over the management network and addresses
of remote storage servers to connect to can be specified as a
LLNL-style hostlist (compact representation for a number of
similarly-named hosts, e.g. `foo[1-1024]`, `bar[35,42,55-64]`, etc)
on the commandline or in the
[client config file](/utils/config/daos_control.yml).

For details on how gRPC communications are secured and authenticated,
see the
[Security documentation](/src/control/security/README.md#host-authentication-with-certificates).

## Usage documentation

The `dmg` man page can be automatically updated when adding or
modifying commands by running
`go test -v -run TestDmg_ManPageIsCurrent -args --update`, unit
tests will fail if this isn't run.
Implementation in `man_test.go`.

## Functionality

The functionality provided by the management tool is split into
domains which map to individual subcommands.

For exhaustive subcommand listings please see output of
`dmg [<subcommand>] --help`.

### Config

Provides capability to automatically generate recommended server
config file for a given set of hosts.
Implementation in `auto.go`.

### Container

Provides capability to change the owner of a DAOS container.
Implementation in `cont.go`.

### Firmware

Firmware related capabilities are selectively compiled and may not
be present in all builds. Provided capability to query and update
firmware on NVMe and PMem devices.
Implementation with firmware enabled in `firmware.go`. When
disabled: `firmware_disabled.go`.

### Network

Provides capability to manage and enumerate available network
devices. Implementation in network.go.

### Pool

Provides capability to create, destroy and manage DAOS storage pools
including ability to manage individual properties and pool Access
Control Lists. Online Server Addition related commands provide
capability drain, evict, exclude, extend and reintegrate.
Implementation in `pool.go` and ACL functionality and helpers in
`acl.go`.

### Storage

Provides capability to scan available storage devices, provision
relevant hardware and format allocated storage for use with DAOS.
Implementation in `storage.go`.

The capability to run queries on a particular device as well as
setting particular device properties is also provided.
Sub-commands of `dmg storage query ...` are implemented in
`storage_query.go`.

### System

Provides capability to query system members/ranks that have
previously joined the DAOS system. Additionally perform controlled
stop and start on members/ranks recorded in the system membership.
Implementation in `system.go`.

## Unit tests

Unit tests are provided for each functionality file (filename
suffixed with `_test.go`). Command syntax is verified by tests
that call into helper methods within `command_test`. Command
handlers are automatically checked to verify meaningful output is
provided when the `--json` flag is set.

