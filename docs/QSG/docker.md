# QSG - DAOS in Docker

This section describes how to build and deploy Docker images to simulate a small cluster using DAOS as backend storage.  This small cluster is composed of the following three nodes:

- The `daos-server` node running a DAOS server daemon managing data storage devices such as SCM or
  NVMe disks.
- The `daos-admin` node allowing to manage the DAOS server thanks to `dmg`command.
- The `daos-client` node using the the DAOS server to store data.

At this time only emulated hardware storage are supported by this Docker platform:

- SCM (i.e. Storage Class Memory) are emulated with standard RAM memory.
- NVMe disks are emulated with a file device.

!!!warning
Virtual Docker networking is still in development. The following are known issues:
- [bridge](https://docs.docker.com/network/bridge/) 
- loopback
	- Work around - use one physical network interface for the [host](https://docs.docker.com/network/host/) use by the Docker containers  network.


## Hardware and Software Prerequisites
### Hardware Prerequisites
- 5GB of DDR and 
- 16GB of storage.
### Software Prerequisites
- To build and deploy the Docker images, `docker` 
	- optionally `docker-compose`
- Internet access for the following repositories
	- [Docker Hub](https://hub.docker.com/) 
	- [Rocky Linux](https://rockylinux.org/) official repositories.  
	- [hugepages](https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt) enabled
		-linux kernel feature shall be enabled on the docker host. At least, 4096 pages of 2048kB should be available. 
### Hardware and software configuration used in this QSG
- Server Board: Wolf Pass
- CPU: Cascade Lake (2)
- OS: Ubuntu 20.0.4LTE. freshly Installed
- Memory: 192GB 
- PMEM:(12)x16GB

## Building Docker Images
The three images `daos-server`, `daos-admin` and `daos-client` could be built directly from GitHub
or from a local tree in the same way as for the `daos-base` image.
### DAOS Nodes Images
Following command could be used to build directly the three images from GitHub:

```bash
$ for image in daos-server daos-admin daos-client ; do \
	docker build --tag "$image:rocky8.4" \
		"https://github.com/daos-stack/daos.git#master:utils/docker/vcluster/$image/el8"; \
  done
```
@@@Need to add in the ethernet flag above sp specify Ethernet adapter `DAOS_IFACE_NAME`
Note:- `DAOS_IFACE_NAME`: Fabric network interface used by the DAOS engine (default "eth0")

