# QSG - DAOS in Docker

This section describes how to build and deploy Docker images to simulate a small cluster using DAOS as backend storage.  This small cluster is composed of the following three nodes:

- The `daos-server` node running a DAOS server daemon managing data storage devices such as SCM or
  NVMe disks.
- The `daos-admin` node is used to manage the DAOS server thanks to the `dmg`command.
- The `daos-client` node uses the DAOS server to store data.

At this time only emulated hardware storage is supported by this Docker platform:

- SCM (i.e. Storage Class Memory) is emulated with standard RAM memory.
- NVMe disks are emulated with a file device.

!!!Warning
DAOS support of virtual networking is still in development. The following are known issues:
- [bridge](https://docs.docker.com/network/bridge/) 
- loopback
	- Workaround - use one physical network interface for the [host](https://docs.docker.com/network/host/) used by the Docker containers network.

## Hardware and Software Prerequisites

### Hardware Prerequisites

- CPU: Cascade Lake (2) or newer
- OS: Ubuntu 20.0.4LTE. fresh Install
- 5GB of DDR  
- 16GB of storage.
- BIIOS: VT-d enable

### Software Prerequisites

- BIOS
	- Enable VT-d 
- To build and deploy the Docker images, `docker` 
	- optionally `docker-compose`
- Hugepages must be enabled
	- At least, 4096 pages of 2048kB should be available 
- Internet access for the following repositories
	- [Docker Hub](https://hub.docker.com/) 
	- [Rocky Linux](https://rockylinux.org/) official repositories.  
	- [hugepages](https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt) enabled
		-Linux kernel feature shall be enabled on the docker host. At least, 4096 pages of 2048kB should be available. 

## Hugepages

Additional information in Hugepages is located in the administration portion of this guide <need link> and at (kernel.org)[https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt]
	
The default size of a huge page, the number of available huge pages, etc. could be found with the following command: 
- Ubuntu- ```$ cat /proc/meminfo | grep -e "^Huge" ```
If the pages are not enabled, you will see something like this:

```
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:               0 kB
```

### Note: 

Huge pages are supported by all containerized Linux distributions. However support of huge pages 
with DAOS and its dependencies are not working for some Linux distribution. Only the following distributions have
been validated:
- [rockylinux/rockylinux:8.4](https://hub.docker.com/r/rockylinux/rockylinux) 
- [centos:centos8](https://hub.docker.com/_/centos)
The following Linux docker distributions are known not to support hugepages at this time:
- [rockylinux/rockylinux:8.5](https://hub.docker.com/r/rockylinux/rockylinux)

### Configuring HugePages
If hugepages have not been enabled, we will need to do so now to avoid memory fragmentation. To configure
2Mb hugepages use the following command:
```bash
sysctl vm.nr_hugepages=4096
reboot
```

## Building Docker Images
The three images `daos-server`, `daos-admin` and `daos-client` could be built directly from GitHub
or from a local tree in the same way as for the `daos-base` image.

First you will need to install docker and docker compose, for example on Debian like distribution

```
sudo apt install docker
sudo apt install docker-compose
....

Following command could be used to build directly the three images from GitHub:

```bash
docker build --tag "$image:rocky8.4" \ "https://github.com/daos-stack/daos.git#master:utils/docker/vcluster/$image/el8"
```
For additional docker, file arguments see <link>
	
## Running the DAOS Containers
From a local tree, a simple way to start the containers is done with a `docker-compose` commands:

```bash
$ docker-compose --file utils/docker/vcluster/docker-compose.yml -- up --detach
```

!!! note
    Before starting the containers with `docker-compose`, the IP address of the network interface,
    which was provided when the images have been built, shall be defined in the Docker
    Compose environment file `utils/docker/vcluster/.env`.

As with the docker command, the system shall be formatted, pools created, etc.
