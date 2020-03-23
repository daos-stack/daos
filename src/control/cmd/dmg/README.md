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
[client config file](/utils/config/daos.yml).

For more details on how certificates are used within DAOS, see the
[Security documentation](/src/control/security/README.md#certificate-usage-in-daos).

## Communications

The management tool is a gRPC client and uses the
[management API](/src/control/client) to interact concurrently with
[`daos_server`](/src/control/cmd/daos_server/README.md) processes
running listening gRPC servers that host the control service.

Communications take place over the management network and addresses of
remote storage servers to connect to can be specified on the commandline
or in the [client config file](/utils/config/daos.yml).

For details on how gRPC communications are secured and authenticated, see the
[Security documentation](/src/control/security/README.md#host-authentication-with-certificates).

## Functionality

The functionality provided by the management tool is split into
domains which map to individual subcommands.

For exhaustive subcommand listings please see output of
`dmg [<subcommand>] --help`.

### Network

- scan, retrieves details of available network devices.

### Pool

- create, creates a DAOS storage pool across targets in the system.
- destroy, destroys a given pool identified by its UUID.
- query, queries details of given pool identified by its UUID.
- set and get prop, manages update and retrieval of DAOS storage pool
properties.

### Storage

- scan, retrieve storage device details relevant to DAOS configuration.
- prepare, provision storage devices for use with DAOS.
- format, format allocated storage for use with DAOS.
- query, gather detailed information on specific hardware devices.
- set, set property on a given hardware device.

### System

- query, list system members/ranks that have previously joined.
- stop, perform controlled shutdown on system members/ranks.
- start, start previously stopped system members/ranks.
