**Please note: This is a sample article I am preparing for software.Intel.com, this should only be viewed as a Draft.**
**Original version is located here: https://docs.daos.io/admin/installation/#daos-in-docker"
***

# Using Distributed Asynchronous Object Storage in a Docker Container

This article shows how to get started using Distributed Asynchronous Object Storage (DAOS) containers, by taking you through the steps to build, configure and run the DAOS service in a Docker container. 

All commands shown here are on two socket Cascade Lake server running Ubuntu 20.0.4LTE. To perform the steps below, you will need a minimum of 5GB of DRAM and 16GB of disk space. On Mac, please make sure that the Docker settings under "Preferences/{Disk, Memory}" are configured accordingly.

## What is DAOS
The Distributed Asynchronous Object Storage (DAOS) is an open-source object store that leverages Non Volatile Memory (NVM), such as Storage Class Memory (SCM) and NVM express (NVMe). The storage process uses a key-value storage interface on top of NVM hardware.

## Building a Docker Image

To build the Docker image we can do it one of two ways:
- Local clone of the [GitHub DAOS repo](https://github.com/daos-stack/daos.git)
- Directly from GitHub

If you prefer a different base than CentOS7, replace the filename "Dockerfile.centos.7" in the command strings below with one of the following"
- 'Dockerfile.centos.8'
- 'Dockerfile.ubuntu.20.04'

### 1. Build From Local Clone

```bash
git clone https://github.com/daos-stack/daos.git 
git submodule init; git submodule update
cd daos
docker build  . -f utils/docker/Dockerfile.centos.7 -t daos
```

### 2. Build From Remote Github Repo
This creates a CentOS 7 image and fetches the latest DAOS version from [GitHub/daos-stack](https://github.com/daos-stack/daos/tree/master/utils/docker), builds it, and installs it in the image.

`docker build https://github.com/daos-stack/daos.git#release/1.2 -f utils/docker/Dockerfile.centos.7 -t daos`


## Docker Setup
Once the image has been created, a container will need to be started to run the DAOS service. 

### Setting Hugepages
At this stage depending on how hugepages is configured you may or may not get errors, so for consistency we will configure hugepages before running the docker image:

```bash
echo 1024 | sudo tee /proc/sys/vm/nr_hugepages
cat /proc/meminfo | grep Huge
```

This should provide an output similar to:

```bash
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
FileHugePages:         0 kB
HugePages_Total:    1024
HugePages_Free:     1024
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:         2097152 kB
```
### Starting the Docker Container
Now we need to start the docker container, by invoking 'docker run'

`sudo docker run -it -d --privileged --cap-add=ALL --name server -v /dev/hugepages:/dev/hugepages daos`

Alternatively you can use 1G hugepages or no Hugepages as well

`sudo docker run -it -d --privileged --cap-add=ALL --name server -v /dev/hugepages-1G:/dev/hugepages-1G'

`sudo docker run -it -d --privileged --cap-add=ALL --name server -v /dev:/dev`

> Warning: If Docker is being run on a non Linux system, the "-v" parameter should be removed from the command line.
** `docker run -it -d --privileged --cap-add=ALL --name server'

## Start the DAOS Servoce
The daos_server_local.yml configuration file sets up a simple local DAOS system with a single server instance running in the container. By default, it uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under /tmp. The storage size can be changed in the yaml file if necessary.

The DAOS service can be started in the docker container as follows:

`docker exec server daos_server start -o /home/daos/daos/utils/config/examples/daos_server_local.yml`

> Note: Please make sure that the uio_pci_generic module is loaded on the host. **Need to research**

## Format the DAOS storage
Once started, the DAOS server waits for the administrator to format the system. This can be triggered in a different shell, using the following command:

`sudo docker exec server dmg -i storage format`
This should provide an output similar to:

```bash
Format Summary:
  Hosts     SCM Devices NVMe Devices
  -----     ----------- ------------
  localhost 1           1
```

## Next Steps:
If all the above steps are done, we now have a complete Docker instance established, we now need to look at pool creation using the DAOS Admin Tool


