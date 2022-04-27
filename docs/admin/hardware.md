# Hardware Requirements


The purpose of this section is to describe processor, storage, and
network requirements to deploy a DAOS system.

## Deployment Options


A DAOS storage system is deployed as a **Pooled Storage Model**.
The DAOS servers can run on dedicated storage nodes in separate racks.
This is a traditional pool model where storage is uniformly accessed by
all compute nodes. In order to minimize the number of I/O racks and to
optimize floor space, this approach usually requires high-density storage
servers.


## Processor Requirements


DAOS requires a 64-bit processor architecture and is primarily developed
on Intel x86\_64 architecture. The DAOS software and the libraries it
depends on (e.g., [ISA-L](https://github.com/intel/isa-l),
[SPDK](https://pmem.io/pmdk/), [PMDK](https://spdk.io/), and
[DPDK](https://www.dpdk.org/) can take
advantage of Intel Intel Streaming SIMD (SSE) and Intel Advanced Vector (AVX) extensions.

Some success was also reported by the community on running the DAOS client
on 64-bit ARM processors configured in Little Endian mode. That being said,
ARM testing is not part of the current DAOS CI pipeline and is thus not
validated on a regular basis.

## Network Requirements

The DAOS data plane relies on [OFI libfabrics](https://ofiwg.github.io/libfabric/)
and supports OFI
providers for Ethernet/tcp and InfiniBand/verbs. An RDMA-capable
fabric is preferred for better performance.

DAOS supports multiple network interfaces on the servers
by binding different instances of the DAOS engine to individual
network cards.
DAOS can support multiple network interfaces on the clients,
by assigning different client processes on the node to different
network interfaces. Note that DAOS does *not* support network-level
striping over multiple network interfaces, so a *single* client process
will always use a single network link.

The DAOS control plane provides methods for administering and managing
the DAOS servers using a secure socket layer interface. Management
traffic between clients and servers uses IP over Fabric. On large
clusters, however, administration of DAOS servers typically uses an
additional out-of-band network connecting the nodes in the DAOS service
cluster.

## Storage Requirements


DAOS requires each storage node to have direct access to storage-class
memory (SCM). While DAOS is primarily tested and tuned for Intel
Optane^TM^ Persistent Memory, the DAOS software stack is built over the
Persistent Memory Development Kit (PMDK) and the Direct Access (DAX) feature of the
Linux operating systems as described in the
[SNIA NVM Programming Model](https://www.snia.org/sites/default/files/technical\_work/final/NVMProgrammingModel\_v1.2.pdf).
As a result, the open-source DAOS software stack should be
able to run transparently over any storage-class memory supported by the
PMDK.

The storage node can optionally be equipped with [NVMe](https://nvmexpress.org/)
(non-volatile memory express)[^10] SSDs to provide capacity. HDDs,
as well as SATA andSAS SSDs, are not supported by DAOS.
Both NVMe 3D-NAND and Optane SSDs are supported. Optane SSDs are
preferred for DAOS installation that targets a very high IOPS rate.
NVMe-oF devices are also supported by the
userspace storage stack but have never been tested.

A minimum 6% ratio of SCM to SSD capacity will guarantee that DAOS has
enough space in SCM to store its internal metadata (e.g., pool metadata,
SSD block allocation tracking). Lower ratios are possible, but the
amount of SCM required will depend on the usage patterns of the
applications accessing the DAOS storage. Since DAOS uses the SCM for its
metadata, if the ratio is too low, it is possible to have bulk storage
available but insufficient SCM for DAOS metadata.

For testing purposes, SCM can be emulated with DRAM by mounting a tmpfs
filesystem, and NVMe SSDs can be also emulated with DRAM or a loopback
file.

## Storage Server Design


The hardware design of a DAOS storage server balances the network
bandwidth of the fabric with the aggregate storage bandwidth of the NVMe
storage devices. This relationship sets the number of NVMe drives
depending on the read/write balance of the application workload. Since
NVMe SSDs have read faster than they write, a 200Gbps PCIe4 x4 NIC can
be balanced for read only workloads by 4 NVMe4 x4 SSDs, but for write
workloads by 8 NVMe4 x4 SSDs. The capacity of the SSDs will determine
the minimum capacity of the Optane PMem DIMMs needed to provide the 6%
ratio for DAOS metadata.

![](media/image2.png)

## CPU Affinity


Recent Intel Xeon data center platforms use two processor CPUs connected
together with the Ultra Path Interconnect (UPI). PCIe lanes in these
servers have a natural affinity to one CPU. Although globally accessible
from any of the system cores, NVMe SSDs and network interface cards
connected through the PCIe bus may provide different performance
characteristics (e.g., higher latency, lower bandwidth) to each CPU.
Accessing non-local PCIe devices may involve traffic over the UPI link
that might become a point of congestion. Similarly, persistent memory is
non-uniformly accessible (NUMA), and CPU affinity must be respected for
maximal performance.

Therefore, when running in a multi-socket and multi-rail environment,
the DAOS service must be able to detect the CPU to PCIe device and
persistent memory affinity and minimize, as much as possible, non-local
access. This can be achieved by spawning one instance of the I/O Engine
per CPU, then accessing only the persistent memory and PCI devices local
to that CPU from that server instance. The DAOS control plane is
responsible for detecting the storage and network affinity and starting
the I/O Engines accordingly.

![](media/image3.png)

## Fault Domains


DAOS relies on single-ported storage massively distributed across
different storage nodes. Each storage node is thus a single point of
failure. DAOS achieves fault tolerance by providing data redundancy
across storage nodes in different fault domains.

DAOS assumes that fault domains are hierarchical and do not overlap. For
instance, the first level of a fault domain could be the racks and the
second one, the storage nodes.

For efficient placement and optimal data resilience, more fault domains
are better. As a result, it is preferable to distribute storage nodes
across as many racks as possible.

