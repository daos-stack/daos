**Please note: This is a sample article I am preparing for software.Intel.com, this should only be viewed as a Draft.**

***

# Using Distributed Asynchronous Object Storage in a Docker Container

This artical shows how to get started using Distributed Asynchronous Object Storage (DAOS) containers, by taking you through the steps to build, configure and run the DAOS service in a Docker container. 

All commands shown here are on two Socket Server running Ubuntu 20.0.4LTE. To perform the steps below, you will need a minimum of 5GB of DRAM and 16GB of disk space. On Mac, please make sure that the Docker settings under "Preferences/{Disk, Memory}" are configured accordingly.

## What is DAOS
The Distributed Asynchronous Object Storage (DAOS) is an open-source object store that leverages Non Volatile Memory (NVM), such as Storage Class Memory (SCM) and NVM express (NVMe). The storage process uses a key-value storage interface on top of NVM hardware.

## Building a Docker Image

To build the Docker image we can do it one of two ways:
- Local clone of the [GitHub DAOS repo](https://github.com/daos-stack/daos.git)
- Directly from GitHub

If you prefer a different base than CentOS8, replace the filename "Dockerfile.centos.8" in the command strings below with one of the following"
- 'Dockerfile.centos.7'
- 'Dockerfile.ubuntu.20.04'

### 1. Build From Local Clone

```bash
git clone https://github.com/daos-stack/daos.git 
git submodule init; git submodule update
cd daos
docker build  . -f utils/docker/Dockerfile.centos.8 -t daos
```

### 2. Build From Remote Github Repo
This creates a CentOS 8 image and fetches the latest DAOS version from [GitHub/daos-stack](https://github.com/daos-stack/daos/tree/master/utils/docker), builds it, and installs it in the image.

`docker build https://github.com/daos-stack/daos.git#release/1.2 -f utils/docker/Dockerfile.centos.8 -t daos`


## Docker Setup
Once the image has been created, a container will need to be started to run the DAOS service:

`docker run -it -d --privileged --cap-add=ALL --name server -v /dev:/dev daos`

Optional: - If you want to be more selective with the devices that are exported to the container, individual devices should be listed and exported as volume via the -v option. In this case, the hugepages devices should also be added to the command line.

**Note to self need more research here**

`/dev/hugepages:/dev/hugepages and -v /dev/hugepages-1G:/dev/hugepages-1G`

> Warning: If Docker is being run on a non-Linux system (e.g., OSX), -v /dev:/dev should be removed from the command line.

The daos_server_local.yml configuration file sets up a simple local DAOS system with a single server instance running in the container. By default, it uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under /tmp. The storage size can be changed in the yaml file if necessary.

## Start the DAOS Servoce
The DAOS service can be started in the docker container as follows:

`docker exec server daos_server start -o /home/daos/daos/utils/config/examples/daos_server_local.yml`

> Note: Please make sure that the uio_pci_generic module is loaded on the host.

## Format the DAOS storage
Once started, the DAOS server waits for the administrator to format the system. This can be triggered in a different shell, using the following command:

`docker exec server dmg -i storage format`

## Next Steps:
If all the above steps are done, we now have a complete Docker instance established, we now need to look at pool creation using the DAOS Admin Tool


