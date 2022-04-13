# Software Installation

Please check the [support matrix](https://docs.daos.io/v1.2/release/support_matrix/)
to select the appropriate software combination.

## Distribution Packages

DAOS packages are available for CentOS7 and openSUSE Leap 15.
For other distribution, manual building is required (see next section).

### CentOS 7.9

Configure the DAOS package repository as follows:
``` bash
$ sudo wget -O /etc/yum.repos.d/daos-packages.repo https://packages.daos.io/v1.2/CentOS7/packages/x86_64/daos_packages.repo
```

epel-release is required for both client and server:
``` bash
$ sudo yum install -y epel-release
```

To install the DAOS client packages:
``` bash
$ sudo yum install -y daos-client
```

To install the DAOS server packages:
``` bash
$ sudo yum install -y daos-server
```

Debug and source RPMs available in the respective [debug](https://packages.daos.io/v1.2/CentOS7/debug/x86_64/daos_debug.repo)
and [source](https://packages.daos.io/v1.2/CentOS7/source/daos_source.repo)
repositories.

### openSUSE Leap 15

Add the DAOS package repository via zypper:
``` bash
$ sudo zypper ar https://packages.daos.io/v1.2/Leap15/packages/x86_64/ daos_packages
$ sudo rpm --import https://packages.daos.io/RPM-GPG-KEY
$ sudo zypper --non-interactive ref
```
To install the DAOS client packages:
``` bash
$ sudo zypper install -y daos-client
```

To install the DAOS server packages:
``` bash
$ sudo zypper install -y daos-server
```

Debug and source RPMs available in the respective [debug](https://packages.daos.io/v1.2/Leap15/debug/x86_64/daos_debug.repo)
and [source](https://packages.daos.io/v1.2/Leap15/source/daos_source.repo)
repositories.

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
maintained in the Docker files (please click on the link):

-    [CentOS 7](https://github.com/daos-stack/daos/blob/release/1.2/utils/docker/Dockerfile.centos.7)
-    [openSUSE Leap 15](https://github.com/daos-stack/daos/blob/release/1.2/utils/docker/Dockerfile.leap.15)
-    [Ubuntu 20.04](https://github.com/daos-stack/daos/blob/release/1.2/utils/docker/Dockerfile.ubuntu.20.04)

The command lines to install the required packages can be extracted from
the Docker files by removing the "RUN" command, which is specific to Docker.
Check the [utils/docker](https://github.com/daos-stack/daos/blob/release/1.2/utils/docker/)
directory for different Linux distribution versions.

Some DAOS tests use MPI. The DAOS build process uses the environment modules
package to detect the presence of MPI. If none is found, the build will skip
building those tests.

### DAOS Source Code

The DAOS repository is hosted on [GitHub](https://github.com/daos-stack/daos/).

To checkout the latest 1.2 version, simply run:

```bash
$ git clone --recurse-submodules -b release/1.2 https://github.com/daos-stack/daos.git
```

This command clones the DAOS git repository (path referred as ${daospath}
below) and initializes all the submodules automatically.

### Building DAOS and Dependencies

If all the software dependencies listed previously are already satisfied, then
type the following command in the top source directory to build the DAOS stack:

```bash
$ scons --config=force install
```

If you are a developer of DAOS, we recommend following the instructions in the
[DAOS for Development](https://docs.daos.io/v1.2/dev/development/#building-daos-for-development)
section.

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

### Building a Docker Image

To build the Docker image directly from GitHub, run the following command:

```bash
$ docker build https://github.com/daos-stack/daos.git#release/1.2 \
        -f utils/docker/Dockerfile.centos.7 -t daos
```

or from a local tree:

```bash
$ docker build  . -f utils/docker/Dockerfile.centos.7 -t daos
```

This creates a CentOS 7 image, fetches the latest DAOS version from GitHub,
builds it, and installs it in the image.
For Ubuntu and other Linux distributions, replace Dockerfile.centos.7 with
Dockerfile.ubuntu.20.04 and the appropriate version of interest.

### Simple Docker Setup

Once the image created, one can start a container that will eventually run
the DAOS service:

```bash
$ docker run -it -d --privileged --cap-add=ALL --name server -v /dev:/dev daos
```

!!! note
    If you want to be more selective with the devices that are exported to the
    container, individual devices should be listed and exported as volume via
    the -v option. In this case, the hugepages devices should also be added
    to the command line via -v /dev/hugepages:/dev/hugepages and
    -v /dev/hugepages-1G:/dev/hugepages-1G

!!! warning
    If Docker is being run on a non-Linux system (e.g., OSX), -v /dev:/dev
    should be removed from the command line.

The `daos_server_local.yml` configuration file sets up a simple local DAOS
system with a single server instance running in the container. By default, it
uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under
/tmp. The storage size can be changed in the yaml file if necessary.

The DAOS service can be started in the docker container as follows:

```bash
$ docker exec server daos_server start \
        -o /home/daos/daos/utils/config/examples/daos_server_local.yml
```

!!! note
    Please make sure that the uio_pci_generic module is loaded on the host.

Once started, the DAOS server waits for the administrator to format the system.
This can be triggered in a different shell, using the following command:

```bash
$ docker exec server dmg -i storage format
```

Upon successful completion of the format, the storage engine is started, and pools
can be created using the daos admin tool (see next section).

For more advanced configurations involving SCM, SSD or a real fabric, please
refer to the next section.
