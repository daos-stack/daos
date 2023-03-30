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
- [DAOS 2.2](https://docs.daos.io/v2.2/) official [RPMS](https://packages.daos.io/v2.2/)
- [rockylinux/rockylinux:8.6](https://hub.docker.com/r/rockylinux/rockylinux/) official docker
  images.


## Building DAOS Cloud Base Docker Image

This section describes how to build the base Docker image used for building the DAOS docker images
of the following sections.  The easiest way is to use the `docker compose` sub command.  The first
step is to update the docker environment file `utils/docker/examples/client/.env` according to the
targeted DAOS system.  The following environment variables must be defined for being able to
properly build a docker image:
- `DAOS_CLIENT_UNAME`: User name of the client (e.g. "foo")
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GNAME`: Group name of the client (e.g., "bar")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

!!! note
    If the node running the docker host is using a name service such as NIS or LDAP, it could be
    more adapted to export this service inside the docker container.

The following environment variables allow to customize the Docker image to build:
- `BUST_CACHE`: Manage docker building cache (default "").  To invalidate the cache, a random value
  such as the date of day shall be given.
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")
- `RHEL_BASE_IMAGE_NAME`: Base docker image name to use (default "rockylinux/rockylinux")
- `RHEL_BASE_IMAGE_TAG`: Tag identifier of the base docker image to use (default "8.6")
- `DAOS_REPOS`: Space separated list of repos needed to install DAOS (default
  "https://packages.daos.io/v2.2/EL8/packages/x86\_64/")
- `DAOS_GPG_KEYS`: Space separated list of GPG keys associated with DAOS repos (default
   "https://packages.daos.io/RPM-GPG-KEY")
- `DAOS_REPOS_NOAUTH`: Space separated list of repos to use without GPG authentication
   (default "")
- `DAOS_VERSION`: Version of DAOS to use (default "2.2.0-4.el8")

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_base.yml build
```


## DAOS Client Containerized with Bare Metal DAOS Agent

With the deployment solution presented in this section, the DAOS client is running in a docker
container and the DAOS Agent is running on the docker host node.

### Building DAOS Client Docker Image

This section describes how to build Docker container allowing to access a DAOS file system through
a DAOS agent running on the docker host.  The easiest way is to use the `docker compose` sub
command.  The first step is to update the docker environment file
`utils/docker/examples/client/.env` according to the targeted DAOS system.

The following environment variables allow to customize the Docker image to build:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")

The docker image could be then created thanks to the following command:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client.standalone.yml build
```

!!! note
    It is not needed to copy or share the certificates of the DAOS agent running on the docker host
    in the Docker image.

### Running DAOS Client Docker Image

This section presents how to run some relevant use cases with a docker image build according to the
previous section.  Firstly the following environment variables of the docker environment file
`utils/docker/examples/client/.env` must be defined:
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

It could also be needed to define the following environment variable according to the configuration
of DAOS agent running on the docker host:
- `DAOS_AGENT_RUNTIME_DIR`: Directory containing the DAOS agent socket (default `/var/run/daos_agent`)

When the environment file has been properly filled, the `daos pool autotest` could be run thanks to
the following commands:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client.standalone.yml run --rm daos_client
$ daos pool autotest <POOL ID>
```

With the same prerequites, the [fio](https://fio.readthedocs.io/) file system benchmark tool could
be run thanks to the following commands:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client.standalone.yml run --rm daos_client
$ mkdir -p "/home/<DAOS_CLIENT_UNAME>/mnt"
$ daos container create --type=posix --label=posix-fs tank
$ dfuse --mountpoint="/home/<DAOS_CLIENT_UNAME>/mnt" --pool=tank --container=posix-fs
$ df --human-readable --type=fuse.daos
$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting --directory="/home/<DAOS_CLIENT_UNAME>/mnt"
```

### Docker Host Configuration

When a Docker Engine service is installed on a node it creates a virtual interface `docker0` which
could be misused by the DAOS agent.  To overcome this issue, the `fabric_ifaces` section of the
`daos_agent.yml` configuration file could be used, as illustrated on the following example:
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
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.standalone.yml build daos_client
```

### Building DAOS Agent Docker Image

This section describes how to build the Docker container running the DAOS agent service allowing the
DAOS client container to access a DAOS file system.  The following environment variables must be
defined for being able to properly build the docker image:
- `DAOS_ACCESS_POINTS`: List of DAOS management server access points (e.g. "['hostname1',
  'hostname2']")
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")

The following environment variables allow to customize the Docker images to build:
- `DAOS_PORT`: DAOS access point port number to connect (default "1001")
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")
- `DAOS_IFACE_CFG`: Enable manual configuration of the interface to use by the agent (default "yes")
- `DAOS_IFACE_NUMA_NODE`: Numa node of the interface to use by the agent (default "0").  Defining
  this variable is mandatory when `DAOS_IFACE_CFG` is enabled.
- `DAOS_IFACE_NAME`: Name of the interface to use by the agent (default "").  Defining this variable
  is mandatory when `DAOS_IFACE_CFG` is enabled.
- `DAOS_IFACE_DOMAIN_NAME`: Domain name of the interface to use by the agent (default "eth0").
  Defining this variable is mandatory when `DAOS_IFACE_CFG` is enabled.

!!! warning
    On most of the system the`DAOS_IFACE_CFG` should be enabled to avoid the DAOS agent service of
    using an invalid network interface such as the `docker0` virtual network interface.

When the environment file has been properly filled, the docker image could be created thanks to the
following command:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.standalone.yml build daos_agent
```

### Running DAOS Docker Images

This section presents how to run some relevant use cases with a docker image build according to the
previous section.  In a first time the following environment variables of the docker environment
file `utils/docker/examples/client/.env` must be defined:
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

In a second time, a tarball (i.e. `tar` archive compressed with `xz`) of the DAOS certificate files
should be created when the DAOS authentication is enabled.  For using Docker Compose the tarball of
the certificates file path readable by all users and its file path defined in the following variable
of the docker environment file `utils/docker/examples/client/.env`:
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
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.standalone.yml up --detach daos_agent
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.standalone.yml run --rm daos_client
$ daos pool autotest <POOL ID>
```

With the same prerequites, the [fio](https://fio.readthedocs.io/) file system benchmark tool could
be run thanks to the following commands:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.standalone.yml up --detach daos_agent
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.standalone.yml run --rm daos_client
$ mkdir -p "/home/<DAOS_CLIENT_UNAME>/mnt"
$ daos container create --type=posix --label=posix-fs tank
$ dfuse --mountpoint="/home/<DAOS_CLIENT_UNAME>/mnt" --pool=tank --container=posix-fs
$ df --human-readable --type=fuse.daos
$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting --directory="/home/<DAOS_CLIENT_UNAME>/mnt"
```


## DAOS Client and Agent Gathered

With the deployment solution presented in this section, the DAOS client and the DAOS Agent are
running in the same container.

### Building DAOS Client Docker Image

This section describes how to build the `daos-client_agent` docker image.

The easiest way to build this image is to use the `docker compose` sub command.  The first step is
to update the docker environment file `utils/docker/examples/client/.env` according to the targeted
DAOS system.  The following environment variables must be defined for being able to properly build
the docker image:
- `DAOS_DOCKER_IMAGE_TAG`: Tag identifier of the DAOS client docker image (default "rocky8.6")
- `DAOS_CLIENT_UNAME`: User name of the client (e.g. "foo")
- `DAOS_CLIENT_GNAME`: Group name of the client (e.g., "bar")

When the environment file has been properly filled, the docker image could be created thanks to the
following commands:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.gathered.yml build daos_client_agent
```

### Running DAOS Docker Containers

This section presents two different way for running some relevant use cases with a docker image
build according to the previous section.  For both methods, a tarball (i.e. `tar` archive compressed
with `xz`) of the DAOS certificate files should be created when the DAOS authentication is enabled.
However, it is not managed in the same way with both solutions.

#### Running DAOS Docker Images with Docker Compose

For using Docker Compose the tarball of the certificates file path readable by all users and its
file path defined in the following variable of the docker environment file
`utils/docker/examples/client/.env`:
- `DAOS_AGENT_CERTS_TXZ`: tarball containing the DAOS certificated needed by the DAOS agent
  (e.g. "secrets/daos\_agent-certs.txz")
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

When the environment file has been properly filled, then an application such as `daos pool autotest`
could be run in the following way:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.gathered.yml run --rm daos_client_agent
$ daos pool autotest <POOL ID>
```

With the same prerequites as for the previous section, the [fio](https://fio.readthedocs.io/) file
system benchmark tool could be run thanks to the following commands:
```bash
docker compose --file utils/docker/examples/client/docker-compose.daos_client_agent.gathered.yml run --rm daos_client_agent
$ mkdir -p "/home/<DAOS_CLIENT_UNAME>/mnt"
$ daos container create --type=posix --label=posix-fs tank
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
bash utils/docker/examples/client/deploy-docker_stack.sh utils/docker/examples/client/docker-stack.daos_client_agent.gathered.yml
docker exec -u${DAOS_CLIENT_UID}:${DAOS_CLIENT_GID} -ti <DAOS CLIENT DOCKER CONTAINER ID> bash
$ daos pool autotest <POOL ID>
```

At this time, it is not possible to use
[DFuse](https://docs.daos.io/v2.2/user/filesystem/?h=dfuse#dfuse-daos-fuse) inside a stack deployed
in swarm mode.  Indeed, the docker option
[devices](https://docs.docker.com/compose/compose-file/compose-file-v3/#devices) is not supported,
and thus it is not possible to export the `dev/fuse` device needed by DFuse.
