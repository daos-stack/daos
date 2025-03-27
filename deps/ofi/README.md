[![Build Checks](https://github.com/ofiwg/libfabric/actions/workflows/pr-ci.yml/badge.svg)](https://github.com/ofiwg/libfabric/actions/workflows/pr-ci.yml)
[<img alt="libfabric Coverity scan build status" src="https://scan.coverity.com/projects/4274/badge.svg"/>](https://scan.coverity.com/projects/4274)
[<img alt="libfabric main branch AppVeyor CI status" src="https://ci.appveyor.com/api/projects/status/github/ofiwg/libfabric?svg=true"/>](https://ci.appveyor.com/api/projects/status/github/ofiwg/libfabric)
[![libfabric release version](https://img.shields.io/github/release/ofiwg/libfabric.svg)](https://github.com/ofiwg/libfabric/releases/latest)
[![openssf scorecard](https://api.securityscorecards.dev/projects/github.com/ofiwg/libfabric/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ofiwg/libfabric)

# libfabric

libfabric, also known as Open Fabrics Interfaces (OFI), is a framework focused
on exporting high-performance networking services to applications.  It
specifically targets parallel and distributed applications and middleware.

See [the OFI website](http://libfabric.org) for more details, including a
description and overview of the project, and detailed documentation of the
libfabric APIs.

You can join the libfabric mailing lists from www.openfabrics.org (membership
in the Open Fabrics Alliance is NOT required to join the mailing lists).
libfabric discussions are also available on slack: libfabric.slack.com.

## Installing pre-built libfabric packages

On OS X, the latest release of libfabric can be installed using the
[Homebrew](https://github.com/Homebrew/homebrew) package manager using the
following command:

```bash
$ brew install libfabric
```

Libfabric pre-built binaries may be available from other sources, such as Linux
distributions.

## Building and installing libfabric from source

Distribution tarballs are available from the Github
[releases](https://github.com/ofiwg/libfabric/releases) tab.

If you are building libfabric from a developer git clone, you must first run
the `autogen.sh` script. This will invoke the GNU Autotools to bootstrap
libfabric's configuration and build mechanisms. If you are building libfabric
from an official distribution tarball, there is no need to run `autogen.sh`;
libfabric distribution tarballs are already bootstrapped for you.

Libfabric currently supports GNU/Linux, Free BSD, and OS X.

### Configure options

The `configure` script has many built-in options (see `./configure --help`).
Some useful options are:

```
--prefix=<directory>
```

By default `make install` will place the files in the `/usr` tree.
The `--prefix` option specifies that libfabric files should be installed into
the tree specified by named `<directory>`. The executables will be located at
`<directory>/bin`.

```
--with-valgrind=<directory>
```

Directory where valgrind is installed. If valgrind is found, then valgrind
annotations are enabled. This may incur a performance penalty.

```
--enable-debug
```

Enable debug code paths. This enables various extra checks and allows for using
the highest verbosity logging output that is normally compiled out in
production builds.

```
--enable-<provider>=[yes|no|auto|dl|<directory>]
--disable-<provider>
```

This enables or disables the provider named `<provider>`. Valid options are:
- auto (This is the default if the `--enable-<provider>` option isn't specified)

  The provider will be enabled if all of its requirements are satisfied. If one
  of the requirements cannot be satisfied, then the provider is disabled.
- yes (This is the default if the `--enable-<provider>` option is specified)

  The configure script will abort if the provider cannot be enabled (e.g., due
  to some of its requirements not being available.
- no

  Disable the provider. This is synonymous with `--disable-<provider>`.
- dl

  Enable the provider and build it as a loadable library.
- \<directory\>

  Enable the provider and use the installation given in `<directory>`.

### Examples

Consider the following example:

```bash
$ ./configure --prefix=/opt/libfabric --disable-sockets && make -j 32 && sudo make install
```
This will tell libfabric to disable the `sockets` provider, and install
libfabric in the `/opt/libfabric` tree. All other providers will be enabled if
possible and all debug features will be disabled.

Alternatively:

```bash
$ ./configure --prefix=/opt/libfabric --enable-debug --enable-psm3=dl && make -j 32 && sudo make install
```

This will tell libfabric to enable the `psm3` provider as a loadable library,
enable all debug code paths, and install libfabric to the `/opt/libfabric`
tree. All other providers will be enabled if possible.


## Validate installation

The fi_info utility can be used to validate the libfabric and provider
installation and provide details about provider support and available
interfaces.  See `fi_info(1)` man page for details on using the fi_info
utility.  fi_info is installed as part of the libfabric package.

A more comprehensive test package is available via the fabtests package.


## Providers

### opx

***

The OPX provider is an updated Libfabric provider for Omni-Path HPC
fabrics. The other provider for Omni-Path is PSM2.

The OPX provider began as a fork of the libfabric BGQ provider, with the
hardware-specific parts re-written for the Omni-Path hfi1 fabric
interface card. Therefore OPX inherits several desirable characteristics
of the BGQ driver, and analysis of instruction counts and cache line
footprints of most HPC operations show OPX being lighter weight than
PSM2 on the host software stack, leading to better overall performance.

See the `fi_opx(7)` man page for more details. See [Cornelis Customer
Center](https://customercenter.cornelisnetworks.com/) for support information.

### psm2

***

The `psm2` provider runs over the PSM 2.x interface that is supported
by the Intel Omni-Path Fabric. PSM 2.x has all the PSM 1.x features plus a set
of new functions with enhanced capabilities. Since PSM 1.x and PSM 2.x are not
ABI compatible, the `psm2` provider only works with PSM 2.x and doesn't support
Intel TrueScale Fabric.

See the `fi_psm2(7)` man page for more details.

### psm3

***

The `psm3` provider provides optimized performance and scalability for most
verbs UD and sockets devices. Additional features and optimizations can be
enabled when running over Intel's E810 Ethernet NICs and/or using Intel's
rendezvous kernel module ([`rv`](https://github.com/intel/iefs-kernel-updates)).
PSM 3.x fully integrates the OFI provider and the underlying PSM3
protocols/implementation and only exports the OFI APIs.

See [`fi_psm3`(7)](https://ofiwg.github.io/libfabric/main/man/fi_psm3.7.html) for more details.

### rxm

***

The `ofi_rxm` provider is an utility provider that supports RDM endpoints emulated
over MSG endpoints of a core provider.

See [`fi_rxm`(7)](https://ofiwg.github.io/libfabric/main/man/fi_rxm.7.html) for more information.

### sockets

***

The sockets provider has been deprecated in favor of the tcp, udp, and
utility providers, which provide improved performance and stability.

The `sockets` provider is a general-purpose provider that can be used on any
system that supports TCP sockets.  The provider is not intended to provide
performance improvements over regular TCP sockets, but rather to allow
developers to write, test, and debug application code even on platforms
that do not have high-performance fabric hardware.  The sockets provider
supports all libfabric provider requirements and interfaces.

See the `fi_sockets(7)` man page for more details.

### tcp

***

The tcp provider is an optimized socket based provider that supports
reliable connected endpoints.  The current version is the redesigned
one previously called the net provider.  This version supports both
MSG endpoints and RDM endpoints. It can also work in conjunction with
the rxm provider for apps that need similar RDM behavior as the old
tcp provider.  The tcp provider targets replacing the sockets provider
for applications using standard networking hardware.

See the `fi_tcp(7)` man page for more details.

### udp

***

The `udp` provider is a basic provider that can be used on any system that
supports UDP sockets.  The provider is not intended to provide performance
improvements over regular UDP sockets, but rather allow applications and
provider developers to write, test, and debug their code.  The `udp` provider
forms the foundation of a utility provider that enables the implementation of
libfabric features over any hardware.

See the `fi_udp(7)` man page for more details.

### usnic

***

The `usnic` provider is designed to run over the Cisco VIC (virtualized NIC)
hardware on Cisco UCS servers. It utilizes the Cisco usnic (userspace NIC)
capabilities of the VIC to enable ultra low latency and other offload
capabilities on Ethernet networks.

See the `fi_usnic(7)` man page for more details.

#### Dependencies

- The `usnic` provider depends on library files from either `libnl` version 1
  (sometimes known as `libnl` or `libnl1`) or version 3 (sometimes known as
  `libnl3`). If you are compiling libfabric from source and want to enable
  usNIC support, you will also need the matching `libnl` header files (e.g.,
  if you are building with `libnl` version 3, you need both the header and
  library files from version 3).

#### Configure options

```
--with-libnl=<directory>
```

If specified, look for libnl support. If it is not found, the `usnic`
provider will not be built. If `<directory>` is specified, then check in the
directory and check for `libnl` version 3. If version 3 is not found, then
check for version 1. If no `<directory>` argument is specified, then this
option is redundant with `--with-usnic`.

### verbs

***

The verbs provider enables applications using OFI to be run over any verbs
hardware (Infiniband, iWarp, and RoCE). It uses the Linux Verbs API for network
transport and translates OFI calls to appropriate verbs API calls.
It uses librdmacm for communication management and libibverbs for other control
and data transfer operations.

The verbs provider can also be built on Windows using the Microsoft Network
Direct SPI for network transport.

See the `fi_verbs(7)` man page for more details.

#### Dependencies

- The verbs provider requires libibverbs (v1.1.8 or newer) and librdmacm (v1.0.16
  or newer). If you are compiling libfabric from source and want to enable verbs
  support, you will also need the matching header files for the above two libraries.
  If the libraries and header files are not in default paths, specify them in CFLAGS,
  LDFLAGS and LD_LIBRARY_PATH environment variables.

- Windows built requires Network Direct SPI. If you are compiling libfabric from
  source, you will also need the matching header files for the Network Direct SPI.
  If the libraries and header files are not in default paths, specify them in the
  configuration properties of the VS project.

### shm

***

The shm provider enables applications using OFI to be run over shared memory.

See the `fi_shm(7)` man page for more details.

#### Dependencies

- The shared memory provider only works on Linux platforms and makes use of
  kernel support for 'cross-memory attach' (CMA) data copies for large
  transfers.

### efa

***

The `efa` provider enables the use of libfabric-enabled applications on [Amazon
EC2 Elastic Fabric Adapter (EFA)](https://aws.amazon.com/hpc/efa/), a
custom-built OS bypass hardware interface for inter-instance communication on
EC2.

See [`fi_efa`(7)](https://ofiwg.github.io/libfabric/main/man/fi_efa.7.html) for more information.

## WINDOWS Instructions

It is possible to compile and link libfabric with windows applications.

- 1. You need the NetDirect provider to use RDMA NICs:
  Network Direct SDK/DDK may be obtained as a NuGet package (preferred) from:

  https://www.nuget.org/packages/NetworkDirect

  or downloaded from:

  https://www.microsoft.com/en-us/download/details.aspx?id=36043
  on page press Download button and select NetworkDirect_DDK.zip.

  Extract header files from downloaded
  NetworkDirect_DDK.zip:`\NetDirect\include\` into `include\windows`, or
  add the path to NetDirect headers into VS include paths

- 2. compiling:
  libfabric has 6 Visual Studio solution configurations:

      1-2: Debug/Release ICC (restricted support for Intel Compiler XE 15.0 only)
      3-4: Debug/Release v140 (VS 2015 tool set)
      5-6: Debug/Release v141 (VS 2017 tool set)
      7-8: Debug/Release v142 (VS 2019 tool set)

  Make sure you choose the correct target fitting your compiler.
  By default, the library will be compiled to `<libfabricroot>\x64\<yourconfigchoice>`

- 3. linking your library
  - right-click your project and select properties.
  - choose C/C++ > General and add `<libfabricroot>\include` to "Additional include Directories"
  - choose Linker > Input and add `<libfabricroot>\x64\<yourconfigchoice>\libfabric.lib` to "Additional Dependencies"
  - depending on what you are building you may also need to copy `libfabric.dll` into the target folder of your own project.

### cxi

The CXI provider enables libfabric on Cray's Slingshot network. Slingshot is
comprised of the Rosetta switch and Cassini NIC. Slingshot is an
Ethernet-compliant network. However, The provider takes advantage of proprietary
extensions to support HPC applications.

The CXI provider supports reliable, connection-less endpoint semantics. It
supports two-sided messaging interfaces with message matching offloaded by the
Cassini NIC. It also supports one-sided RMA and AMO interfaces, light-weight
counting events, triggered operations (via the deferred work API), and
fabric-accelerated small reductions.

See the `fi_cxi(7)` man page for more details.

#### Dependencies

- The CXI Provider requires Cassini's optimized HPC protocol which is only
  supported in combination with the Rosetta switch.

- The provider uses the libCXI library for control operations and a set of
  Cassini-specific header files to enable direct hardware access in the data path.
