# DAOS in Docker

This document describes different ways to build and deploy base Docker images for running
application using a DAOS storage system.


## Prerequisites

To build and deploy the Docker images, `docker` cli shall be available.
The docker host should have access to the [Docker Hub](https://hub.docker.com/) and
[Rocky Linux](https://rockylinux.org/) official repositories.

The platform was tested and validated with the following dependencies:
- [Docker CE](https://docs.docker.com/engine/install/centos/) latest
  [RPMs](https://download.docker.com/linux/centos/docker-ce.repo)
- [DAOS 2.4](https://docs.daos.io/v2.4/) official [RPMS](https://packages.daos.io/v2.4/)
- [rockylinux/rockylinux:8.6](https://hub.docker.com/r/rockylinux/rockylinux/) official docker
  images.


## Building DAOS Cloud Base Docker Image

This section describes how to build the base Docker image used for building the DAOS docker images
of the following sections.  The easiest way is to use the `docker compose` sub command.  The first
step is to update the docker environment file `utils/docker/examples/.env` according to the
targeted DAOS system.

The following environment variables allow to customize the Docker image to build:
- `BUST_CACHE`: Manage docker building cache (default "").  To invalidate the cache, a random value
  such as the date of day shall be given.
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")
- `RHEL_BASE_IMAGE_NAME`: Base docker image name to use (default "rockylinux/rockylinux")
- `RHEL_BASE_IMAGE_TAG`: Tag identifier of the base docker image to use (default "8.6")
- `DAOS_REPOS`: Space separated list of repos needed to install DAOS (default
  "https://packages.daos.io/v2.4/EL8/packages/x86\_64/")
- `DAOS_GPG_KEYS`: Space separated list of GPG keys associated with DAOS repos (default
   "https://packages.daos.io/RPM-GPG-KEY")
- `DAOS_REPOS_NOAUTH`: Space separated list of repos to use without GPG authentication
   (default "")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.0-2.el8")
- `DAOS_CLIENT_UNAME`: User name of the client (default "root")
- `DAOS_CLIENT_UID`: User id of the client (default  "0")
- `DAOS_CLIENT_GNAME`: Group name of the client (default "root")
- `DAOS_CLIENT_GID`: Group id of the client (default "0")

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/docker-compose.base.yml build daos_base
```

!!! note
    If the node running the docker host is using a name service such as NIS or LDAP, it could be
    more adapted to export this service inside the docker container.


## DAOS Client Containerized with Bare Metal DAOS Agent

With the deployment solution presented in this section, the DAOS client is running in a docker
container and the DAOS Agent is running on the docker host node.

### Building DAOS Client Docker Image

This section describes how to build Docker container allowing to access a DAOS file system through
a DAOS agent running on the docker host.  The easiest way is to use the `docker compose` sub
command.  The first step is to update the docker environment file
"utils/docker/examples/.env" according to the targeted DAOS system.

The following environment variables allow to customize the Docker image to build:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.0-2.el8")

The docker image could be then created thanks to the following command:
```bash
docker compose --file utils/docker/examples/docker-compose.client_bm.yml build daos_client
```

!!! note
    It is not needed to copy or share the certificates of the DAOS agent running on the docker host
    in the Docker image.

### Running DAOS Client Docker Image

This section presents how to run some relevant use cases with a docker image build according to the
previous section.  Firstly the following environment variables of the docker environment file
"utils/docker/examples/.env" must be defined:
- `DAOS_CLIENT_UID`: User id of the client (e.g.  "0")
- `DAOS_CLIENT_GID`: Group id of the client (e.g. "0")
- `DAOS_AGENT_RUNTIME_DIR`: Directory containing the DAOS agent socket (e.g. "/var/run/daos\_agent")

When the environment file has been properly filled, the `daos pool autotest` could be run thanks to
the following commands:
```bash
docker compose --file utils/docker/examples/docker-compose.client_bm.yml run --rm daos_client
$ daos pool autotest <POOL NAME>
```

With the same prerequites, the [fio](https://fio.readthedocs.io/) file system benchmark tool could
be run thanks to the following commands:
```bash
docker compose --file utils/docker/examples/docker-compose.client_bm.yml run --rm daos_client
$ mkdir -p "/home/<DAOS_CLIENT_UNAME>/mnt"
$ daos container create --type=posix tank posix-fs
$ dfuse --mountpoint="/home/<DAOS_CLIENT_UNAME>/mnt" --pool=tank --container=posix-fs
$ df --human-readable --type=fuse.daos
$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting --directory="/home/<DAOS_CLIENT_UNAME>/mnt"
```

### Configuring Bare Metal DAOS Agent

When a Docker Engine service is installed on a node it creates a virtual interface `docker0` which
could be misused by the DAOS agent.  To overcome this issue, the `fabric_ifaces` section of the
"daos\_agent.yml" configuration file could be used, as illustrated on the following example:
```yaml
fabric_ifaces:
- numa_node: 0
  devices:
  - iface: eth0
    domain: eth0
```


## DAOS Client and Agent Containerized

With the deployment solution presented in this section, the DAOS client and the DAOS Agent are
running in two different docker containers.

### Building DAOS Client Docker Image

This image is using the same environment variables as the DAOS Client Docker image of the previous
section.

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/docker-compose.client_sa.yml build daos_client
```

### Building DAOS Agent Docker Image

This section describes how to build the Docker container running the DAOS agent service allowing the
DAOS client container to access a DAOS file system.  The first step is to create a "daos\_agent.yml"
configuration file and to place it in the directory "utils/docker/examples/daos-agent/el8".

!!! warning
    As for the bare metal DAOS agent, the `fabric_ifaces` section of the "daos\_agent.yml"
    configuration file should be defined.

In a second time, the following environment variables of the docker environment file
`utils/docker/examples/.env`, could be defined to customize the Docker images to build:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.0-2.el8")

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/docker-compose.client_sa.yml build daos_agent
```

### Running DAOS Docker Images

This section presents how to run some relevant use cases with a docker image build according to the
previous section.  In a first time the following environment variables of the docker environment
file `utils/docker/examples/.env` must be defined:
- `DAOS_CLIENT_UID`: User id of the client (e.g.  "0")
- `DAOS_CLIENT_GID`: Group id of the client (e.g. "0")

In a second time, a tarball (i.e. `tar` archive compressed with `xz`) of the DAOS certificate files
should be created when the DAOS authentication is enabled.  For using Docker Compose the tarball of
the certificates file path should be readable by all users and its file path defined in the
following variable of the docker environment file `utils/docker/examples/.env`:
- `DAOS_AGENT_CERTS_TXZ`: tarball containing the DAOS certificated needed by the DAOS agent
  (e.g. "secrets/daos\_agent-certs.txz")

!!! note
    As [Docker Secret](https://docs.docker.com/engine/swarm/secrets/) is a Docker Swarm service, it
    could not be used properly with Docker Compose.  With Docker Compose, secret are managed as
    standard Docker Volume mounted in `/run/secrets` directory.  More details could be found at:
    https://github.com/docker/compose/issues/9139#issuecomment-1098137264

!!! note
    For properly managing secret, Docker Stack should be used instead of Docker Compose.  Sadly, the
    `pid=host` option is not yet supported in swarm mode, and this last one is mandatory to allow
    the DAOS Agent to monitor its associated clients. More details could be found at:
    https://github.com/docker/docs/issues/5624 and https://github.com/moby/swarmkit/issues/1605

When the environment file has been properly filled, then an application such as `daos pool autotest`
could be run in the following way:
```bash
docker compose --file utils/docker/examples/docker-compose.client_sa.yml up --detach daos_agent
docker compose --file utils/docker/examples/docker-compose.client_sa.yml run --rm daos_client
$ daos pool autotest <POOL NAME>
```

With the same prerequites, the [fio](https://fio.readthedocs.io/) file system benchmark tool could
be run thanks to the following commands:
```bash
docker compose --file utils/docker/examples/docker-compose.client_sa.yml up --detach daos_agent
docker compose --file utils/docker/examples/docker-compose.client_sa.yml run --rm daos_client
$ mkdir -p "/home/<DAOS_CLIENT_UNAME>/mnt"
$ daos container create --type=posix tank posix-fs
$ dfuse --mountpoint="/home/<DAOS_CLIENT_UNAME>/mnt" --pool=tank --container=posix-fs
$ df --human-readable --type=fuse.daos
$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting --directory="/home/<DAOS_CLIENT_UNAME>/mnt"
```


## DAOS Client and Agent Gathered

With the deployment solution presented in this section, the DAOS client and the DAOS Agent are
running in the same container.

### Building DAOS Client Docker Image

This section describes how to build the `daos-client` docker image.

The easiest way to build this image is to use the `docker compose` sub command.  The first step is
to build the `daos_agent` image.  The procedure for building this image is the same as the one
described in the previous section.

In a second time, the following environment variables of the docker environment file
`utils/docker/examples/.env`, could be defined to customize the Docker images to build:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.0-2.el8")
- `DAOS_CLIENT_UNAME`: User name of the client (default "root")
- `DAOS_CLIENT_GNAME`: Group name of the client (default "root")

When the environment file has been properly filled, the docker image could be created thanks to the
following commands:
```bash
docker compose --file utils/docker/examples/docker-compose.client_gt.yml build daos_client
```

### Running DAOS Client Docker Image

This section presents two different ways for running some relevant use cases with a docker image
build according to the previous section.  For both methods, a tarball (i.e. `tar` archive compressed
with `xz`) of the DAOS certificate files should be created when the DAOS authentication is enabled.
However, it is not managed in the same way with both solutions.

#### Running DAOS Docker Images with Docker Compose

For using Docker Compose the tarball of the certificates file path should be readable by all users
and its file path defined in the following variable of the docker environment file
`utils/docker/examples/.env`:
- `DAOS_AGENT_CERTS_TXZ`: tarball containing the DAOS certificated needed by the DAOS agent
  (e.g. "secrets/daos\_agent-certs.txz")
- `DAOS_CLIENT_UID`: User id of the client (e.g.  "0")
- `DAOS_CLIENT_GID`: Group id of the client (e.g. "0")

When the environment file has been properly filled, then an application such as `daos pool autotest`
could be run in the following way:
```bash
docker compose --file utils/docker/examples/docker-compose.client_gt.yml run --rm daos_client
$ daos pool autotest <POOL NAME>
```

With the same prerequites as for the previous section, the [fio](https://fio.readthedocs.io/) file
system benchmark tool could be run thanks to the following commands:
```bash
docker compose --file utils/docker/examples/docker-compose.yml run --rm daos_client
$ mkdir -p "/home/<DAOS_CLIENT_UNAME>/mnt"
$ daos container create --type=posix tank posix-fs
$ dfuse --mountpoint="/home/<DAOS_CLIENT_UNAME>/mnt" --pool=tank --container=posix-fs
$ df --human-readable --type=fuse.daos
$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting --directory="/home/<DAOS_CLIENT_UNAME>/mnt"
```

#### Running DAOS Docker Images with Docker Stack

With Docker Stack the tarball of the certificates are managed as [Docker
Secret](https://docs.docker.com/engine/swarm/secrets/).  Docker Secret is a swarm service allowing
to securely store and access blob of data.  Recording a tarball containing the DAOS agent
certificates could be done in the following way:
```bash
docker swarm init
docker secret create daos_agent-certs <DAOS_AGENT_CERTS_TXZ>
```

As soon as the Docker secret has been created, an application such as `daos pool autotest`
could be run in the following way:
```bash
bash utils/docker/examples/deploy-docker_stack.sh utils/docker/examples/docker-stack.client_gt.yml
docker exec -u${DAOS_CLIENT_UID}:${DAOS_CLIENT_GID} -ti <DAOS CLIENT DOCKER CONTAINER ID> bash
$ daos pool autotest <POOL NAME>
```

!!! note
    At this time, it is not possible to use
    [DFuse](https://docs.daos.io/v2.4/user/filesystem/?h=dfuse#dfuse-daos-fuse) inside a stack
    deployed in swarm mode.  Indeed, the docker option
    [devices](https://docs.docker.com/compose/compose-file/compose-file-v3/#devices) is not
    supported, and thus it is not possible to export the `dev/fuse` device needed by DFuse.


## DAOS Admin Containerization

This section presents how to build and run a Docker image allowing to administrate a DAOS system.

### Building DAOS Admin Docker Image

This section describes how to build a Docker image allowing to administrate a DAOS system through
the DAOS Management Tool (i.e. dmg) CLI.  The first step is to create the "daos\_control.yml"
configuration file and to place it in the directory
"utils/docker/examples/daos-admin/el8".  Example of such configuration file could be find
in this last directory.

In a second time, the following environment variables of the docker environment file
`utils/docker/examples/.env`, could be defined to customize the Docker images to build:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS base docker image (default "rocky8.6")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.0-2.el8")

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/docker-compose.admin.yml build daos_admin
```

### Running DAOS Admin Docker Image

This section presents two different ways for running a DAOS admin container.  For both methods,
a tarball (i.e. `tar` archive compressed with `xz`) of the DAOS certificate files should be
created when the DAOS authentication is enabled.
However, it is not managed in the same way with both solutions.

#### Running DAOS Docker Images with Docker Compose

For using Docker Compose the tarball of the certificates file path should be readable by all users
and its file path defined in the following variable of the docker environment file
`utils/docker/examples/.env`:
- `DAOS_ADMIN_CERTS_TXZ`: tarball containing the DAOS certificated needed by the dmg CLI (e.g.
  "secrets/daos\_admin-certs.txz")

When the environment file has been properly filled, then a DAOS system could be formatted and a pool
created with the following commands:
```bash
docker compose --file utils/docker/examples/docker-compose.admin.yml run --rm daos_admin
$ dmg storage format
$ dmg system query --verbose
$ dmg pool create --size=100% --user=<DAOS_CLIENT_UID> --group=<DAOS_CLIENT_GID> <POOL NAME>
$ dmg pool query <POOL NAME>
```

#### Running DAOS Docker Images with Docker Stack

With Docker Stack the tarball of the certificates are managed as [Docker
Secret](https://docs.docker.com/engine/swarm/secrets/).  Docker Secret is a swarm service allowing
to securely store and access blob of data.  Recording a tarball containing the DAOS admin
certificates could be done in the following way:
```bash
docker swarm init
docker secret create daos_admin-certs <DAOS_ADMIN_CERTS_TXZ>
```

As soon as the Docker secret has been created, a DAOS system could be formatted and a pool
created with the following commands:
```bash
bash utils/docker/examples/deploy-docker_stack.sh utils/docker/examples/docker-stack.admin.yml
docker exec -ti <DAOS ADMIN DOCKER CONTAINER ID> bash
$ dmg storage format
$ dmg system query --verbose
$ dmg pool create --size=100% --user=<DAOS_CLIENT_UID> --group=<DAOS_CLIENT_GID> <POOL NAME>
$ dmg pool query <POOL NAME>
```

## DAOS Server Containerization

This section presents how to build and deploy a Docker image running a DAOS server.

### Docker Host Prerequisites

According to the targeted DAOS server configuration, the Docker host should respect the same
[hardware requirements](https://docs.daos.io/v2.4/admin/hardware) and
[system setup](https://docs.daos.io/v2.4/admin/predeployment_check/) as if the DAOS server was
running on bare metal.

The [hugepages](https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt) linux kernel feature
shall also be enabled on the docker host.  At least, 4096 pages of 2048KiB should be available.  The
number of huge pages allocated could be checked with the following command:

```bash
$ sysctl vm.nr_hugepages
```

The default size of a huge page, the number of available huge pages, etc. could be found with the
following command:

```bash
$ cat /proc/meminfo | grep -e "^Huge"
```

!!! warning
    Currently, the [VFIO](https://docs.kernel.org/driver-api/vfio.html) driver is not supported when
    the DAOS server is containerized.   This last driver should thus be
    [deactivated](https://docs.daos.io/v2.4/admin/predeployment_check/#enable-iommu) to let
    [SPDK](https://spdk.io/) use the
    [UIO](https://www.kernel.org/doc/html/v4.12/driver-api/uio-howto.html) driver.

### Building DAOS Server Docker Image

This section describes how to build a Docker image of a DAOS server.  The first step is to create
the "daos\_server.yml" configuration file and to place it in the directory
"utils/docker/examples/daos-server/el8".  Example of such configuration file could be find
in this last directory.

!!! warning
    The 'disable_vfio' yaml property of the "daos\_server.yml" configuration file shall be set to
    use the UIO driver instead of the VFIO one (which is not yet supported with docker).

In a second time, the following environment variables of the docker environment file
`utils/docker/examples/.env`, could be defined to customize the Docker images to build:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS base docker image (default "rocky8.6")
- `DAOS_VERSION`: Version of DAOS to use (default "2.4.0-2.el8")

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/docker-compose.server.yml build daos_server
```

### Running DAOS Server Docker Image

This section presents how to run the image of a containerized DAOS server thanks to docker compose.
In a first time, a tarball (i.e. `tar` archive compressed with `xz`) of the DAOS certificate files
should be created when the DAOS authentication is enabled.  For using Docker Compose the tarball of
the certificates file path should be readable by all users and its file path defined in the
following variable of the docker environment file `utils/docker/examples/.env`:
- `DAOS_SERVER_CERTS_TXZ`: tarball containing the DAOS certificated needed by the DAOS server
  (e.g. "secrets/daos\_server-certs.txz").

!!! note
    For properly managing secret, Docker Stack should be used instead of Docker Compose.  Sadly,
    several docker compose configuration options needed for running a containerized DAOS server, such
    as [privileged](https://github.com/moby/swarmkit/pull/3072), are not yet supported in swarm
    mode.

When the tarball has been created and the environment file properly filled, then a DAOS server could
be started with the following command:
```bash
docker compose --file utils/docker/examples/docker-compose.server.yml run --rm daos_server
```
