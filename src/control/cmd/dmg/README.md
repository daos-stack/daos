# DAOS Management Tool (dmg)

The DAOS Management Tool is a system administration application for DAOS.
Unlike other DAOS client applications that require authentication through
DAOS agent, `dmg` is capable of authenticating directly through local
certificates.

The management tool has limited software dependencies as tasks are
performed on remote servers over RPC and as such is suitable to be run
from a login node.

## Certificates

Local certificate locations can be specified in the
[client config file](/utils/config/daos_control.yml).

For more details on how certificates are used within DAOS, see the
[Security documentation](/src/control/security/README.md#certificate-usage-in-daos).

## Communications

The management tool is a gRPC client and uses the
[management API](/src/control/client) to interact concurrently with
[`daos_server`](/src/control/cmd/daos_server/README.md) processes
running listening gRPC servers that host the control service.

Communications take place over the management network and addresses of
remote storage servers to connect to can be specified on the commandline
or in the [client config file](/utils/config/daos_control.yml).

For details on how gRPC communications are secured and authenticated, see the
[Security documentation](/src/control/security/README.md#host-authentication-with-certificates).

## Functionality

The functionality provided by the management tool is split into
domains which map to individual subcommands.

For exhaustive subcommand listings please see output of
`dmg [<subcommand>] --help`.

### Network

Provides capability to manage and enumerate available network
devices.

### Pool

Provides capability to create, destroy and manage DAOS storage pools
including ability to manage individual properties and pool Access
Control Lists.

### Storage

Provides capability to scan available storage devices, provision
relevant hardware and format allocated storage for use with DAOS.
The capability to run queries on a particular device as well as
setting particular device properties is also provided.

### System

Provides capability to query system members/ranks that have
previously joined the DAOS system. Additionally perform controlled
stop and start on members/ranks recorded in the system membership.
