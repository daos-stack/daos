# DAOS Quick Start Guide

DAOS runs on both x86 and ARM platforms and has been successfully tested on CentOS7, openSuSE 42.2 and Ubuntu 16.04 distributions.

## Hardware Support

DAOS requires a 64-bit processor architecture and is primarily developed on x64-64 and AArch64 platforms. Apart from using a popular linux distribution, no special considerations are necessary to build DAOS on 64-bit ARM processors. The same instructions that are used in Xeon are applicable for ARM builds as well, DAOS and its dependencies will make the necessary adjustments automatically in their respective build systems for ARM platforms.

Storage-class memory (SCM) can be emulated with DRAM by creating tmpfs mountpoints on the DAOS servers.

## Software Dependencies

DAOS requires a C99-capable compiler, a golang compiler and the scons build tool. Moreover, the DAOS stack leverages the following open source projects:
- [CaRT](https://github.com/daos-stack/cart) that relies on both [Mercury](https://mercury-hpc.github.io) and [Libfabric](https://ofiwg.github.io/libfabric/) for lightweight network transport and [PMIx](https://github.com/pmix/master) for process set management. See the CaRT repository for more information on how to build the CaRT library.
- [PMDK](https://github.com/pmem/pmdk.git) for persistent memory programming.
- [SPDK](http://spdk.io) for userspace NVMe device access and management.
- [ISA-L](https://github.com/01org/isa-l) for checksum and erasure code computation.
- [Argobots](https://github.com/pmodels/argobots) for thread management.

The DAOS build system can be configured to download and build any missing dependencies automatically.

## DAOS Source Code

To check out the DAOS source code, run the following command:

    $ git clone https://github.com/daos-stack/daos.git

This clones the DAOS git repository (path referred as \${daospath} below). Then initialize the submodules with:

    $ cd ${daospath}
    $ git submodule init
    $ git submodule update

## DAOS in Docker

Docker is the fastest way to build, install and run DAOS on a non-Linux system.

### Building DAOS in a Container

To build the Docker image directly from GitHub, run the following command:

    $ docker build -t daos -f Dockerfile.centos\:7 github.com/daos-stack/daos#:utils/docker

This creates a CentOS7 image, fetches the latest DAOS version from GitHub and  builds it in the container.
For Ubuntu, replace Dockerfile.centos\:7 with Dockerfile.ubuntu\:16.04.

To build from a local tree stored on the host, a volume must be created to share the source tree with the Docker container. To do so, execute the following command to create a docker image without checking out the DAOS source tree:

    $ docker build -t daos -f utils/docker/Dockerfile.centos\:7 --build-arg NOBUILD=1 .

And then the following command to export the DAOS source tree to the docker container and build it:

    $ docker run -v ${daospath}:/home/daos/daos:Z daos scons --build-deps=yes USE_INSTALLED=all install

### Running DAOS in a Container

Let's first create a container that will run the DAOS service:

    $ docker run -it -d --name server --tmpfs /mnt/daos:rw,uid=1000,size=1G -v /tmp/uri:/tmp/uri daos

Add "-v \${daospath}:/home/daos/daos:Z" to this command line if DAOS source tree is stored on the host.

This allocates 1GB of DRAM for DAOS storage. The more, the better.

To start the DAOS service in this newly created container, execute the following command:

    $ docker exec server orterun -H localhost -np 1 --report-uri /tmp/uri/uri.txt daos_server

Once the DAOS server is started, the integration tests can be run as follows:

    $ docker run -v /tmp/uri:/tmp/uri daos \
      orterun -H localhost -np 1 --ompi-server file:/tmp/uri/uri.txt daos_test

Again, "-v \${daospath}:/home/daos/daos:Z" must be added if the DAOS source tree is shared with the host.

## DAOS from Scratch

The below instructions have been verified with CentOS. Installations on other Linux distributions might be similar with some variations. Please contact us in our [forum](users@daos.groups.io) if running into issues.

### Build Prerequisites

Please install the following software packages (or equivalent for other distros):

On CentOS and openSuSE:

    $ yum install -y epel-release
    $ yum install -y git gcc gcc-c++ make cmake golang libtool scons boost-devel
    $ yum install -y libuuid-devel openssl-devel libevent-devel libtool-ltdl-devel
    $ yum install -y librdmacm-devel libcmocka libcmocka-devel readline-devel
    $ yum install -y doxygen pandoc flex patch nasm yasm
    # Additionally required SPDK packages
    $ yum install -y CUnit-devel libaio-devel astyle-devel python-pep8 lcov
    $ yum install -y python clang-analyzer sg3_utils libiscsi-devel
    $ yum install -y libibverbs-devel numactl-devel doxygen mscgen graphviz

On Ubuntu and Debian:

    $ apt-get install -y git gcc golang make cmake libtool-bin scons autoconf
    $ apt-get install -y libboost-dev uuid-dev libssl-dev libevent-dev libltdl-dev
    $ apt-get install -y librdmacm-dev libcmocka0 libcmocka-dev libreadline6-dev
    $ apt-get install -y curl doxygen pandoc flex patch nasm yasm
    # Additionally required SPDK packages
    $ apt-get install -y libibverbs-dev librdmacm-dev libcunit1-dev graphviz
    $ apt-get install -y libaio-dev sg3-utils libiscsi-dev doxygen mscgen

Moreover, please make sure all the auto tools listed below are at the appropriate versions.

    m4 (GNU M4) 1.4.16
    flex 2.5.37
    autoconf (GNU Autoconf) 2.69
    automake (GNU automake) 1.13.4
    libtool (GNU libtool) 2.4.2


### Protobuf Compiler

The DAOS control plane infrastrucure will be using protobuf as the data serialization format for its RPC requests. The DAOS proto files use protobuf 3 syntax which is not supported by the platform protobuf compiler in all cases. Not all developers will need to build the proto files into the various source files. However if changes are made to the proto files they will need to be regenerated with a protobuf 3.* or higher compiler. To setup support for compiling protobuf files download the following precompiled package for Linux and install it somewhere accessible by your PATH variable.

    https://github.com/google/protobuf/releases/download/v3.5.1/protoc-3.5.1-linux-x86_64.zip

### Golang dependencies

The utilities/fetch_go_packages.sh script is provided to prep a GOPATH location for DAOS development.

By default scons will look for a GOPATH located under _build.external/go. To setup your build GOPATh execute the command below from the top level directory.

    utils/fetch_go_packages.sh -i .

Once complete verify the install worked by running the same command with an additional -v flag for verification.

### Building DAOS & Dependencies

If all the software dependencies listed previously are already satisfied, then just type the following command in the top source directory to build the DAOS stack:

    $ scons install

Otherwise, the missing dependencies can be built automatically by invoking scons with the following parameters:

    $ scons --build-deps=yes USE_INSTALLED=all install

By default, DAOS and its dependencies are installed under \${daospath}/install. The installation path can be modified by adding the PREFIX= option to the above command line (e.g. PREFIX=/usr/local).

### Environment setup

Once built, the environment must be modified to search for binaries and header files in the installation path. This step is not required if standard locations (e.g. /bin, /sbin, /usr/lib, ...) are used.

    CPATH=${daospath}/install/include/:$CPATH
    PATH=${daospath}/install/bin/:${daospath}/install/sbin:$PATH
    export CPATH PATH

If using bash, PATH can be setup for you after a build by sourcing the script scons_local/utils/setup_local.sh from the daos root.   This script utilizes a file generated by the build to determine the location of daos and its dependencies.

If required, \${daospath}/install must be replaced with the alternative path specified through PREFIX. The network type to use as well the debug log location can be selected as follows:

    export CRT_PHY_ADDR_STR="ofi+sockets",
	OFI_INTERFACE=eth0, where eth0 is the network device you want to use.
	for infiniband you could use ib0 or whichever else pointing to IB device.

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

For development, it is recommended to build and install each dependency in a unique subdirectory. The DAOS build system supports this through the TARGET\_PREFIX variable. Once the submodules have been initialized and updated, run the following:

    $ scons PREFIX=$(daos_prefix_path} TARGET_PREFIX=${daos_prefix_path}/opt install --build-deps=yes

Installing the components into seperate directories allow to upgrade the components individually replacing --build-deps=yes with --update-prereq={component\_name}. This requires change to the environment configuration from before. For automated environment setup, source scons_local/utils/setup_local.sh.

    ARGOBOTS=${daos_prefix_path}/opt/argobots
    CART=${daos_prefix_path}/opt/cart
    HWLOC=${daos_prefix_path}/opt/hwloc
    MERCURY=${daos_prefix_path}/opt/mercury
    PMDK=${daos_prefix_path}/opt/pmdk
    OMPI=${daos_prefix_path}/opt/ompi
    OPA=${daos_prefix_path}/opt/openpa
    PMIX=${daos_prefix_path}/opt/pmix
    SPDK=${daos_prefix_path}/opt/spdk

    PATH=$CART/bin/:$OMPI/bin/:${daos_prefix_path}/bin/:$PATH

With this approach DAOS would get built using the prebuilt dependencies in ${daos_prefix_path}/opt and required options are saved for future compilations. So, after the first time, during development, a mere "scons" and "scons install" would suffice for compiling changes to daos source code.

If you wish to compile DAOS with clang rather than gcc, set COMPILER=clang on the scons command line.   This option is also saved for future compilations.
