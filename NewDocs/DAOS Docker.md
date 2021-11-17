**Please note: This is a sample article I am preparing for software.Intel.com. This should only be viewed as a Draft.**
**Source material for this can be found at https://docs.daos.io/admin/installation/#daos-in-docker"
***

# Using Distributed Asynchronous Object Storage in a Docker Container

This article shows how to get started using Distributed Asynchronous Object Storage (DAOS) containers by taking you through the steps to build, configure and run the DAOS service in a Docker container. 

On Mac, please make sure that the Docker settings under "Preferences/{Disk, Memory}" are configured accordingly.

## What is DAOS
The Distributed Asynchronous Object Storage (DAOS) is an open-source object store that leverages Non-Volatile Memory (NVM), such as Storage Class Memory (SCM) and NVM Express (NVMe). The storage process uses a key-value storage interface on top of NVM hardware. For additional information, see the following articles articles:
- [DAOS Sets New Records with Intel® Optane™ Persistent Memory](https://www.intel.com/content/www/us/en/developer/articles/technical/daos-sets-new-records-with-intel-optane-persistent-memory.html)
- [DAOS: Revolutionizing High-Performance Storage with Intel® Optane™ Technology](https://www.intel.com/content/dam/www/public/us/en/documents/solution-briefs/high-performance-storage-brief.pdf)

## Building a Docker Image

To build the Docker image, we can do it one of two ways:
- Local clone of the GitHub DAOS repo
- Directly from GitHub

> I should reconsider this path and build off CentOS8 as its closer to Rocky8.4

If you prefer a different base than CentOS7, replace the filename "Dockerfile.centos.7" in the command strings below with one of the following."
- Dockerfile.centos.8
- Dockerfile.ubuntu.20.04

> All commands shown here are on a two-socket Cascade Lake server running a new install of Ubuntu 20.0.4LTE. To perform the steps below, you will need a minimum of 5GB of DDR and 16GB of storage. 

### 1. Build From Local Clone

```bash
git clone https://github.com/daos-stack/daos.git 
git submodule init; git submodule update
cd daos
docker build  . -f utils/docker/Dockerfile.centos.7 -t daos
```

### 2. Build From Remote Github Repo
In this step, we create a CentOS 7 image and fetches the latest DAOS version from [GitHub/daos-stack](https://github.com/daos-stack/daos/tree/master/utils/docker), builds it, and install it in the image.

`docker build https://github.com/daos-stack/daos.git#release/1.2 -f utils/docker/Dockerfile.centos.7 -t daos`

## Docker Setup
Once the image has been created, a container will need to be started to run the DAOS service. 

### Setting Hugepages (Work in Progress)
At this stage, depending on how hugepages are configured on your host system, you may get errors when the `docker run` command is issued. So for this demonstration, we will be configuring hugepages before issuing the `docker run` command:

In this situation of using a Cascade Lake CPU, the 2mb hugepage capacity at 1024 pages, so we set the hugepages by using the following commands:

```bash
echo 1024 | sudo tee /proc/sys/VM/nr_hugepages
cat /proc/meminfo | grep Huge
```

This command should provide an output similar to:

```bash
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
FileHugePages:         0 kB
HugePages_Total:    1024
HugePages_Free:     1023
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:         2097152 kB

```

For more help on hugepages see the [Ubuntu Documentation page](https://help.ubuntu.com/community/KVM%20-%20Using%20Hugepages) and the white paper [Intel Architecture Optimization with Large Code Pages](https://www.intel.com/content/dam/develop/external/us/en/documents/runtimeperformanceoptimizationblueprint-largecodepages-q1update.pdf)

### Notes on 1g Huge pages (inclusion TBD)
Depending on the CPU involved, the quantity of available1G  hugepages changes, for example.
- Cascade Lake 16 pages
- Icelake 64 pages
- Saphire Rapids 96 pages

https://docs.01.org/clearlinux/latest/guides/maintenance/configure-hugepages.html


## Starting the Docker Container
Now we need to start the docker container by invoking the "docker run" command

`sudo docker run -it -d --privileged --cap-add=ALL --name server -v /dev/hugepages-1G:/dev/hugepages-1G`

Alternatively, you can use standard hugepages or no hugepages as well. In those cases, the docker container would need to be started as follows:

`sudo docker run -it -d --privileged --cap-add=ALL --name server -v /dev/hugepages:/dev/hugepages daos`

or

`sudo docker run -it -d --privileged --cap-add=ALL --name server -v /dev:/dev`

> Warning: If Docker is being run on a non-Linux system, the "-v" parameter should be removed from the command line. Example:
`sudo docker run -it -d --privileged --cap-add=ALL --name server`

## Start the DAOS Service
Now that the DAOS Docker image is running, we need to enable the DAOS Service 

The DAOS service can be started in the docker container as follows:

`sudo docker exec server sudo /opt/daos/bin/daos_server start -o /home/daos/daos/utils/config/examples/daos_server_local.yml`

> The daos_server_local.yml configuration file sets up a simple local DAOS system with a single server instance running in the container. By default, it uses 4GB of DRAM to emulate persistent memory and 16GB of bulk storage under /tmp. The storage size can be changed in the yaml file if necessary.

> Note: Please make sure that the uio_pci_generic module is loaded on the host. **Need to research**

## Format the DAOS storage
Once started, the DAOS server waits for the administrator to format the system. The formatting needs to be triggered in a different shell, using the following command:

`sudo docker exec server dmg -i storage format`

This command should provide an output similar to:

```bash
Format Summary:
  Hosts     SCM Devices NVMe Devices
  -----     ----------- ------------
  localhost 1           1
```

## Creating DAOS Pools (Work in Progress)

`sudo docker exec server dmg pool create --size 15GB`

## Saving the Docker changes (optional)
Now that we have started the service and formatted the storage, you may want to save the changes at this point. You do that by running the following commands:

`sudo docker ps -a`

Locate the container ID in the output and copy it, then run the following command:

`sudo docker commit [CONTAINER_ID] [new_image_name]`

You will now see in your list of images, including your new_image_name

`sudo docker images`

## Next Steps (work in Progress)


## Resources
- [DAOS Sets New Records with Intel® Optane™ Persistent Memory](https://www.intel.com/content/www/us/en/developer/articles/technical/daos-sets-new-records-with-intel-optane-persistent-memory.html)
- [DAOS Github Repo](https://github.com/daos-stack/daos)
- [daos.io](https://docs.daos.io/)
- [Intel Architecture Optimization with Large Code Pages](https://www.intel.com/content/dam/develop/external/us/en/documents/runtimeperformanceoptimizationblueprint-largecodepages-q1update.pdf)
- [Designing DAOS Storage Solutions with Lenovo ThinkSystem SR630 Servers](https://lenovopress.com/lp1398-designing-daos-storage-solutions-with-sr630)
