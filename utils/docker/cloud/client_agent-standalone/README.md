# DAOS in Docker

This document describes how to build and deploy base Docker images for running application
using a DAOS storage system.


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


## Building DAOS Docker Images

This section describes how to build the following two Docker images:
- `daos_agent`: This image is used for running a DAOS agent service inside a Docker container.
- `daos_client`: This image is used for running an application accessing a DAOS file system through
  a DAOS agent running in a different container on the same Docker host.

The easiest way to build these images is to use the `docker compose` sub command.  The first step is
to update the docker environment file `utils/docker/cloud/.env` according to the targeted DAOS
system.  The following environment variables must be defined for being able to properly build
a docker image:
- `DAOS_CLIENT_UNAME`: User name of the client (e.g. "foo")
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GNAME`: Group name of the client (e.g., "bar")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")
- `DAOS_ACCESS_POINTS`: List of DAOS management server access points (e.g. "['hostname1',
  'hostname2']")

!!! note
    If the node running the docker host is using a name service such as NIS or LDAP, it could be
    more adapted to export this service inside the docker container.

The following environment variables allow to customize the Docker images to build:
- `BUST_CACHE`: Manage docker building cache (default "").  To invalidate the cache, a random value
  such as the date of day shall be given.
- `DAOS_PORT`: DAOS access point port number to connect (default "1001")
- `DAOS_AUTH`: Enable DAOS authentication when set to "yes" (default "yes")
- `DAOS_IFACE_CFG`: Enable manual configuration of the interface to use by the agent (default "no")
- `DAOS_IFACE_NUMA_NODE`: Numa node of the interface to use by the agent (default "").  Defining
  this variable is mandatory when `DAOS_IFACE_CFG` is enabled.
- `DAOS_IFACE_NAME`: Name of the interface to use by the agent (default "").  Defining this
  variable is mandatory when `DAOS_IFACE_CFG` is enabled.
- `DAOS_IFACE_DOMAIN_NAME`: Domain name of the interface to use by the agent (default "").  Defining
  this variable is mandatory when `DAOS_IFACE_CFG` is enabled.
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

When the environment file has been properly filled, the docker images could be created thanks to the
following command:
```bash
docker compose --file utils/docker/cloud/docker-compose.yml build
```

!!! warning
    On most of the system the`DAOS_IFACE_CFG` should be enabled to avoid the DAOS agent service of
    using the `docker0` virtual network interface.


## Running DAOS Docker Containers

This section presents two ways of running the `daos pool autotest` subcommand with docker images
build according to the previous section.  For both methods the following environment variables of
the docker environment file `utils/docker/cloud/.env` must be defined:
- `DAOS_CLIENT_UID`: User id of the client (e.g.,  "666")
- `DAOS_CLIENT_GID`: Group id of the client (e.g., "999")

For both methods, a tarball (i.e. `tar` archive compressed with `xz`) of the DAOS certificate files
should be created when the DAOS authentication is enabled.


### Running DAOS Application with Docker Compose

For using Docker Compose the tarball of the certificates file path readable by all users and its
file path defined in the following variable of the docker environment file
`utils/docker/cloud/.env`:
- `DAOS_AGENT_CERTS_TXZ`: tarball containing the DAOS certificated needed by the DAOS agent
  (e.g. "secrets/daos\_agent-certs.txz")

When the environment file has been properly filled, then an application such as `daos pool autotest`
could be run in the following way:
```bash
docker compose --file=utils/docker/cloud/client_agent-standalone/docker-compose.yml up --detach daos_agent
docker compose --file=utils/docker/cloud/client_agent-standalone/docker-compose.yml run --entrypoint=/usr/bin/daos daos_client pool auotest <POOL ID>
```


### Running DAOS Application with Docker Stack

With Docker Stack the tarball of the certificates are managed as [Docker
Secret](https://docs.docker.com/engine/swarm/secrets/).  Docker Secret is a swarm service allowing
to securely store and access blob of data.  Recording a tarball containing the DAOS agent
certificates could be done in the following way:
```bash
docker swarm init
docker secret create daos_agent-certs <TARBALL PATH>
```

As soon as the Docker secret has been created, an application such as `daos pool autotest`
could be run in the following way:
```bash
bash utils/docker/cloud/client_agent-standalone/deploy-docker_stack.sh
docker exec -ti <DAOS CLIENT DOCKER CONTAINER ID> bash
$ daos pool autotest <POOL ID>
```

!!! note
    As Docker Secret is a Docker Swarm service, it could not be used properly with Docker Compose.
    With Docker Compose, secret are managed as standard Docker Volume mounted in `/run/secrets`
    directory.  More details could be found at:
    https://github.com/docker/compose/issues/9139#issuecomment-1098137264

!!! warning the DAOS Agent and Client docker swam service shall be deployed on the same host: The
    DAOS agent service is monitoring the `pid` of its associated DAOS clients.
