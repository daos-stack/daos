# Software Installation

DAOS runs on both Intel 64 and ARM64 platforms and has been successfully tested
on CentOS 7, OpenSUSE Leap 15.1, and Ubuntu 20.04 distributions.

The majority of testing has been performed on Centos 7.7 and SLES 15, with Centos being used in the majority of the test cycles.

## Software Dependencies

DAOS requires a C99-capable compiler, a Go compiler, and the scons build tool.
Moreover, the DAOS stack leverages the following open source projects:

-   [*CaRT*](https://github.com/daos-stack/cart) for high-performance
    communication leveraging advanced network capabilities.

-   [*gRPC*](https://grpc.io/) provides a secured out-of-band channel for
    DAOS administration.

-   [*PMDK*](https://github.com/pmem/pmdk.git) for persistent memory
    programming.

-   [*SPDK*](http://spdk.io/) for userspace NVMe device access and management.

-   [*FIO*](https://github.com/axboe/fio) for flexible testing of Linux I/O
    subsystems, specifically enabling validation of userspace NVMe device
    performance through fio-spdk plugin.

-   [*ISA-L*](https://github.com/01org/isa-l) for checksum and erasure code
-   [*ISA-L_Crypto*](https://github.com/01org/isa-l_crypto) for checksum
    computation.

-   [*Argobots*](https://github.com/pmodels/argobots) for thread management.

-   [*hwloc*](https://github.com/open-mpi/hwloc) for discovering system devices,
    detecting their NUMA node affinity and for CPU binding

-   [*libfabric*](https://github.com/ofiwg/libfabric) for detecting fabric
    interfaces, providers, and connection management.

The DAOS build system can be configured to download and build any missing
dependencies automatically.

## Distribution Packages

DAOS RPM packaging is currently available, and DEB packaging is under development and will be available in a future DAOS release. Integration with the [Spack](https://spack.io/) package manager is also
under consideration.

### Installing DAOS from RPMs

DAOS RPMs are available from the Intel&copy; Registration Center.
Clicking the [Intel&copy; Registration Center](https://registrationcenter.intel.com/forms/?productid=3412) link will take you to the registration center, where you will create an account. After creating an account, the following files can be downloaded:

- daos_debug.tar - _debuginfo_ packages
- daos_packages.tar - client and server main packages
- daos_source.tar - source RPMs

**Recommended steps after download:**

	sudo tar -C / -xf daos_packages.tar 
	sudo cp /opt/intel/daos_rpms/packages/daos_packages.repo /etc/yum.repos.d
	rm /opt/intel/daos_rpms/packages/libabt*
	(cd /opt/intel/daos_rpms/packages/ && createrepo .)
	sudo yum install epel-release
	sudo yum install daos-server
	sudo yum install daos-client

**Note:** *Only daos-client OR daos-server needs to be specified on the yum command line.*


## DAOS from Scratch

The following instructions have been verified with CentOS. Installations on other
Linux distributions might be similar with some variations.
Developers of DAOS may want to review the additional sections below before beginning,
for suggestions related specifically to development. Contact us in our
[*forum*](https://daos.groups.io/g/daos) for further help with any issues.

### Build Prerequisites

To build DAOS and its dependencies, several software packages must be installed
on the system. This includes scons, libuuid, cmocka, ipmctl, and several other
packages usually available on all the Linux distributions. Moreover, a Go
version of at least 1.10 is required.

An exhaustive list of packages for each supported Linux distribution is
maintained in the Docker files:

-    [CentOS](https://github.com/daos-stack/daos/blob/master/utils/docker/Dockerfile.centos.7#L53-L76)
-    [OpenSUSE](https://github.com/daos-stack/daos/blob/master/utils/docker/Dockerfile.leap.15#L16-L42)
-    [Ubuntu](https://github.com/daos-stack/daos/blob/master/utils/docker/Dockerfile.ubuntu.20.04#L21-L39)

The command lines to install the required packages can be extracted from
the Docker files by removing the "RUN" command, which is specific to Docker.
Check the [utils/docker](https://github.com/daos-stack/daos/tree/master/utils/docker)
directory for different Linux distribution versions.

Some DAOS tests use MPI.   The DAOS build process
uses the environment modules package to detect the presence of MPI.  If none
is found, the build will skip building those tests.

### DAOS Source Code

To check out the DAOS source code, run the following command:

```bash
$ git clone https://github.com/daos-stack/daos.git
```

This command clones the DAOS git repository (path referred as ${daospath}
below). Then initialize the submodules with:

```bash
$ cd ${daospath}
$ git submodule init
$ git submodule update
```

### Building DAOS & Dependencies

If all the software dependencies listed previously are already satisfied, then
type the following command in the top source directory to build the DAOS stack:

```bash
$ scons --config=force install
```

If you are a developer of DAOS, we recommend following the instructions in the
[DAOS for Development](https://daos-stack.github.io/dev/development/#building-daos-for-development) section.

Otherwise, the missing dependencies can be built automatically by invoking scons
with the following parameters:

```bash
$ scons --config=force --build-deps=yes install
```

By default, DAOS and its dependencies are installed under ${daospath}/install.
The installation path can be modified by adding the PREFIX= option to the above
command line (e.g., PREFIX=/usr/local).

!!! note
    Several parameters can be set (e.g., COMPILER=clang or COMPILER=icc) on the
    scons command line. Please see `scons --help` for all the possible options.
    Those options are also saved for future compilations.

### Environment setup

Once built, the environment must be modified to search for binaries and header
files in the installation path. This step is not required if standard locations
(e.g. /bin, /sbin, /usr/lib, ...) are used.

```bash
CPATH=${daospath}/install/include/:$CPATH
PATH=${daospath}/install/bin/:${daospath}/install/sbin:$PATH
export CPATH PATH
```

If using bash, PATH can be set up for you after a build by sourcing the script
utils/sl/utils/setup_local.sh from the daos root. This script utilizes a file
generated by the build to determine the location of daos and its dependencies.

If required, ${daospath}/install must be replaced with the alternative path
specified through PREFIX.

## DAOS in Docker

This section describes how to build and run the DAOS service in a Docker
container. A minimum of 5GB of DRAM and 16GB of disk space will be required.
On Mac, please make sure that the Docker settings under
"Preferences/{Disk, Memory}" are configured accordingly.

### Building from GitHub

To build the Docker image directly from GitHub, run the following command:

```bash
$ curl -L https://raw.githubusercontent.com/daos-stack/daos/master/utils/docker/Dockerfile.centos.7 | \
        docker build --no-cache -t daos -
```

This creates a CentOS 7 image, fetches the latest DAOS version from GitHub,
builds it, and installs it in the image.
For Ubuntu and other Linux distributions, replace Dockerfile.centos.7 with
Dockerfile.ubuntu.20.04 and the appropriate version of interest.

Once the image created, one can start a container that will eventually run
the DAOS service:

```bash
$ docker run -it -d --privileged --name server \
        -v /dev/hugepages:/dev/hugepages \
        daos
```

If Docker is being run on a non-Linux system (e.g., OSX), the export of /dev/hugepages
should be removed since it is not supported.

### Building from a Local Tree

To build from a local tree stored on the host, a volume must be created to share
the source tree with the Docker container. To do so, execute the following
command to create a docker image without checking out the DAOS source tree:

```bash
$ docker build -t daos -f utils/docker/Dockerfile.centos.7 --build-arg NOBUILD=1 .
```

Then create a container that can access the local DAOS source tree:

```bash
$ docker run -it -d --privileged --name server \
        -v ${daospath}:/home/daos/daos:Z \
        -v /dev/hugepages:/dev/hugepages \
        daos
```

${daospath} should be replaced with the full path to your DAOS source tree.
As mentioned above, the export of /dev/hugepages should be removed if the
host is not a Linux system.

Then execute the following command to build and install DAOS in the
container:

```bash
$ docker exec server scons --build-deps=yes install PREFIX=/usr
```

### Simple Docker Setup

The `daos_server_local.yml` configuration file sets up a simple local DAOS
system with a single server instance running in the container. By default, it
uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under
/tmp. The storage size can be changed in the yaml file if necessary.

The DAOS service can be started in the docker container as follows:

```bash
$ docker exec server mkdir /var/run/daos_server
$ docker exec server daos_server start \
        -o /home/daos/daos/utils/config/examples/daos_server_local.yml
```

Once started, the DAOS server waits for the administrator to format the system.
This can be triggered in a different shell, using the following command:

```bash
$ docker exec server dmg -i storage format
```

Upon successful completion of the format, the storage engine is started, and pools
can be created using the daos admin tool (see next section).

!!! note
    Please make sure that the uio_pci_generic module is loaded.

For more advanced configurations involving SCM, SSD or a real fabric, please
refer to the next section.
