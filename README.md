# Distributed Asynchronous Object Store (DAOS)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

The Distributed Asynchronous Object Store (DAOS) provides a new storage paradigm for Exascale computing and Big Data. DAOS is an open-source storage stack designed from the ground up to exploit NVRAM and NVMe storage technologies with integrated fabric. It provides ultra-fine grained I/O by using a persistent memory storage model for byte-granular data & metadata combined with NVMe storage for bulk data, all this with end-to-end OS bypass to guarantee ultra-low latency. The DAOS stack aims at increasing data velocity by several orders of magnitude over conventional storage stacks and providing extreme scalability and resilience.

The essence of the DAOS storage model is a key-value store interface over which specific data models can be implemented. A DAOS object is effectively a table of records which are addressed through a flexible multi-level key allowing fine-grain control over colocation of related data. Objects are collected into manageable units called containers. DAOS provides scalable distributed transactions across all objects of a container guaranteeing data consistency and automated rollback on failure to I/O middleware libraries and applications. The DAOS transaction mechanism supports complex workflows with native producer/consumer pipeline in which producers are not blocked by concurrent consumers and consumers receive notification on complete atomic updates and see a consistent snapshot of data.

For both performance and resilience, DAOS objects support multiple distribution and redundancy schemas with fully automated and distributed recovery on storage failure. DAOS uses declustered replication and/or erasure coding over homogeneous shared-nothing servers and provides a lockless consistency model at arbitrary alignment.

Moreover, the DAOS stack abstracts multi-tier storage architecture and offers a unified storage model over which multiple top-level APIs will be developed. This includes domain-specific APIs like HDF5, SCiDB, ADIOS and high-level data models like HDFS, Spark and Graph A. A POSIX namespace encapsulation inside a DAOS container is also under consideration.

