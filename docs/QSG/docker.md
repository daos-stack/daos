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
- Hugepages must be enabled
	- At least, 4096 pages of 2048kB should be available 
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

## Configuring Hugepages
Additional information in Hugepages is located in the administration portion of this guid <need link> and at (kernel.org)[https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt]
	
### Determining Huge pages
The number of huge pages allocated could be checked with the following command: 
- Ubuntu- ```$ sysctl vm.nr_hugepages```

The default size of a huge page, the number of available huge pages, etc. could be found with the following command: 
- Ubuntu- ```$ cat /proc/meminfo | grep -e "^Huge" ```

@@@ Not even sure what this means-The platform was tested and validated with the [rockylinux/rockylinux:8.4](https://hub.docker.com/r/rockylinux/rockylinux) and [centos:centos8](https://hub.docker.com/_/centos) official docker images.  However other RHEL-like distributions should be supported.

@@@ Is this a warning about rocky and huge pages or daos with hugepages on rocky or??? 
!!! warning
    Some distributions are not yet well supported such as [rockylinux/rockylinux:8.5](https://hub.docker.com/r/rockylinux/rockylinux): issue with the management of hugepages with the [spdk](https://spdk.io/) library.

### Configuring HugePages
If hugepages have not been enabled, we will need to do so now to avoid memory fragmentation. To configure 2Mb hugepages use the following command:
- ubuntu ```$ sysctl vm.nr_hugepages=4096```

## Building Docker Images
The three images `daos-server`, `daos-admin` and `daos-client` could be built directly from GitHub
or from a local tree in the same way as for the `daos-base` image.

Following command could be used to build directly the three images from GitHub:

@@@ as far as can determine the tag doesnt exist	
```bash
$ for image in daos-server daos-admin daos-client ; do \
	docker build --tag "$image:rocky8.4" \
		"https://github.com/daos-stack/daos.git#master:utils/docker/vcluster/$image/el8"; \
  done
```
@@@Need to add in the ethernet flag above to specify Ethernet adapter `DAOS_IFACE_NAME`
Note:- `DAOS_IFACE_NAME`: Fabric network interface used by the DAOS engine (default "eth0")

For aditional docker file arguments see <link>
	
## Running the DAOS Containers

### Via Docker Commands  @@section below needs simplification to one path, but as I cant getthis to run yet, I need guidance or a working docker image tag

Once the images are created, the containers could be directly started with docker with the following
commands:

```bash
$ export DAOS_IFACE_IP=x.x.x.x
$ docker run --detach --privileged --name=daos-server --hostname=daos-server \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--volume=/dev/hugepages:/dev/hugepages  --tmpfs=/run --network=host \
	daos-server:rocky8.4
$ docker run --detach --privileged --name=daos-agent --hostname=daos-agent \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--tmpfs=/run --network=host daos-agent:rocky8.4
$ docker run --detach --privileged --name=daos-client --hostname=daos-client \
	--add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
	--add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
	--tmpfs=/run --network=host daos-client:rocky8.4
```

The value of the `DAOS_IFACE_IP` shall be replaced with the one of the network interface which was
provided when the images have been built.

Once started, the DAOS server waits for the administrator to format the system.
This can be done using the following command:

```bash
$ docker exec daos-admin dmg -i storage format
```

Upon successful completion of the format, the storage engine is started, and pools
can be created using the daos admin tool.  For more advanced configurations and usage refer to the
section [DAOS Tour](https://docs.daos.io/QSG/tour/).


### Via docker-compose

From a local tree, a more straightforward way to start the containers could be done with
`docker-compose` and the following commands:

```bash
$ docker-compose --file utils/docker/vcluster/docker-compose.yml -- up --detach
```

!!! note
    Before starting the containers with `docker-compose`, the IP address of the network interface,
    which was provided when the images have been built, shall be defined in the Docker
    Compose environment file `utils/docker/vcluster/.env`.

As with the docker command, the system shall be formatted, pools created, etc..
