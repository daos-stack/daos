# DAOS Server

The DAOS Server can be run either as a foreground process to issue hardware
provisioning commands or as a background process (binary name followed by the
"start" subcommand) when it will listen for instructions from the [management
tool](/src/control/cmd/dmg/README.md) and manage bring-up of the DAOS system.
A DAOS Server instance will typically be run on every storage node to
orchestrate the operation of the managed data plane processes.

## Network and Storage Management

The DAOS data plane utilizes two forms of non-volatile storage,
storage class memory (SCM) in the form of persistent memory modules
and NVMe in the form of high-performance SSDs.

The DAOS Server provides capability to provision and manage network
devices and non-volatile storage including the allocation of
resources to data plane instances.

See
[admin guide](https://daos-stack.github.io/admin/deployment/#hardware-provisioning)
for details on hardware provisioning.

## Configuration

A server configuration file which is passed on invocation contains details of
global and per-io-server parameters related to hardware devices and
environments.

### Configuration File

A populated `daos_server` config file is required when starting
`daos_server` process, its location can be specified on the
commandline (`-o` option) or default location
(`<daos install dir>/etc/daos_server.yml`).

Example config files can be found in the
[examples folder](/utils/config/examples).

Config file parameters will be parsed and populated with defaults as
documented in the
[default daos server config](/utils/config/daos_server.yml).

Parameters passed to `daos_server` on the commandline as application
options (excluding environment variables) take precedence over values
specified in config file and for convenience, active parsed config
values are written to the directory where the server config file was
read from or `/tmp/` if that fails.

It is strongly recommended to specify all parameters and environment
for running DAOS servers in the
[server config file](/utils/config/daos_server.yml).

NOTES:
* some environment variables can only be supplied to `daos_io_server`
instances through the server config file:
	* `CRT_PHY_ADDR_STR`, `OFI_INTERFACE`, `OFI_PORT`, `D_LOG_MASK`,
`D_LOG_FILE`
* other environment variables can be specified in config file as
"key=value" strings in the per-server `env_vars` list:
	* these environment variables will be applied to the `daos_io_server`
environment overriding any specified in the environment
used to launch  `daos_io_server` (e.g. using "-x" orterun option)
* while it is very highly recommended to use the server config file
as a means to supply parameters, environment variables not applied
through the config file but specified in the calling environment will
still be present in the environment used to launch `daos_io_server`.

### Logging

Log file and level mask for both data (`daos_io_server`) and control
(`daos_server`) planes can be set in the server config file.

### NVMe/block-device storage

Parameters prefixed with `nvme_` in the per-server section of the
config file determine how NVMe storage will be assigned for use by
DAOS on the storage node.
Examples for NVMe and AIO (emulation) classes including config file
syntax can be found in the
[examples folder](/utils/config/examples).

### SCM/pmem storage

Parameters prefixed with `scm_` in the per-server section of the
config file determine how SCM storage will be assigned for use by
DAOS on the storage node.
Examples for both DCPM and RAM (emulation) SCM classes including
config file syntax can be found in the
[examples folder](/utils/config/examples).

## Certificates

If the system is set up in normal mode (not insecure mode), the server will have
been configured with a certificate with the CommonName "server" to identify
itself and attest the validity of user credentials.

For more details on how certificates are used within DAOS, see the
[Security documentation](/src/control/security/README.md#certificate-usage-in-daos).

For details on how gRPC communications are secured and authenticated, see the
[Security documentation](/src/control/security/README.md#host-authentication-with-certificates).

## Data-Plane Communication

I/O Server processes communicate with the DAOS Server using UNIX Domain Sockets
set up on the storage node.

### UNIX Domain Socket

The default directory used for the UNIX Domain Sockets is
`/var/run/daos_server`. Alternately, a directory may be specified in the server
configuration file. The directory must exist, and the user starting the server
process must have write access to it in order for the server to start up
successfully. The server opens the socket and proceeds to listen for
communications from local I/O server processes.

### dRPC

The protocol used to communicate between the DAOS server and I/O server processes
is [dRPC](/src/control/drpc/README.md). Control and I/O servers are both capable
of acting as dRPC server or client.

## Management Tool Communication

Communications between the [management tool](/src/control/cmd/dmg/README.md)
and the DAOS Control Plane Server occur over the management network, via the
gRPC protocol.
Management requests can be made to operate over resources local to specific
storage nodes or to operate over the distributed DAOS system, in the latter case
the requests are directed at access point servers defined in the server's config
file.

## Functionality

The functions provided by the server include:

- Storage and Network hardware provisioning and formatting.
- Hardware resource allocation between data plane instances.
- Formatting and monitoring of data plane instances.
- Controlled start and stop of managed data plane instances.
- Storage pool management including creation, property handling and destruction.

## Subcommands

`daos_server` supports various subcommands (see `daos_server --help`
for available subcommands) which will perform stand-alone tasks (e.g.
`storage prepare|scan`) or attempt to bring-up the data plane
(`start` subcommand).

### Storage Prepare

Preparing NVMe consists of binding SSD controllers to a driver
(usually UIO or VFIO) which makes it available for communication
via the SPDK framework (and unavailable to the OS).

Preparing SCM involves configuring DCPM modules in AppDirect
memory regions (interleaved mode) in groups of modules local to a
specific socket (NUMA) and resultant nvdimm namespaces are defined a
device identifier (e.g. /dev/pmem0).

See `daos_server storage prepare --help` and the
[admin guide](https://daos-stack.github.io/admin/deployment/#hardware-provisioning) for usage.

### Storage Scan

Device details for any discovered NVMe SSDs accessible through SPDK
on the storage server will be returned when running `storage scan`
subcommand.

Device details for any discovered (Intel) data-centre persistent
memory modules (DCPM modules) on the storage server will be returned
when running `storage scan` subcommand.

See `daos_server storage scan --help` and the
[admin guide](https://daos-stack.github.io/admin/deployment/#storage-selection)
for usage.

### Network Scan

See `daos_server network scan --help`.
