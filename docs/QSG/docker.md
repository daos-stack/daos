# DAOS in Docker  QSG

This section describes building and deploying Docker images that simulate a small cluster
using DAOS as backend storage. This small cluster is composed of the following three nodes:

- `daos-server` - node running a DAOS server daemon that manages data storage devices such as SCM or
  NVMe disks.
- `daos-admin` - node allows managing the DAOS server thanks to the `dmg` command.
- `daos-client` - node uses the DAOS server to store data.

At this time, only emulated hardware storage is supported by this Docker platform:

- SCM (i.e., Storage Class Memory) is emulated using standard RAM memory.
- NVMe disks are emulated with a file device.

!!! warning
  DAOS does not yet fully support virtual Docker networks such as: [bridge](https://docs.docker.com/network/bridge/) and the loopback interface. Docker Virtual Network Interface Workaround - Use one physical network interface of the host for use by the containers through the Docker [host](https://docs.docker.com/network/host/) network.

## Prerequisites

To build and deploy the Docker images, the following is required on the host:

- Apps
  - `docker`
  - `docker-compose` optionally
- Web access
  - [Docker Hub](https://hub.docker.com/)
  - [Rocky Linux](https://rockylinux.org/) official repositories.
- BIOS enabled features
  - "Virtualized device access" must be enabled in the system BIOS.
    - On Intel™ systems, this capability is named Intel® Virtualization Technology for Directed I/O (VT-d).
- OS enabled
  - Hugepages enabled (see below)
  - IOMMU support must also be enabled in the Linux kernel once VT-d is enabled in the BIOS
    - Exact details depend on the Host OS distribution, for distributions using GRUB, use the following:

      ```bash
      $ sudo vi /etc/default/grub # add the following line:
      GRUB_CMDLINE_LINUX_DEFAULT="intel_iommu=on"

      $ sudo grub2-mkconfig --output=/boot/grub2/grub.cfg

      $ sudo reboot
      ```

## HugePages

### Enabling HugePages

[HugePages](https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt) Linux kernel feature needs to be enabled on the docker host. At least 4096 pages of 2048kB should be made available. The number of
HugePages allocated is checked with the following command: 

```command
sysctl vm.nr_hugepages
```

The default size of a huge page, the number of available HugePages, etc. is found with the
following command:

```command
cat /proc/meminfo | grep -e "^Huge"
```

!!! warning
    Some distributions are not yet well supported, for example:
    [rockylinux/rockylinux:8.5](https://hub.docker.com/r/rockylinux/rockylinux):
    - issue [DAOS-10046](https://daosio.atlassian.net/browse/DAOS-10046): management of
      HugePages with the [spdk](https://spdk.io/) library.

### Configuring HugePages

First, the Linux kernel needs to be built with the `CONFIG_HUGETLBFS` (present under "File systems")
and `CONFIG_HUGETLB_PAGE` (selected automatically when `CONFIG_HUGETLBFS` is selected) configuration
options.

To avoid memory fragmentation, HugePages could be allocated on the kernel boot command line by
specifying the "hugepages=N" parameter, where 'N' = the number of HugePages requested. It is also
possible to allocate them at run time, thanks to the `sysctl` command: 

```command
sysctl vm.nr_hugepages=8192
```

It is also possible to use the `sysctl` command to allocate HugePages at boot time with the
following command:

```command
cat <<< "vm.nr_hugepages = 8192" > /etc/sysctl.d/50-hugepages.conf 
sysctl -p
```

## Building Docker Images

### Step 1: Base DAOS Image

The first image to create is the `daos-base` image, used to build the other three daos images. This first image is built directly from GitHub with the following command:

```bash
docker build --tag daos-base:rocky8.4 \
      https://github.com/daos-stack/daos.git#release/2.0:utils/docker/vcluster/daos-base/el8
```

### Step 2: Create the DAOS Nodes Images

The three images `daos-server`, `daos-admin` and `daos-client` are built directly from GitHub

The following command is used to build the three images directly from GitHub:

```bash
for image in daos-server daos-admin daos-client ; do \
      docker build --tag "$image:rocky8.4" \
            "https://github.com/daos-stack/daos.git#release/2.0:utils/docker/vcluster/$image/el8"; \
done
```

The Docker file of the `daos-server` image accept several arguments, but for this QSG, you may need to use:

- `DAOS_IFACE_NAME`: Fabric network interface used by the DAOS engine (default "eth0")

!!! note
    The IP address of the network interface referenced by the `DAOS_IFACE_NAME` argument will be
    required when starting DAOS.

## Running the DAOS Containers

### Step 1: Start the containers

Once the images are created, the containers can be directly started with the following commands:

```bash
export DAOS_IFACE_IP=x.x.x.x
docker run --detach --privileged --name=daos-server --hostname=daos-server \
      --add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
      --add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
      --volume=/dev/hugepages:/dev/hugepages  --tmpfs=/run --network=host \
      daos-server:rocky8.4
docker run --detach --privileged --name=daos-agent --hostname=daos-agent \
      --add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
      --add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
      --tmpfs=/run --network=host daos-agent:rocky8.4
docker run --detach --privileged --name=daos-client --hostname=daos-client \
      --add-host "daos-server:$DAOS_IFACE_IP" --add-host "daos-admin:$DAOS_IFACE_IP" \
      --add-host "daos-client:$DAOS_IFACE_IP" --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
      --tmpfs=/run --network=host daos-client:rocky8.4
```

The value of the `DAOS_IFACE_IP` shall be replaced with one of the network interfaces which was
provided when the images were built.

### Step 2: Format the system

Once started, the DAOS server waits for the administrator to format the system.
This can be done using the following command:

`docker exec daos-admin dmg -i storage format`

### Step 3: Creating pools

The storage engine is started upon successful format completion, and pools
can be created using the daos admin tool. Refer to the
[DAOS Tour](https://docs.daos.io/v2.0/QSG/tour/) for more advanced details.
