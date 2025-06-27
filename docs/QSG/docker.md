# DAOS in Docker

This section describes how to build and deploy Docker images allowing to simulate a small cluster
using DAOS as backend storage.  This small cluster is composed of the following three nodes:
- The `daos-server` node running a DAOS server daemon managing data storage devices such as SCM or
  NVMe disks.
- The `daos-admin` node allowing to manage the DAOS server thanks to `dmg`command.
- The `daos-client` node using the the DAOS server to store data.

At this time only emulated hardware storage are supported by this Docker platform:
- SCM (i.e. Storage Class Memory) are emulated with standard RAM memory.
- NVMe disks are emulated with a file device.

!!! warning
    Virtual Docker network such as [bridge](https://docs.docker.com/network/bridge/) are not yet
    well supported by DAOS.  Thus, one physical network interface (i.e. loopback interface is also
    not well supported) of the host should be chosen for being used by the containers through the
    Docker [host](https://docs.docker.com/network/host/) network.


## Prerequisites

To build and deploy the Docker images, `docker` cli shall be available.  The docker host should have
access to the [Docker Hub](https://hub.docker.com/) and [Rocky Linux](https://rockylinux.org/)
official repositories.  Finally,
[hugepages](https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt) linux kernel feature shall
be enabled on the docker host.  At least, 4096 pages of 2048kB should be available.  The number of
huge pages allocated could be checked with the following command:

```bash
$ sysctl vm.nr_hugepages
```

The default size of a huge page, the number of available huge pages, etc. could be found with the
following command:

```bash
$ cat /proc/meminfo | grep -e "^Huge"
```

The platform was tested and validated with the following dependencies:
- [Docker CE](https://docs.docker.com/engine/install/centos/) latest
  [RPMs](https://download.docker.com/linux/centos/docker-ce.repo)
- [DAOS 2.6](https://docs.daos.io/v2.6/) local RPMS builds from [DAOS master
  branch](https://github.com/daos-stack/daos/tree/master)
- [rockylinux/rockylinux:8.9](https://hub.docker.com/r/rockylinux/rockylinux/) official docker
  images.

### Configuring HugePages

First the Linux kernel needs to be built with the `CONFIG_HUGETLBFS` (present under "File systems")
and `CONFIG_HUGETLB_PAGE` (selected automatically when `CONFIG_HUGETLBFS` is selected) configuration
options.

To avoid memory fragmentation, huge pages could be allocated on the kernel boot command line by
specifying the "hugepages=N" parameter, where 'N' = the number of huge pages requested.  It is also
possible to allocate them at run time, thanks to the `sysctl` command:

```bash
$ sysctl vm.nr_hugepages=8192
```

It is also possible to use the `sysctl` command to allocate huge pages at boot time with the
following command:

```bash
$ cat <<< "vm.nr_hugepages = 8192" > /etc/sysctl.d/50-hugepages.conf
$ sysctl -p
```

## Building Docker Images

### Base DAOS Image

The first image to create is the `daos-base` image which is not intended to be used as is, but as
a base image for building the other three daos images.  The easiest way is to use the `docker
compose` sub command from a local DAOS source file tree.  The first step is to update the docker
environment file "utils/docker/examples/.env" according to the targeted DAOS system.  The following
environment variables allow to customize the Docker image to build:
- `LINUX_DISTRO`: Linux distribution identifier (default "el8")
- `DAOS_DOCKER_IMAGE_NSP`: Namespace identifier of the base DAOS docker image (default "daos")
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the base DAOS docker image (default "v2.4.1")
- `BUST_CACHE`: Manage docker building cache (default "").  To invalidate the cache, a random value
   such as the date of day shall be given.
- `LINUX_IMAGE_NAME`: Base docker image name to use (default "rockylinux/rockylinux")
- `LINUX_IMAGE_TAG`: Tag identifier of the base docker image to use (default "8.9")
- `DAOS_REPOS`: Space separated list of repos needed to install DAOS (default
  "https://packages.daos.io/v2.4.1/EL8/packages/x86\_64/")
- `DAOS_GPG_KEYS`: Space separated list of GPG keys associated with DAOS repos (default
   "https://packages.daos.io/v2.4.1/RPM-GPG-KEY-2023")
- `DAOS_REPOS_NOAUTH`: Space separated list of repos to use without GPG authentication
   (default "")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.1-2.el8")
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")

When the environment file has been properly filled, run the following command to build the base DAOS
docker image.
```bash
$ docker compose --file utils/docker/vcluster/docker-compose.yml build daos_base
```

!!! warning
    For working properly, the DAOS authentication have to be enabled in all the images (i.e. nodes
    images and base image).

### DAOS Nodes Images

To build the the three docker images `daos-server`, `daos-admin` and `daos-client`, the first step
is to update the docker environment file "utils/docker/examples/.env" according to the targeted DAOS
system.  The `daos-server`,`daos-client` and `daos-admin` images can be customize with the following
environment variables:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the base DAOS docker image to use (default "v2.4.1")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.1-2.el8")
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")

The `daos-server` image is also using the following environment variables:
- `DAOS_HUGEPAGES_NBR`: Number of huge pages to allocate for SPDK (default 4096)
- `DAOS_SCM_SIZE`: Size in GB of the RAM emulating SCM devices (default 4)
- `DAOS_BDEV_SIZE`: Size in GB of the file created to emulate NVMe devices (default 16)
- `DAOS_IFACE_NAME`: Fabric network interface used by the DAOS engine (default "eth0")
- `DAOS_MD_ON_SSD`: Enable DAOS MD-on-SSD feature when set to "yes" (default "no")

!!! note
    The IP address of the network interface referenced by the `DAOS_IFACE_NAME` argument will be
    required when starting DAOS.

The `daos-client` image is also using the following environment variables:
- `DAOS_AGENT_IFACE_CFG`: Enable manual configuration of the interface to use by the agent (default
  "yes")
- `DAOS_AGENT_IFACE_NUMA_NODE`: Numa node of the interface to use by the agent (default "0").
  Defining this variable is mandatory when `DAOS_AGENT_IFACE_CFG` is equal to "yes".
- `DAOS_AGENT_IFACE_NAME`: Name of the interface to use by the agent (default "eth0").  Defining this
  variable is mandatory when `DAOS_IFACE_CFG` is equal to "yes".
- `DAOS_AGENT_IFACE_DOMAIN_NAME`: Domain name of the interface to use by the agent (default "eth0").
  Defining this variable is mandatory when `DAOS_IFACE_CFG` is equal to "yes".

!!! warning
    On most of the system the`DAOS_IFACE_CFG` should be enabled: The DAOS Network Interface
    auto-detection could not yet be properly done inside a DAOS Agent Docker container.

When the environment file has been properly filled, run the following command to build the docker
images:
```bash
$ docker compose --file utils/docker/vcluster/docker-compose.yml build daos_server daos_admin daos_client
```

## Running the DAOS Containers

Once the images are created, the bash script `utils/docker/vcluster/daos-cm.sh` can be used to to
start the containers and setup a simple DAOS system composed of the following elements:
- 1 DAOS pool of 10GB (i.e. size of the pool is configurable)
- 1 DAOS POSIX container mounted on /mnt/daos-posix-fs
This script can also be used to respectively stop and monitor the containers.

To get more details on the usage of `daos-cm.sh` run the following command:
```bash
$ utils/docker/vcluster/daos-cm.sh --help
```
