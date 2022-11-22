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

To build and deploy the Docker images, `docker` and optionally `docker-compose` shall be available.
The docker host should have access to the [Docker Hub](https://hub.docker.com/) and
[Rocky Linux](https://rockylinux.org/) official repositories.  Finally,
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

The platform was tested and validated with the
[rockylinux/rockylinux:8.5](https://hub.docker.com/r/rockylinux/rockylinux) official docker images.
However other RHEL-like distributions should be supported.

!!! warning
    Some distributions are not yet well supported such as
    [rockylinux/rockylinux:8.5](https://hub.docker.com/r/rockylinux/rockylinux): issue with the
    management of hugepages with the [spdk](https://spdk.io/) library.



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

The first image to create is the `daos-base` image which is not intetended to be used as it, but as
a base image for building the other three daos images.  This first image could be built directly
from GitHub with the following command:

```bash
$ docker build --tag daos-base:rocky8.4 \
	https://github.com/daos-stack/daos.git#master:utils/docker/vcluster/daos-base/el8
```

This Docker file accept the following arguments:

- `RHEL_BASE_IMAGE`: Base docker image to use (default "rockylinux/rockylinux")
- `RHEL_BASE_VERSION`: Version of the base docker image to use (default "8.4")
- `BUST_CACHE`: Manage docker building cache (default undefined).  To invalidate the cache, a random
	value such as the date of the day shall be given.
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")
- `DAOS_REPOS`: Space separated list of repos needed to install DAOS (default
	"https://packages.daos.io/v2.0/EL8/packages/x86_64/")
- `DAOS_GPG_KEYS`: Space separated list of GPG keys associated with DAOS repos (default
	"https://packages.daos.io/RPM-GPG-KEY")
- `DAOS_REPOS_NOAUTH`: Space separated list of repos to use without GPG authentication
	(default "")

For example, building a DAOS base image, with authentication disabled, could be done with the
following command:

```bash
$ docker build --tag daos-base:rocky8.4 --build-arg DAOS_AUTH=no \
	https://github.com/daos-stack/daos.git#master:utils/docker/vcluster/daos-base/el8
```

It is also possible to build the `daos-base` image from a local tree with the following command:

```bash
$ docker build --tag daos-base:rocky8.4 utils/docker/vcluster/daos-base/el8
```

### DAOS Nodes Images

The three images `daos-server`, `daos-admin` and `daos-client` could be built directly from GitHub
or from a local tree in the same way as for the `daos-base` image.  Following command could be used
to build directly the three images from GitHub:

```bash
$ for image in daos-server daos-admin daos-client ; do \
	docker build --tag "$image:rocky8.4" \
		"https://github.com/daos-stack/daos.git#master:utils/docker/vcluster/$image/el8"; \
  done
```

The Docker file of the `daos-server` image accept the following arguments:

- `DAOS_BASE_IMAGE`: Base docker image to use (default "daos-base")
- `DAOS_BASE_VERSION`: Version of the base docker image to use (default "rocky8.4")
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")
- `DAOS_HUGEPAGES_NBR`: Number of huge pages to allocate for SPDK (default 4096)
- `DAOS_SCM_SIZE`: Size in GB of the RAM emulating SCM devices (default 4)
- `DAOS_BDEV_SIZE`: Size in GB of the file created to emulate NVMe devices (default 16)
- `DAOS_IFACE_NAME`: Fabric network interface used by the DAOS engine (default "eth0")

!!! note
    The IP address of the network interface referenced by the `DAOS_IFACE_NAME` argument will be
    required when starting DAOS.

The Dockerfile of the `daos-client` and `daos-admin` images accept the following arguments:

- `DAOS_BASE_IMAGE`: Base docker image to use (default "daos-base")
- `DAOS_BASE_VERSION`: Version of the base docker image to use (default "rocky8.4")
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")
- `DAOS_ADMIN_USER`: Name or uid of the daos administrattor user (default "root")
- `DAOS_ADMIN_GROUP`: Name or gid of the daos administrattor group (default "root")

From a local tree, a more straightforward way to build these images could be done with
`docker-compose` and the following commands:

```bash
$ docker-compose --file utils/docker/vcluster/docker-compose.yml -- build
```

The same arguments are accepted but they have to be defined in the Docker Compose environment file
`utils/docker/vcluster/.env`.

!!! warning
    For working properly, the DAOS authentication have to be enabled in all the images (i.e. nodes
    images and base image).

## Running the DAOS Containers

### Via Docker Commands

Once the images are created, the containers could be directly started with docker with the following
commands:

```bash
$ export DAOS_IFACE_IP=x.x.x.x
$ docker run --detach --privileged --name=daos-server --hostname=daos-server \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--volume=/dev/hugepages:/dev/hugepages  --tmpfs=/run --network=host \
	daos-server:rocky8.4
$ docker run --detach --privileged --name=daos-agent --hostname=daos-agent \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--tmpfs=/run --network=host daos-agent:rocky8.4
$ docker run --detach --privileged --name=daos-client --hostname=daos-client \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--tmpfs=/run --network=host daos-client:rocky8.4
```

The value of the `DAOS_IFACE_IP` shall be replaced with the one of the network interface which was
provided when the images have been built.

Once started, the DAOS server waits for the administrator to format the system.
This can be done using the following command:

```bash
$ docker exec daos-admin dmg -i storage format
```

Upon successful completion of the format, the storage engine is started, and pools
can be created using the daos admin tool.


### Via docker-compose

From a local tree, a more straightforward way to start the containers could be done with
`docker-compose` and the following commands:

```bash
$ docker-compose --file utils/docker/vcluster/docker-compose.yml -- up --detach
```

!!! note
    Before starting the containers with `docker-compose`, the IP address of the network interface,
    which was provided when the images have been built, shall be defined in the Docker
    Compose environment file `utils/docker/vcluster/.env`.

As with the docker command, the system shall be formatted, pools created, etc..


### Via Custom Scripts

From a local tree, the bash script `utils/docker/vcluster/daos-cm.sh` could be used to start the
containers and setup a simple DAOS system composed of the following elements:

- 1 DAOS pool of 10GB (i.e. size of the pool is configurable)
- 1 DAOS POSIX container mounted on /mnt/daos-posix-fs

This script could also be used to respectively stop and monitor the containers.

More details on the usage of `daos-cm.sh` command could be found with running the following command:

```bash
$ utils/docker/vcluster/daos-cm.sh --help
```
