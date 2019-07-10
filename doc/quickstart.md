# DAOS Quick Start Guide

This guide provides basic instructions on how to install and run DAOS. More in-depth details on how to deploy, administer and monitor a DAOS installation are provided in the [DAOS operations manual](https://wiki.hpdd.intel.com/display/DC/Operations+Manual).

DAOS runs on both x86 and ARM platforms and has been successfully tested on CentOS7, openSuSE 42.2 and Ubuntu 18.04 distributions. [Recipes](/utils/docker/) and [instructions](/utils/docker/README.md) to build Docker images (Singularity support to come soon) for each of these Linux distributions are available. To set up a DAOS development environment, please check this extra [information](development.md)

## Hardware Support

DAOS requires a 64-bit processor architecture and is primarily developed on x64-64 and AArch64 platforms. Apart from using a popular Linux distribution, no special considerations are necessary to build DAOS on 64-bit ARM processors. The same instructions that are used in Xeon are applicable for ARM builds as well, DAOS and its dependencies will make the necessary adjustments automatically in their respective build systems for ARM platforms.

Storage-class memory (SCM) can be emulated with DRAM by creating tmpfs mountpoints on the DAOS servers.

## Software Dependencies

DAOS requires a C99-capable compiler, a Go compiler, and the scons build tool. Moreover, the DAOS stack leverages the following open source projects:
- [CaRT](https://github.com/daos-stack/cart) that relies on both [Mercury](https://mercury-hpc.github.io) and [Libfabric](https://ofiwg.github.io/libfabric/) for lightweight network transport and [PMIx](https://github.com/pmix/master) for process set management. See the CaRT repository for more information on how to build the CaRT library.
- [PMDK](https://github.com/pmem/pmdk.git) for persistent memory programming.
- [SPDK](http://spdk.io) for userspace NVMe device access and management.
- [FIO](https://github.com/axboe/fio) for flexible testing of Linux I/O subsystems, specifically enabling validation of userspace NVMe device performance through fio-spdk plugin.
- [ISA-L](https://github.com/01org/isa-l) for checksum and erasure code computation.
- [Argobots](https://github.com/pmodels/argobots) for thread management.

The DAOS build system can be configured to download and build any missing dependencies automatically.

## DAOS Packages

### RPMs

TODO

### deb

TODO

### Spack

TODO

### Snaps

TODO

## DAOS from Scratch

The following instructions have been verified with CentOS. Installations on other Linux distributions might be similar with some variations. Developers of DAOS may want to check additional sections below before beginning for suggestions related specifically to development. Please contact us in our [forum](https://daos.groups.io/g/daos) if running into issues.

### Build Prerequisites

Please install the following software packages (or equivalent for other distros):

On CentOS and openSuSE:

```
    yum install -y epel-release
    yum install -y git gcc gcc-c++ make cmake golang libtool scons boost-devel
    yum install -y libuuid-devel openssl-devel libevent-devel libtool-ltdl-devel
    yum install -y librdmacm-devel libcmocka libcmocka-devel readline-devel
    yum install -y doxygen pandoc flex patch nasm yasm
    yum install -y ninja-build meson libyaml-devel
    # Required SPDK packages for managing NVMe SSDs
    yum install -y CUnit-devel libaio-devel python-pep8 lcov
    yum install -y python clang-analyzer sg3_utils libiscsi-devel
    yum install -y libibverbs-devel numactl-devel doxygen graphviz
    # Required IpmCtl packages for managing SCM Modules
    yum install -y yum-plugin-copr epel-release
    yum copr -y enable jhli/ipmctl
    yum copr -y enable jhli/safeclib
    yum install -y libipmctl-devel
```

On Ubuntu and Debian:

```
    apt-get install -y git gcc golang make cmake libtool-bin scons autoconf
    apt-get install -y libboost-dev uuid-dev libssl-dev libevent-dev libltdl-dev
    apt-get install -y librdmacm-dev libcmocka0 libcmocka-dev libreadline6-dev
    apt-get install -y curl doxygen pandoc flex patch nasm yasm
    apt-get install -y ninja-build meson libyaml-dev python2.7-dev
    # Required SPDK packages for managing NVMe SSDs
    apt-get install -y libibverbs-dev librdmacm-dev libcunit1-dev graphviz
    apt-get install -y libaio-dev sg3-utils libiscsi-dev doxygen libnuma-dev
    # Required IpmCtl packages for managing SCM Modules
    apt-get install -y software-properties-common
    add-apt-repository ppa:jhli/libsafec
    add-apt-repository ppa:jhli/ipmctl
    apt-get update
    apt-get install -y libipmctl-dev
```

Moreover, please make sure the autotools packages listed below are at the appropriate versions:
- m4 (GNU M4) 1.4.16
- flex 2.5.37
- autoconf (GNU Autoconf) 2.69
- automake (GNU automake) 1.13.4
- libtool (GNU libtool) 2.4.2

### DAOS Source Code

To check out the DAOS source code, run the following command:

```
    git clone https://github.com/daos-stack/daos.git
```

This clones the DAOS git repository (path referred as \${daospath} below). Then initialize the submodules with:

```
    cd ${daospath}
    git submodule init
    git submodule update
```

### Building DAOS and Dependencies

If all the software dependencies listed previously are already satisfied, type the following command in the top source directory to build the DAOS stack:

```
    scons --config=force install
```

If you are a developer of DAOS, we recommend following the instructions in [DAOS for Developers](development.md).

Otherwise, the missing dependencies can be built automatically by invoking scons with the following parameters:

```
    scons --config=force --build-deps=yes install
```

By default, DAOS and its dependencies are installed under \${daospath}/install. The installation path can be modified by adding the PREFIX= option to the above command line (e.g., PREFIX=/usr/local).

### Environment setup

Configuration files can be used to specify options to run DAOS with. Pass location of config when starting daos_server with -o option and examples/explanations can be found in YAML files located in utils/config/ within DAOS source.

Once built, the environment must be modified to search for binaries and header files in the installation path. This step is not required if standard locations (e.g., /bin, /sbin, /usr/lib, ...) are used.

```
    CPATH=${daospath}/install/include/:$CPATH
    PATH=${daospath}/install/bin/:${daospath}/install/sbin:$PATH
    export CPATH PATH
```

If using bash, PATH can be setup for you after a build by sourcing the script scons_local/utils/setup_local.sh from the DAOS root.   This script utilizes a file generated by the build to determine the location of DAOS and its dependencies.

If required, \${daospath}/install must be replaced with the alternative path specified through PREFIX. The network type to use as well the debug log location can be selected as follows:

```
    export CRT_PHY_ADDR_STR="ofi+sockets",
	OFI_INTERFACE=eth0, where eth0 is the network device you want to use.
	For InfiniBand you could use ib0 or whichever else pointing to IB device.
```

## Running DAOS

### Runtime Directory Setup

DAOS uses a series of Unix Domain Sockets to communicate between its various components. On modern Linux systems, Unix Domain Sockets are typically stored under /run or /var/run (usually a symlink to /run) and are a mounted tmpfs file system. There are several methods for ensuring the necessary directories are setup.

A sign that this step may have missed is when starting daos_server or daos_agent you may see the message:

```
	mkdir /var/run/daos_server: permission denied
	Unable to create socket directory: /var/run/daos_server
```

#### Non-default Directory

By default daos_server and daos_agent will use the directories /var/run/daos_server and /var/run/daos_agent respectively. To change the default location that daos_server uses for its runtime directory either uncomment and set the socket_dir configuration value in install/etc/daos_server.yaml or pass the location to daos_server on the command line using the -d flag. For the daos_agent an alternate location can be passed on the command line using the -runtime_dir flag.

#### Default Directory (non-persistent)

Files and directories created in /run and /var/run only survive until the next reboot. However, if reboots are infrequent an easy solution while still utilizing the default locations is to manually create the required directories. To do this execute the following commands.

daos_server:
- mkdir /var/run/daos_server
- chmod 0755 /var/run/daos_server
- chown user:user /var/run/daos_server (where user is the user you will run daos_server as)

daos_agent:
- mkdir /var/run/daos_agent
- chmod 0755 /var/run/daos_agent
- chown user:user /var/run/daos_agent (where user is the user you will run daos_agent as)

#### Default Directory (persistent)

If the server hosting daos_server or daos_agent will be rebooted often, systemd provides a persistent mechanism for creating the required directories called tmpfiles.d. This mechanism will be required every time the system is provisioned and require a reboot to take effect.

To tell Systemd to create the necessary directories for daos:
- Copy the file utils/systemd/daosfiles.conf to /etc/tmpfiles.d (cp utils/systemd/daosfiles.conf /etc/tmpfiles.d)
- Modify the copied file to change the user and group fields (currently daos) to the user daos will be run as
- Reboot the system and the directories will be created automatically on all subsequent reboots.

### Starting DAOS server

DAOS uses orterun(1) for scalable process launch. The list of storage nodes can be specified in a host file (referred as \${hostfile}). The DAOS server and the application can be started separately, but must share a URI file (referred as \${urifile}) to connect to each other. The \${urifile} is generated by orterun using (\-\-report-uri filename) at server and used at application with (\-\-ompi-server file:filename). Also, the DAOS server must be started with the \-\-enable-recovery option to support server failure. See the orterun(1) man page for additional options.

On each storage node, the DAOS server will use a storage path (specified either in the server configuration file or by --storage or -s cli options) that must be a directory in a tmpfs filesystem, for the time being. If not specified, it is assumed to be /mnt/daos. To configure the storage:

```
    mount -t tmpfs -o size=<bytes> tmpfs /mnt/daos
```

To start the DAOS server, run:

```
    orterun -np <num_servers> --hostfile ${hostfile} --enable-recovery --report-uri ${urifile} daos_server -i
```

By default, the DAOS server will use all the cores available on the storage server. You can limit the number of execution streams in the configuration file (cpus) or with the -c #cores\_to\_use cli option.
Hostfile used here is the same as the ones used by Open MPI. See (https://www.open-mpi.org/faq/?category=running#mpirun-hostfile) for additional details.

### Starting DAOS agent

The DAOS agent is a process run locally to the machine running DAOS client library applications. The purpose of the agent is to allow DAOS applications to obtain security credentials for the purposes of connecting to various DAOS pools. To ensure that the agent can start properly follow the directions in the [Runtime Directory setup](#-Runtime-Directory-Setup) section.

To start the DAOS agent, run:
```
	daos_agent -i &
```

### DAOS Pool Management

A DAOS pool can be created and destroyed through the DAOS management API (see  daos\_mgmt.h). A utility called dmg to manage storage pools from the command line is also provided.

To create a pool:

```
    orterun -np 1 --ompi-server file:${urifile} dmg create --size=xxG --nvme=yyG
```

This creates a pool distributed across the DAOS servers with a target size on each server of xxGB SCM (mandatory option) and yyGB NVMe (optional). The UUID allocated to the newly created pool is printed to stdout (referred to as \${pooluuid}).

To destroy a pool:

```
    orterun -np 1 --ompi-server file:${urifile} dmg destroy --pool=${pooluuid}
```

### Testing DAOS

To build applications or I/O middleware against the DAOS library, include the daos.h header file in your program and link with -Ldaos. Examples are available under src/tests. All DAOS library applications including daos_test must have daos_agent started prior to running. To configure and start daos agent see the [Starting DAOS agent](#-Starting-DAOS-agent) section

To run the applications:

```
    orterun -np <num_clients> --hostfile ${hostfile_cli} --ompi-server file:$urifile ${application} eg., ./daos_test
```

An exhaustive list of DAOS-aware I/O middleware is available [here](middleware.md)
