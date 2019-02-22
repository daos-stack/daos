# DAOS in Docker

Docker is the fastest way to build, install and run DAOS on a non-Linux system.

## Building DAOS in a Container

To build the Docker image directly from GitHub, run the following command:

```
    $ docker build -t daos -f Dockerfile.centos\:7 github.com/daos-stack/daos#:utils/docker
```

This creates a CentOS7 image, fetches the latest DAOS version from GitHub and  builds it in the container.
For Ubuntu, replace Dockerfile.centos\:7 with Dockerfile.ubuntu\:18.04.

To build from a local tree stored on the host, a volume must be created to share the source tree with the Docker container. To do so, execute the following command to create a docker image without checking out the DAOS source tree:

```
    $ docker build -t daos -f utils/docker/Dockerfile.centos\:7 --build-arg NOBUILD=1 .
```

Then execute the following command to export the DAOS source tree to the docker container and build it:

```
    $ docker run -v ${daospath}:/home/daos/daos:Z daos /bin/bash -c "scons --build-deps=yes USE_INSTALLED=all install"
```

## Running DAOS in a Container

Start by creating a container that will run the DAOS service:

```
    $ docker run -it -d --name server --tmpfs /mnt/daos:rw,uid=1000,size=1G -v /tmp/uri:/tmp/uri daos
```

Add "-v \${daospath}:/home/daos/daos:Z" to this command line if the DAOS source tree is stored on the host.

This allocates 1GB of DRAM for DAOS storage. The more, the better.

To start the DAOS service in this newly created container, execute the following command:

```
    $ docker exec server orterun -H localhost -np 1 --report-uri /tmp/uri/uri.txt daos_server
```

Once the DAOS server is started, the integration tests can be run as follows:

```
    $ docker run -v /tmp/uri:/tmp/uri daos \
      orterun -H localhost -np 1 --ompi-server file:/tmp/uri/uri.txt daos_test
```

Again, "-v \${daospath}:/home/daos/daos:Z" must be added if the DAOS source tree is shared with the host.
