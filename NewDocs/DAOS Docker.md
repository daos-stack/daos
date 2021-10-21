# Using DAOS in a Docker Container

This section describes how to build and run the DAOS service in a Docker container. A minimum of 5GB of DRAM and 16GB of disk space will be required. On Mac, please make sure that the Docker settings under "Preferences/{Disk, Memory}" are configured accordingly.

## Building a Docker Image

To build the Docker image we can do it one of two ways, from a local clone of the gitrepo or directly from GitHub

If you prefer a different base than CentOS7, replace the filename "Dockerfile.centos.7" in the command strings below with one of the following"
- 'Dockerfile.centos.8'
- 'Dockerfile.ubuntu.20.04'

### 1. Build from local clone

```bash
git clone https://github.com/daos-stack/daos.git 
git submodule init; git submodule update
docker build  . -f utils/docker/Dockerfile.centos.7 -t daos
```

### 2. Build from remote github repo
This creates a CentOS 7 image, fetches the latest DAOS version from GitHub, builds it, and installs it in the image. [here](https://github.com/daos-stack/daos/tree/master/utils/docker)

`docker build https://github.com/daos-stack/daos.git#release/1.2 -f utils/docker/Dockerfile.centos.7 -t daos`


## Docker Setup
Once the image has been created, a container will need to be started to run the DAOS service:

`docker run -it -d --privileged --cap-add=ALL --name server -v /dev:/dev daos`

> Note: If you want to be more selective with the devices that are exported to the container, individual devices should be listed and exported as volume via the -v option. In this case, the hugepages devices should also be added to the command line via -v /dev/hugepages:/dev/hugepages and -v /dev/hugepages-1G:/dev/hugepages-1G

> Warning: If Docker is being run on a non-Linux system (e.g., OSX), -v /dev:/dev should be removed from the command line.

The daos_server_local.yml configuration file sets up a simple local DAOS system with a single server instance running in the container. By default, it uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under /tmp. The storage size can be changed in the yaml file if necessary.

## Start the DAOS Servoce
The DAOS service can be started in the docker container as follows:

`docker exec server daos_server start -o /home/daos/daos/utils/config/examples/daos_server_local.yml`

> Note: Please make sure that the uio_pci_generic module is loaded on the host.

## Format the DAOS storage
Once started, the DAOS server waits for the administrator to format the system. This can be triggered in a different shell, using the following command:

`docker exec server dmg -i storage format`

Upon successful completion of the format, the storage engine is started, and pools can be created using the daos admin tool (see next section).
