# Distributed Asynchronous Object Storage

[![Build Status](https://travis-ci.org/daos-stack/daos.svg?branch=master)](https://travis-ci.org/daos-stack/daos)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

## What is DAOS?

The Distributed Asynchronous Object Storage (DAOS) stack provides a new storage paradigm for Exascale computing and Big Data. DAOS is an open-source storage stack designed from the ground up to exploit NVRAM and NVMe storage technologies with integrated fabric. It provides ultra-fine grained I/O by using a persistent memory storage model for byte-granular data & metadata combined with NVMe storage for bulk data, all this with end-to-end OS bypass to guarantee ultra-low latency. The DAOS stack aims at increasing data velocity by several orders of magnitude over conventional storage stacks and providing extreme scalability and resilience.

The essence of the DAOS storage model is a key-value store interface over which specific data models can be implemented. A DAOS object is effectively a table of records that are addressed through a flexible multi-level key allowing fine-grain control over colocation of related data. Objects are collected into manageable units called containers. DAOS provides scalable distributed transactions across all objects of a container guaranteeing data consistency and automated rollback on failure to I/O middleware libraries and applications. The DAOS transaction mechanism supports complex workflows with native producer/consumer pipeline in which concurrent consumers do not block producers and consumers receive notification on complete atomic updates and see a consistent snapshot of data..

For both performance and resilience, DAOS objects support multiple distribution and redundancy schemas with fully automated and distributed recovery on storage failure. DAOS uses declustered replication and/or erasure coding over homogeneous shared-nothing servers and provides a lockless consistency model at arbitrary alignment.

Finally, the DAOS stack abstracts multi-tier storage architecture and offers a unified storage model over which multiple top-level APIs will be developed. This includes domain-specific APIs like HDF5, SCiDB, ADIOS and high-level data models like HDFS, Spark and Graph A. A POSIX namespace encapsulation inside a DAOS container is also under consideration.

## Project History

The project started back in 2012 with the Fast Forward Storage & I/O program supported by the U.S. DoE in which a first DAOS prototype was implemented over the Lustre filesystem and ZFS. In 2015, a follow-on program called Extreme Scale Storage and I/O (ESSIO) continued the momentum with the development of a new standalone prototype fully in userspace that is the code base provided in this repository.

## Motivations

The emergence of data-intensive applications in business, government and academia stretches the existing I/O models beyond limits. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared-nothing interface from the ground up and be able to support massively distributed storage for which failure will be the norm, while preserving low latency and high bandwidth access over the fabric.

DAOS is a complete I/O architecture that aggregates persistent memory and NVMe storage distributed across the fabric into globally-accessible object address spaces, providing consistency, availability and resiliency guarantees without compromising performance.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Software Requirements

DAOS requires a C99-capable compiler and the scons build tool. In addition, the DAOS stack is proud to leverage the following open source projects:
- [CaRT](https://github.com/daos-stack/cart) that relies on both [Mercury](https://mercury-hpc.github.io) and [CCI](http://cci-forum.com/wp-content/uploads/2015/12/cci-0.3.0.tar.gz) for lightweight network transport and [PMIx](https://github.com/pmix/master) for process set management. See the CaRT repository for more information on how to build the CaRT library.
- [NVML](https://github.com/pmem/nvml.git) for persistent memory programming..
- [Argobots](https://github.com/pmodels/argobots) for thread management.

If all the software dependencies listed above are already satisfied, then just type "scons" in the top source directory to build the DAOS stack. Otherwise, please follow the instructions in the section below to build DAOS with all the dependencies.

In the near future, the DAOS stack will also rely on:
- [SPDK](http://spdk.io) for NVMe device access and management
- [ISA-L](https://github.com/01org/isa-l) for checksum and erasure code computation

## Building DAOS

The below instructions have been verified with CentOS. Installations on other Linux distributions might be similar with some variations. Please contact us in our [Google group](https://groups.google.com/forum/#!forum/daos-users) if running into issues.

(a) Pre-install dependencies
Please install the following Red Hat Enterprise Linux RPM software packages (or) equivalent for other distros:

    # yum -y install epel-release scons cmake doxygen gcc-c++
    # yum -y install boost-devel libevent-devel librdmacm-devel
    # yum -y install libtool-ltdl-devel libuuid-devel openssl-devel
    # yum -y install libcmocka libcmocka-devel pandoc

If no Cmocka RPMs are available, please install from [source](https://cmocka.org/files/1.1/cmocka-1.1.0.tar.xz) as detailed below:

    # tar xvf cmocka-1.1.0.tar.xz
    # cd cmocka
    # mkdir build && cd build && cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug .. && make && sudo make install

Moreover, please make sure all the auto tools listed below are at the appropriate versions.

    m4 (GNU M4) 1.4.16
    flex 2.5.37
    autoconf (GNU Autoconf) 2.69
    automake (GNU automake) 1.13.4
    libtool (GNU libtool) 2.4.2

(b) Checking out the DAOS source code

    # git clone https://github.com/daos-stack/daos.git

This clones the DAOS git repository (path referred as \${daospath} below). Then initialize the submodules with:

    # cd ${daospath}
    # git submodule init
    # git submodule update

(c) Building all dependencies automatically

Invoke scons with the following parameters:

    # scons --build-deps=yes install

By default, all software will be installed under \${daospath}/install. The TARGET\_PREFIX= option can be added to the command line above to specify an alternative installation path.

(d) Environment setup

Once built, the environment must be modified to search for binaries, libraries and header files in the installation path. This step is not required if standard locations (e.g. /bin, /sbin, /usr/lib, ...) are used.

    LD_LIBRARY_PATH=${daospath}/install/lib:$LD_LIBRARY_PATH
    LD_LIBRARY_PATH=${daospath}/install/lib/daos_srv/:$LD_LIBRARY_PATH
    CPATH=${daospath}/install/include/:$CPATH
    PATH=${daospath}/install/bin/:${daospath}/install/sbin:$PATH
    export LD_LIBRARY_PATH CPATH PATH

If required, \${daospath}/install must be replaced with the alternative path specified through PREFIX. The network type to use as well the debug log location can be selected as follows:

    CCI_CONFIG=${daospath}/install/etc/cci.ini
    CRT_PHY_ADDR_STR="cci+tcp", for infiniband use "cci+verbs"
    DD_LOG=${daosdebugpath}, /tmp/daos.log by default.
    export CCI_CONFIG CRT_PHY_ADDR_STR DD_LOG

Additionally, one might want to set the following environment variables to work around an Argobot issue:

    ABT_ENV_MAX_NUM_XSTREAMS=100
    ABT_MAX_NUM_XSTREAMS=100
    export ABT_ENV_MAX_NUM_XSTREAMS ABT_MAX_NUM_XSTREAMS

## Using DAOS

DAOS uses orterun(1) for scalable process launch. The list of storage nodes can be specified in an host file (referred as \${hostfile}). The DAOS server and the application can be started seperately, but must share an URI file (referred as \${urifile}) to connect to each other. In addition, the DAOS server must be started with the \-\-enable-recovery option to support server failure. See the orterun(1) man page for additional options.

(a) Starting the DAOS server

On each storage node, the DAOS server will use /mnt/daos as the storage backend that must be configured, for the time being, as a tmpfs filesystem. To start the DAOS server, run:

    orterun -np <num_servers> --hostfile ${hostfile} --enable-recovery --report-uri ${urifile} daos_server

By default, the DAOS server will use all the cores available on the storage server. You can limit the number of execution streams with the -c #cores\_to\_use option.

(b) Creating/destroy a DAOS pool

A DAOS pool can be created and destroyed through the DAOS management API (see  daos\_mgmt.h). We also provide an utility called dmg to manage storage pools from the command line.

To create a pool:

    orterun --ompi-server file:${urifile} dmg create --size=xxG

This creates a pool distributed across the DAOS servers with a target size on each server of xxGB. The UUID allocated to the newly created pool is printed to stdout (referred as \${pooluuid}).

To destroy a pool:

    orterun --ompi-server file:${urifile} dmg destroy --pool=${pooluuid}

(c) Building applications or I/O middleware against the DAOS library

Include the daos.h header file in your program and link with -Ldaos. Examples are available under src/tests.

(d) Running applications

    orterun -np <num_clients> --hostfile ${hostfile_cli} --ompi-server file:$urifile ${application} eg., ./daos_test

## Setup DAOS for Development

Setting up DAOS for development would be simpler by building with TARGET\_PREFIX for all the dependencies and use PREFIX for your custom DAOS installation from your sandbox. Once the submodule has been initialized and updated,

    scons --build-deps=yes PREFIX=$(daos_prefix_path} TARGET_PREFIX=${daos_prefix_path}/opt install

With this type of installation each individual component is built into a different directory. Installing the components into seperate directories allow to upgrade the components individually using --update-prereq={component\_name}. This requires change to the environment configuration from before.


    ARGOBOTS=${daos_prefix_path}/opt/argobots
    CART=${daos_prefix_path}/opt/cart
    CCI=${daos_prefix_path}/opt/cci
    HWLOC=${daos_prefix_path}/opt/hwloc
    MERCURY=${daos_prefix_path}/opt/mercury
    NVML=${daos_prefix_path}/opt/nvml
    OMPI=${daos_prefix_path}/opt/ompi
    OPA=${daos_prefix_path}/opt/openpa
    PMIX=${daos_prefix_path}/opt/pmix

    PATH=$CART/bin/:$CCI/bin/:$HWLOC/bin/:$PATH
    PATH=$MERCURY/bin/:$NVML/bin/:$OMPI/bin/:$OPA/bin/:$PMIX/bin/:$PATH
    PATH=${daos_prefix_path}/bin/:$PATH

    LD_LIBRARY_PATH=/usr/lib64/:$ARGOBOTS/lib/:$CART/lib/:$CCI/lib/:$HWLOC/lib/:$LD_LIBRARY_PATH
    LD_LIBRARY_PATH=$MERCURY/lib/:$NVML/lib/:$OMPI/lib/:$OMPI/lib/openmpi/:$OPA/lib/:$PMIX/lib/:$LD_LIBRARY_PATH
    LD_LIBRARY_PATH=${daos_prefix_path}/lib/:${daos_prefix_path}/lib/daos_srv/:$LD_LIBRARY_PATH

(a) Using prebuilt dependencies

With an installation complete with TARGET_PREFIX, the PREBUILT_PREFIX functionality can be used to reuse prebuilt dependencies.
On your new sandbox after 'git submodule init and git submodule update'

    scons PREBUILT_PREFIX=${daos_prefix_path}/opt PREFIX=${daos_custom_install} install

With this approach only daos would get built using the prebuilt dependencies in ${daos_prefix_path}/opt and the PREBUILT_PREFIX and PREFIX would get saved for future compilations. So, after the first time, during development, a mere "scons" and "scons install" would suffice for compiling changes to daos source code.

## Reporting Problems

Please report any bugs through our [issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and debug logs that should be compressed. DAOS debug logs are written by default to /tmp/daos.log, this can be modified by specifying a different path through DD_LOG. Similarly, the debug mask and subsystems are controlled by respectively the DD_MASK and DD_SUBSYS environment variables.

## Contacts

For more information on DAOS, please post to our [Google group](https://groups.google.com/forum/#!forum/daos-users).
