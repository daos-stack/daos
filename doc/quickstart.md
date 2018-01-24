# DAOS Quick Start Guide

DAOS runs on both x86 and ARM64 platforms and has been successfully tested on CentOS7, openSuSE 42.2 and Ubuntu 16.04 distributions.

## DAOS in Docker

To build the Docker image, run the following command:

    $ docker build -t daos -f Dockerfile.centos-7 github.com/daos-stack/daos#:utils/docker

This creates a CentOS7 image and builds the latest DAOS version from GitHub in this environment.
For Ubuntu, replace "Dockerfile.centos-7" with "Dockerfile.ubuntu-16.04".

To run the DAOS server in a new container:

    $ docker run --tmpfs /mnt/daos:rw,uid=1000,size=1G -v /tmp/uri:/tmp/uri daos \
      orterun -H localhost -np 1 --report-uri /tmp/uri/uri.txt install/bin/daos_server

This allocates 1GB of DRAM for DAOS storage. The more, the better.

To run the DAOS unit tests:

    $ docker run -v /tmp/uri:/tmp/uri daos \
      orterun -H localhost -np 1 --ompi-server file:/tmp/uri/uri.txt install/bin/daos_test

## DAOS from Scratch

### Build Prerequisites

DAOS requires a C99-capable compiler, a golang compiler and the scons build tool. In addition, the DAOS stack leverages the following open source projects:
- [CaRT](https://github.com/daos-stack/cart) that relies on both [Mercury](https://mercury-hpc.github.io) and [Libfabric](https://ofiwg.github.io/libfabric/) for lightweight network transport and [PMIx](https://github.com/pmix/master) for process set management. See the CaRT repository for more information on how to build the CaRT library.
- [PMDK](https://github.com/pmem/pmdk.git) for persistent memory programming.
- [SPDK](http://spdk.io) for NVMe device access and management
- [ISA-L](https://github.com/01org/isa-l) for checksum and erasure code computation
- [Argobots](https://github.com/pmodels/argobots) for thread management.

If all the software dependencies listed above are already satisfied, then just type "scons" in the top source directory to build the DAOS stack. Otherwise, please follow the instructions in the section below to build DAOS with all the dependencies.

### Building DAOS & Dependencies

The below instructions have been verified with CentOS. Installations on other Linux distributions might be similar with some variations. Please contact us in our [forum](users@daos.groups.io) if running into issues.

(a) Pre-install dependencies
Please install the following software packages (or equivalent for other distros):

- On CentOS and openSuSE:
    $ yum -y install epel-release
    $ yum -y install git gcc gcc-c++ make cmake golang libtool scons boost-devel
    $ yum -y install libuuid-devel openssl-devel libevent-devel libtool-ltdl-devel
    $ yum -y install librdmacm-devel libcmocka libcmocka-devel
    $ yum -y install doxygen pandoc flex patch

- On Ubuntu and Debian:
    $ apt-get install -y git gcc golang make cmake libtool-bin scons autoconf
    $ apt-get install -y libboost-dev uuid-dev libssl-dev libevent-dev libltdl-dev
    $ apt-get install -y librdmacm-dev libcmocka0 libcmocka-dev
    $ apt-get install -y curl doxygen pandoc flex patch

If no Cmocka RPMs are available, please install from [source](https://cmocka.org/files/1.1/cmocka-1.1.0.tar.xz) as detailed below:

    $ tar xvf cmocka-1.1.0.tar.xz
    $ cd cmocka
    $ mkdir build && cd build && cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug .. && make && sudo make install

Moreover, please make sure all the auto tools listed below are at the appropriate versions.

    m4 (GNU M4) 1.4.16
    flex 2.5.37
    autoconf (GNU Autoconf) 2.69
    automake (GNU automake) 1.13.4
    libtool (GNU libtool) 2.4.2

(b) Checking out the DAOS source code

    $ git clone https://github.com/daos-stack/daos.git

This clones the DAOS git repository (path referred as \${daospath} below). Then initialize the submodules with:

    $ cd ${daospath}
    $ git submodule init
    $ git submodule update

(c) Building all dependencies automatically

Invoke scons with the following parameters:

    $ scons --build-deps=yes install

The default installation path, modified by adding the PREFIX= option to the above command line is \${daospath}/install.   DAOS and its dependencies are installed here by default.   If TARGET\_PREFIX is added to the above command line, each dependency will be installed in a unique subdirectory at the specified location.

(d) Environment setup

Once built, the environment must be modified to search for binaries and header files in the installation path. This step is not required if standard locations (e.g. /bin, /sbin, /usr/lib, ...) are used.

    CPATH=${daospath}/install/include/:$CPATH
    PATH=${daospath}/install/bin/:${daospath}/install/sbin:$PATH
    export CPATH PATH

If required, \${daospath}/install must be replaced with the alternative path specified through PREFIX. The network type to use as well the debug log location can be selected as follows:

    CRT_PHY_ADDR_STR="ofi+sockets",
	OFI_INTERFACE=eth0, where eth0 is the network device you want to use.
	for infiniband you could use ib0 or whichever else pointing to IB device.
    export CRT_PHY_ADDR_STR

Additionally, one might want to set the following environment variables to work around an Argobot issue:

    ABT_ENV_MAX_NUM_XSTREAMS=100
    ABT_MAX_NUM_XSTREAMS=100
    export ABT_ENV_MAX_NUM_XSTREAMS ABT_MAX_NUM_XSTREAMS

### Running DAOS

DAOS uses orterun(1) for scalable process launch. The list of storage nodes can be specified in an host file (referred as \${hostfile}). The DAOS server and the application can be started seperately, but must share an URI file (referred as \${urifile}) to connect to each other. The \${urifile} is generated by orterun using (\-\-report-uri filename) at server and used at application with (\-\-ompi-server file:filename). In addition, the DAOS server must be started with the \-\-enable-recovery option to support server failure. See the orterun(1) man page for additional options.

(a) Starting the DAOS server

On each storage node, the DAOS server will use a storage path (specified by --storage or -s) that must be a directory in a tmpfs filesystem, for the time being. If not specified, it is assumed to be /mnt/daos. To configure the storage:

    $ mount -t tmpfs -o size=<bytes> tmpfs /mnt/daos

To start the DAOS server, run:

    $ orterun -np <num_servers> --hostfile ${hostfile} --enable-recovery --report-uri ${urifile} daos_server

By default, the DAOS server will use all the cores available on the storage server. You can limit the number of execution streams with the -c #cores\_to\_use option.
Hostfile used here is the same as the ones used by Open MPI. See (https://www.open-mpi.org/faq/?category=running#mpirun-hostfile) for additional details.

(b) Creating/destroy a DAOS pool

A DAOS pool can be created and destroyed through the DAOS management API (see  daos\_mgmt.h). We also provide an utility called dmg to manage storage pools from the command line.

To create a pool:

    $ orterun --ompi-server file:${urifile} dmg create --size=xxG

This creates a pool distributed across the DAOS servers with a target size on each server of xxGB. The UUID allocated to the newly created pool is printed to stdout (referred as \${pooluuid}).

To destroy a pool:

    $ orterun --ompi-server file:${urifile} dmg destroy --pool=${pooluuid}

(c) Building applications or I/O middleware against the DAOS library

Include the daos.h header file in your program and link with -Ldaos. Examples are available under src/tests.

(d) Running applications

    $ orterun -np <num_clients> --hostfile ${hostfile_cli} --ompi-server file:$urifile ${application} eg., ./daos_test

## DAOS for Development

Setting up DAOS for development would be simpler by building with running a separate command using TARGET\_PREFIX for all the dependencies and then using PREFIX for your custom DAOS installation from your sandbox and PREBUILT\_PREFIX to point to the same location specified for dependencies. Once the submodule has been initialized and updated, run the following:

    $ cd scons_local
    $ scons --build-deps=yes TARGET_PREFIX=${daos_prefix_path}/opt REQUIRES=ompi,pmdk,argobots,cart --build-config=../utils/build.config
    $ cd ..
    $ scons PREFIX=$(daos_prefix_path} PREBUILT_PREFIX=${daos_prefix_path}/opt install

With this type of installation each individual component is built into a different directory. Installing the components into seperate directories allow to upgrade the components individually from the scons_local subdirectory using --update-prereq={component\_name} and REQUIRES={component\_name}. This requires change to the environment configuration from before.

    ARGOBOTS=${daos_prefix_path}/opt/argobots
    CART=${daos_prefix_path}/opt/cart
    HWLOC=${daos_prefix_path}/opt/hwloc
    MERCURY=${daos_prefix_path}/opt/mercury
    PMDK=${daos_prefix_path}/opt/pmdk
    OMPI=${daos_prefix_path}/opt/ompi
    OPA=${daos_prefix_path}/opt/openpa
    PMIX=${daos_prefix_path}/opt/pmix

    PATH=$CART/bin/:$OMPI/bin/:${daos_prefix_path}/bin/:$PATH

With this approach only daos would get built using the prebuilt dependencies in ${daos_prefix_path}/opt and the PREBUILT_PREFIX and PREFIX would get saved for future compilations. So, after the first time, during development, a mere "scons" and "scons install" would suffice for compiling changes to daos source code.
