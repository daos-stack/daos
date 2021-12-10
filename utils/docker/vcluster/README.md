# Using DAOS with Docker

This section describes how to build and deploy Docker images allowing to simulate a small cluster
using DAOS as backend storage.  This small cluster is composed of the following three nodes:

- The `daos-server` node running a DAOS server daemon managing data storage devices such as SCM or
  NVMe disks.
- The `daos-admin` node allowing to manage the DAOS server thanks to `dmg`command.
- The `daos-client` node using the the DAOS server to store data.

At this time only emulated hardware storage are supported by this Docker platform:

- SCM (i.e. Stoarage Class Memory) are emulated with standard RAM memory.
- NVMe disks are emulated with a file device.

!!! warning
    Virtual Docker network such as [bridge](https://docs.docker.com/network/bridge/) are not yet
    well supported by DAOS.  Thus, one physical network interface (i.e. loopback interface is also
    not well supported) of the host should be chosen for being used by the containers through the
    Docker [host](https://docs.docker.com/network/host/) network.


## Prerequisites

To build and deploy the Docker images, `docker` and optionally `docker-compose` shall be available.
The docker host should have access to the [Docker Hub](https://hub.docker.com/) and
[CentOS](https://www.centos.org/) official repositories.  Finally,
[hugepages](https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt) linux kernel feature shall
be enabled on the docker host.

The platform was tested and validated with the [CentOS8](https://hub.docker.com/_/centos) official
docker image.  However other RHEL-like distributions such as
[rocky8.4](https://hub.docker.com/r/rockylinux/rockylinux) should be supported.

!!! warning
    Some distributions are not yet well supported such as
    [rocky8.5](https://hub.docker.com/r/rockylinux/rockylinux): issue with the management of
    hugepages with the [spdk](https://spdk.io/) library.


## Building Docker Images

### Building Base DAOS Image

The first image to create is the `daos-base` image which is not intetended to be used as it, but as
a base image for building the other three daos images.  This first image could be built directly
from GitHub with the following command:

```bash
$ docker build --tag daos-base:centos8 \
	https://github.com/daos-stack/daos.git#release/2.0:utils/docker/vcluster/daos-base/el8
```

This Docker file accept the following arguments:

- `RHEL_BASE_IMAGE`: Base docker image to use (default centos)
- `RHEL_BASE_VERSION`: Version of the base docker image to use (default 8)
- `BUST_CACHE`: Manage docker building cache (default undefined).  To invalidate the cache, a random
	value such as the date of the day shall be given.
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "no")
- `DAOS_REPOS`: Space separated list of repos needed to install DAOS (default
	"https://packages.daos.io/v2.0")
- `DAOS_GPG_KEYS`: Space separated list of GPG keys associated with DAOS repos (default
	"https://packages.daos.io/RPM-GPG-KEY")
- `DAOS_REPOS_NOAUTH`: Space separated list of repos to use without GPG authentication
	(default "")

For example, building a DAOS base image with authentication enabled could be done with the
following command:

```bash
$ docker build --tag daos-base:centos8 --build-arg DAOS_AUTH=yes \
	https://github.com/daos-stack/daos.git#release/2.0:utils/docker/vcluster/daos-base/el8
```

It is also possible to build the `daos-base` image from a local tree with the following command:

```bash
$ docker build --tag daos-base:centos8 utils/docker/vcluster/daos-base/el8
```

### Building DAOS Nodes Images

The three images `daos-server`, `daos-admin` and `daos-client` could be built directly from GitHub
or from a local tree in the same way as for the `daos-base` image.  Following command could be used
to build directly the three images from GitHub:

```bash
$ for image in daos-server daos-admin daos-client ; do \
	docker build --tag "$image:centos8" \
		"https://github.com/daos-stack/daos.git#release/2.0:utils/docker/vcluster/$image/el8"; \
  done
```

The Docker file of the `daos-server` image accept the following arguments:

- `DAOS_BASE_IMAGE`: Base docker image to use (default daos-base)
- `DAOS_BASE_VERSION`: Version of the base docker image to use (default centos8)
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "no")
- `DAOS_HUGEPAGES_NBR`: Number of huge pages to allocate for SPDK (default 4096)
- `DAOS_SCM_SIZE`: Size in GB of the RAM emulating SCM devices (default 4)
- `DAOS_BDEV_SIZE`: Size in GB of the file created to emulate NVMe devices (default 16)
- `DAOS_IFACE_NAME`: Fabric network interface used by the DAOS engine (default "eth0")

!!!note
   The IP address of the networkf interface referenced by the `DAOS_IFACE_NAME` argument will be
   required when starting DAOS.

The Dockerfile of the `daos-client` and `daos-admin` images accept the following arguments:

- `DAOS_BASE_IMAGE`: Base docker image to use (default daos-base)
- `DAOS_BASE_VERSION`: Version of the base docker image to use (default centos8)
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "no")
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

## Running DAOS Nodes Container

### Starting Containers with docker command

Once the images are created, the containers could be directly started with docker with the following
commands:

```bash
$ export DAOS_IFACE_IP=x.x.x.x
$ docker run --detach --privileged --name=daos-server --hostname=daos-server \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--volume=/dev/hugepages:/dev/hugepages  --tmpfs=/run --network=host \
	daos-server:centos8
$ docker run --detach --privileged --name=daos-agent --hostname=daos-agent \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--tmpfs=/run --network=host daos-agent:centos8
$ docker run --detach --privileged --name=daos-client --hostname=daos-client \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--tmpfs=/run --network=host daos-client:centos8
```

The value of the `DAOS_IFACE_IP` shall be replaced with the one of the network interface which was
provided when the images have been built.

Once started, the DAOS server waits for the administrator to format the system.
This can be done using the following command:

```bash
$ docker exec daos-admin dmg -i storage format
```

Upon successful completion of the format, the storage engine is started, and pools
can be created using the daos admin tool.  For more advanced configurations and usage refer to the
section [DAOS Tour](https://docs.daos.io/QSG/tour/).


### Starting Containers with docker-compose command

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


### Managing Virtual Docker cluster with daos-cm.sh

From a local tree, the bash script `utils/docker/vcluster/daos-cm.sh` could be used to start the
containers and setup a simple DAOS system composed of the following elements:

- 1 DAOS pool of 10GB (i.e. size of the pool is configurable)
- 1 DAOS POSIX container mounted on /mnt/daos-posix-fs

This script could also be used to respectively stop and monitor the containers.

More details on the usage of `daos-cm.sh` command could be found with running the following command:

```bash
$ utils/docker/vcluster/daos-cm.sh --help
```
