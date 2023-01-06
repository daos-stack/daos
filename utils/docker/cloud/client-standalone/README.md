# DAOS in Docker

This document describes how to build and deploy base Docker images for running application
using a DAOS storage system.  The DAOS agent should be run on the docker host and some resources
should be shared with its containers.


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


## Building DAOS Client Docker Image

This section describes how to build Docker container allowing to access a DAOS file system through
a DAOS agent running on the docker host.  The easiest way is to use the `docker compose` sub
command.  The first step is to update the docker environment file `utils/docker/cloud/.env`
according to the targeted DAOS system.  The following environment variables must be defined for
being able to properly build a docker image:
- `DAOS_CLIENT_UNAME`: User name of the client (e.g. "foo")
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GNAME`: Group name of the client (e.g., "bar")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

!!! note
    If the node running the docker host is using a name service such as NIS or LDAP, it could be
    more adapted to export this service inside the docker container.

The following environment variables allow to customize the Docker image to build:
- `BUST_CACHE`: Manage docker building cache (default undefined).  To invalidate the cache, a random
   value such as the date of day shall be given.
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
docker compose --file utils/docker/cloud/docker-compose.yml build
```

!!! note
    It is not needed to copy or share the certificates of the DAOS agent running on the docker host
    in the Docker image.


## Running DAOS Client Docker Image

This section presents how to run the `daos pool autotest` subcommand with a docker image build
according to the previous section.  For both method the following environment variables of the
docker environment file `utils/docker/cloud/.env` must be defined:
- `DAOS_CLIENT_UID`: User id of the client (e.g., "666")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

It could also be needed to define the following environment variable according to the configuration
of the DAOS agent running on the docker host:
- `DAOS_AGENT_RUNTIME_DIR`: Directory containing the DAOS agent socket (default
  `/var/run/daos_agent`)

The first way is to launch an interactive shell in a `daos_client` docker container and then run the
`daos` command.
```bash
docker compose run --file utils/docker/cloud/docker-compose.yml run
$ daos pool autotest <POOL ID>
```

A second way is to directly run the `daos` command with overloading the entrypoint of the
`daos_client` docker container.
```bash
docker compose --file=utils/docker/cloud/client-standalone/docker-compose.yml run --entrypoint=/usr/bin/daos daos_client pool auotest <POOL ID>
```


## Docker Host Configuration

When a docker service is installed on a node it creates a virtual interface `docker0` which could be
misused by the DAOS agent.  To overcome this issue, the `fabric_ifaces` section of the
`daos_agent.yml` configuration file could be used, as illustrated on the following example:
```yaml
fabric_ifaces:
- numa_node: 0
  devices:
  - iface: eth0
    domain: eth0
```