Finally, the DAOS stack is proud to leverage the following open source projects:
- Mercury (https://mercury-hpc.github.io) and CaRT (https://github.com/daos-stack/cart) for network transport
- Argobots (https://github.com/pmodels/argobots) for thread management
- NVML (http://pmem.io/nvml/) for persistent memory programming
- SPDK (http://spdk.io) for NVMe device access and management
- ISA-L (https://github.com/01org/isa-l) for checksum and erasure code computation

## Project History

The project started back in 2012 with the Fast Forward Storage & I/O program supported by the U.S. DoE in which a first DAOS prototype was implemented over the Lustre filesystem and ZFS. In 2015, a follow-on program called Extreme Scale Storage and I/O (ESSIO) continued the momentum with the development of a new standalone prototype fully in userspace which is the code base provided in this repository.

## Motivation

The emergence of data-intensive applications in business, government and academia stretches the existing I/O models beyond limits. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared-nothing interface from the ground up and be able to support massively distributed storage for which failure will be the norm, while preserving low latency and high bandwidth access over the fabric.

DAOS is a complete I/O architecture that aggregates persistent memory and NVMe storage distributed across the fabric into globally-accessible object address spaces, providing consistency, availability and resiliency guarantees without compromising performance.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Build

DAOS requires a C99-capable compiler and the scons build tool to build.

DAOS depends on CaRT and some third-party libraries:
- CaRT (https://github.com/daos-stack/cart)
- Mercury (https://github.com/mercury-hpc/mercury) and CCI (wget http://cci-forum.com/wp-content/uploads/2015/12/cci-0.3.0.tar.gz) for RPC and underneath communication
  The CCI needs to be patched ("patch -p1 < xxx" to apply all patches in utils/cci), can set the CCI_CONFIG environment variable as your cci config file (an example can be found in "utils/cci/cci.ini")
  The mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic operation.
- PMIx (https://github.com/pmix/master) for collective communication.
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc used by MCL and PMIx (an example configuration is "./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external").
- NVML library (https://github.com/pmem/nvml.git) for thread-local persistent memory transaction

If all these dependencies are readily available on your machine.
    - Can execute "scons" in top DAOS source directory to build it when all the dependent modules installed.

## To build DAOS with all dependecies.
  Check out latest DAOS from repository
    # git clone https://github.com/daos-stack/daos.git

  DAOS pre-install dependencies
    Red Hat Enterprise Linux RPM software packages (or)
    equivalent for other distros)
    # yum -y install epel-release
    # yum -y install boost-devel
    # yum -y install cmake
    # yum -y install doxygen
    # yum -y install gcc-c++
    # yum -y install libevent-devel.x86_64
    # yum -y install librdmacm-devel.x86_64
    # yum -y install libtool-ltdl-devel.x86_64
    # yum -y install libuuid-devel.x86_64
    # yum -y install openssl-devel.x86_64
    # yum -y install pandoc.x86_64
    # yum -y install scons.noarch
    # yum -y install libcmocka libcmocka-devel

    Cmocka for tests (if rpm not available install from source)
    Download Cmocka from https://cmocka.org/files/1.1/cmocka-1.1.0.tar.xz
    tar xvf cmocka-1.1.0.tar.xz
    cd cmocka
    mkdir build && cd build && cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug .. && make && sudo make install

    Make sure all these below mentioned auto tools are the appropriate versions.
        m4 (GNU M4) 1.4.16
        flex 2.5.37
        autoconf (GNU Autoconf) 2.69
        automake (GNU automake) 1.13.4
        libtool (GNU libtool) 2.4.2

    If not available,
    Install all auto tools in the same directory and export them
    to PATH/LD_LIBRARY_PATH

    All patches required for installation avaiable in utils/build/

    Auto build DAOS-M setup
    ------------------------

    - Enable scons\_local # scons\_local is able to walk through all required
      dependencies for DAOS

    # cd /PATH/TO/daos_m
    # git submodule init
    # git submodule update

   - Add user PATH
    # vim ~/.scons_localrc
    Put following to ~/.scons_localrc file (Replace /USER/PATH with $PATH)
    #!/usr/bin/python
    Import('env')
    env["ENV"]["PATH"] = "/USER/PATH"

   - Build
    To build all dependencies automatically, we use scons --build-deps feature.
    # cd daos_m
    # scons --build-deps=yes install
    You can use scons TARGET_PREFIX="PATH/TO/INSTALL" to specify a different
    install directory location. Default path is: /PATH/TO/DAOS/install/
    TARGET_PREFIX creates individual folder to each component
    Create Symbolic link ln -s libdaos.so.0.0.1 libdaos.so
    Set PATH=PATH_TO_DAOS/install/bin/
    Set LD_LIBRARY_PATH=/PATH/TO/DAOS/install/lib/
    Set CPATH=PATH/TO/DAOS/install/include/

    Environment Setup for DAOS:
    ----------------------------

    export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64
    export LD_LIBRARY_PATH=/path/to/DAOS/install/lib:/path/to/DAOS/install/lib/daos_srv/:/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH
    export CPATH=/path/to/DAOS/install/include/:$CPATH
    export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin
    export PATH=/path/to/DAOS/install/bin/:/path/to/DAOS/build/src/dmg/tests:/path/to/DAOS/build/src/dsm/tests:/path/to/DAOS/build/src/dsr/tests:$PATH
    export CCI_CONFIG=/path/to/cci.ini
    export CRT_PHY_ADDR_STR="cci+tcp", for infiniband use "cci+verbs"


    Start Servers
    --------------

    orterun -np <num_servers> --report-uri ~/uri.txt ./daos_server

    Start Clients
    --------------

    orterun -np <num_clients> --ompi-server file:~/uri.txt <client_process> eg., ./daos_test

    NB: The above specified instructions has been verified only with CentOS.
    Installations in other distros might be similar with some variations.
    Please contact us in our google groups below, if you run into issues.

## Contacts
For more information on DAOS, please post to our [Google group](https://groups.google.com/forum/#!forum/daos-users).
