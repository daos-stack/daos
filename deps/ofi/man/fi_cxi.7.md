---
layout: page
title: fi_cxi(7)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_cxi \- The CXI Fabric Provider

# OVERVIEW

The CXI provider enables libfabric on Cray's Slingshot network. Slingshot is
comprised of the Rosetta switch and Cassini NIC. Slingshot is an
Ethernet-compliant network. However, The provider takes advantage of proprietary
extensions to support HPC applications.

The CXI provider supports reliable, connection-less endpoint semantics. It
supports two-sided messaging interfaces with message matching offloaded by the
Cassini NIC. It also supports one-sided RMA and AMO interfaces, light-weight
counting events, triggered operations (via the deferred work API), and
fabric-accelerated small reductions.

# REQUIREMENTS

The CXI Provider requires Cassini's optimized HPC protocol which is only
supported in combination with the Rosetta switch.

The provider uses the libCXI library for control operations and a set of
Cassini-specific header files to enable direct hardware access in the data path.

# SUPPORTED FEATURES

The CXI provider supports a subset of OFI features.

## Endpoint types

The provider supports the *FI_EP_RDM* endpoint type.

## Memory registration modes

The provider implements scalable memory registration. The provider requires
*FI_MR_ENDPOINT*. *FI_MR_ALLOCATED* is required if ODP in not enabled or not
desired. Client specified 32-bit MR keys are the default unless *FI_MR_PROV_KEY*
is specified. For *FI_MR_PROV_KEY* provider generated 64-bit MR keys are used.
An RMA initiator can work concurrently with client and provider generated keys.

In client/server environments, if concerns with stale MR key usage exists, then
*FI_MR_PROV_KEY* generated keys should be used along with *FI_CXI_MR_MATCH_EVENTS=1*
and *FI_CXI_OPTIMIZED_MRS=0*. The former speeds up MR close, allowing non-remote
MR cached keys to be used that enable full remote memory access protection
after an MR is closed, even if that memory remains in the libfabric MR cache.
The latter uses only standard MR which use matching to enable robust key
usage, protecting against a stale MR key matching a newly generated MR keys.

## Data transfer operations

The following data transfer interfaces are supported: *FI_ATOMIC*, *FI_MSG*,
*FI_RMA*, *FI_TAGGED*.  See DATA TRANSFER OPERATIONS below for more details.

## Completion events

The CXI provider supports all CQ event formats.

## Modes

The CXI provider does not require any operation modes.

## Progress

The CXI provider currently supports *FI_PROGRESS_MANUAL* data and control
progress modes.

## Multi-threading

The CXI provider supports FI_THREAD_SAFE and FI_THREAD_DOMAIN threading models.

## Wait Objects

The CXI provider supports FI_WAIT_FD and FI_WAIT_POLLFD CQ wait object types.
FI_WAIT_UNSPEC will default to FI_WAIT_FD. However FI_WAIT_NONE should achieve
the lowest latency and reduce interrupt overhead.

## Additional Features

The CXI provider also supports the following capabilities and features:

* *FI_MULTI_RECV*
* *FI_SOURCE*
* *FI_NAMED_RX_CTX*
* *FI_RM_ENABLED*
* *FI_RMA_EVENT*
* *FI_REMOTE_CQ_DATA*
* *FI_MORE*
* *FI_FENCE*

## Addressing Format

The CXI provider uses a proprietary address format. This format includes fields
for NIC Address and PID. NIC Address is the topological address of the NIC
endpoint on the fabric. All OFI Endpoints sharing a Domain share the same NIC
Address. PID (for Port ID or Process ID, adopted from the Portals 4
specification), is analogous to an IP socket port number. Valid PIDs are in the
range [0-510].

A third component of Slingshot network addressing is the Virtual Network ID
(VNI). VNI is a protection key used by the Slingshot network to provide
isolation between applications. A VNI defines an isolated PID space for a given
NIC. Therefore, Endpoints must use the same VNI in order to communicate. Note
that VNI is not a field of the CXI address, but rather is specified as part of
the OFI Endpoint auth_key. The combination of NIC Address, VNI, and PID is
unique to a single OFI Endpoint within a Slingshot fabric.

The NIC Address of an OFI Endpoint is inherited from the Domain. By default, a
PID is automatically assigned to an Endpoint when it is enabled. The address of
an Endpoint can be queried using fi_getname. The address received from
fi_getname may then be inserted into a peer's Address Vector. The resulting FI
address may then be used to perform an RDMA operation.

Alternatively, a client may manage PID assignment. fi_getinfo may be used to
create an fi_info structure that can be used to create an Endpoint with a
client-specified address. To achieve this, use fi_getinfo with the *FI_SOURCE*
flag set and set node and service strings to represent the local NIC interface
and PID to be assigned to the Endpoint. The NIC interface string should match
the name of an available CXI domain (in the format cxi[0-9]). The PID string
will be interpreted as a 9-bit integer. Address conflicts will be detected when
the Endpoint is enabled.

## Authorization Keys

The CXI authorization key format is defined by struct cxi_auth_key. This
structure is defined in fi_cxi_ext.h.

```c
struct cxi_auth_key {
	uint32_t svc_id;
	uint16_t vni;
};
```

The CXI authorization key format includes a VNI and CXI service ID. VNI is a
component of the CXI Endpoint address that provides isolation. A CXI service is
a software container which defines a set of local CXI resources, VNIs, and
Traffic Classes which a libfabric user can access.

Two endpoints must use the same VNI in order to communicate. Generally, a
parallel application should be assigned to a unique VNI on the fabric in order
to achieve network traffic and address isolation. Typically a privileged
entity, like a job launcher, will allocate one or more VNIs for use by the
libfabric user.

The CXI service API is provided by libCXI. It enables a privileged entity, like
an application launcher, to control an unprivileged process's access to NIC
resources. Generally, a parallel application should be assigned to a unique CXI
service in order to control access to local resources, VNIs, and Traffic
Classes.

While a libfabric user provided authorization key is optional, it is highly
encouraged that libfabric users provide an authorization key through the domain
attribute hints during `fi_getinfo()`. How libfabric users acquire the
authorization key may vary between the users and is outside the scope of this
document.

If an authorization key is not provided by the libfabric user, the CXI provider
will attempt to generate an authorization key on behalf of the user. The
following outlines how the CXI provider will attempt to generate an
authorization key.

1. Query for the following environment variables and generate an authorization
key using them.
    * *SLINGSHOT_VNIS*: Comma separated list of VNIs. The CXI provider will only
    use the first VNI if multiple are provide. Example: `SLINGSHOT_VNIS=234`.
    * *SLINGSHOT_DEVICES*: Comma separated list of device names. Each device index
    will use the same index to lookup the service ID in *SLINGSHOT_SVC_IDS*.
    Example: `SLINGSHOT_DEVICES=cxi0,cxi1`.
    * *SLINGSHOT_SVC_IDS*: Comma separated list of pre-configured CXI service IDs.
    Each service ID index will use the same index to lookup the CXI device in
    *SLINGSHOT_DEVICES*. Example: `SLINGSHOT_SVC_IDS=5,6`.

    **Note:** How valid VNIs and device services are configured is outside
    the responsibility of the CXI provider.

2. Query pre-configured device services and find first entry with same UID as
the libfabric user.

3. Query pre-configured device services and find first entry with same GID as
the libfabric user.

4. Query pre-configured device services and find first entry which does not
restrict member access. If enabled, the default service is an example of an
unrestricted service.

    **Note:** There is a security concern with such services since it allows
    for multiple independent libfabric users to use the same service.

**Note:** For above entries 2-4, it is possible the found device service does
not restrict VNI access. For such cases, the CXI provider will query
*FI_CXI_DEFAULT_VNI* to assign a VNI.

During Domain allocation, if the domain auth_key attribute is NULL, the CXI
provider will attempt to generate a valid authorization key. If the domain
auth_key attribute is valid (i.e. not NULL and encoded authorization key has
been verified), the CXI provider will use the encoded VNI and service ID.
Failure to generate a valid authorization key will result in Domain allocation
failure.

During Endpoint allocation, if the endpoint auth_key attribute is NULL, the
Endpoint with inherit the parent Domain's VNI and service ID. If the Endpoint
auth_key attribute is valid, the encoded VNI and service ID must match the
parent Domain's VNI and service ID. Allocating an Endpoint with a different VNI
and service from the parent Domain is not supported.

The following is the expected parallel application launch workflow with
CXI integrated launcher and CXI authorization key aware libfabric user:

1. A parallel application is launched.
2. The launcher allocates one or more VNIs for use by the application.
3. The launcher communicates with compute node daemons where the application
   will be run.
4. The launcher compute node daemon configures local CXI interfaces. libCXI is
   used to allocate one or more services for the application. The service will
   define the local resources, VNIs, and Traffic Classes that the application
   may access. Service allocation policies must be defined by the launcher.
   libCXI returns an ID to represent a service.
5. The launcher forks application processes.
6. The launcher provides one or more service IDs and VNI values to the
   application processes.
7. Application processes select from the list of available service IDs and VNIs
   to form an authorization key to use for Endpoint allocation.

## Endpoint Protocols

The provider supports multiple endpoint protocols. The default protocol is
FI_PROTO_CXI and fully supports the messaging requirements of parallel
applicaitons.

The FI_PROTO_CXI_RNR endpoint protocol is an optional protocol that targets
client/server environments where send-after-send ordering is not required and
messaging is generally to pre-posted buffers; FI_MULTI_RECV is recommended.
It utilizes a receiver-not-ready implementation where
*FI_CXI_RNR_MAX_TIMEOUT_US* can be tuned to control the maximum retry duration.

## Address Vectors

The CXI provider supports both *FI_AV_TABLE* and *FI_AV_MAP* with the same
internal implementation.

The CXI provider uses the *FI_SYMMETRIC* AV flag for optimization. When used
with *FI_AV_TABLE*, the CXI provider can use the fi_addr_t index as an endpoint
identifier instead of a network address. The benefit of this is when running
with FI_SOURCE, a reverse lookup is not needed to generate the source fi_addr_t
for target CQ events. Note: FI_SOURCE_ERR should not be used for this
configuration.

If the AV is not configured with *FI_SYMMETRIC*, *FI_AV_USER_ID* is supported
as a flag which can be passed into AV insert.

Since scalable EPs are not supported, fi_av_attr::rx_ctx_bits must be zero.

The following AV capabilities and flags are not supported: FI_SHARED_AV,
FI_SYNC_ERR, FI_EVENT, and FI_READ.

## Operation flags

The CXI provider supports the following Operation flags:

*FI_MORE*
:   When *FI_MORE* is specified in a data transfer operation, the provider will
    defer submission of RDMA commands to hardware. When one or more data
    transfer operations is performed using *FI_MORE*, followed by an operation
    without *FI_MORE*, the provider will submit the entire batch of queued
    operations to hardware using a single PCIe transaction, improving PCIe
    efficiency.

    When *FI_MORE* is used, queued commands will not be submitted to hardware
    until another data transfer operation is performed without *FI_MORE*.

*FI_TRANSMIT_COMPLETE*
:   By default, all CXI provider completion events satisfy the requirements of
    the 'transmit complete' completion level. Transmit complete events are
    generated when the intiator receives an Ack from the target NIC. The Ack is
    generated once all data has been received by the target NIC. Transmit
    complete events do not guarantee that data is visibile to the target
    process.

*FI_DELIVERY_COMPLETE*
:   When the 'delivery complete' completion level is used, the event guarantees
    that data is visible to the target process. To support this, hardware at
    the target performs a zero-byte read operation to flush data across the
    PCIe bus before generating an Ack. Flushing reads are performed
    unconditionally and will lead to higher latency.

*FI_MATCH_COMPLETE*
:   When the 'match complete' completion level is used, the event guarantees
    that the message has been matched to a client-provided buffer. All messages
    longer than the eager threshold support this guarantee. When 'match
    complete' is used with a Send that is shorter than the eager threshold, an
    additional handshake may be performed by the provider to notify the
    initiator that the Send has been matched.

The CXI provider also supports the following operation flags:

* *FI_INJECT*
* *FI_FENCE*
* *FI_COMPLETION*
* *FI_REMOTE_CQ_DATA*

## Scalable Endpoints

Scalable Endpoints (SEPs) support is not enabled in the CXI provider. Future
releases of the provider will re-introduce SEP support.

## Messaging

The CXI provider supports both tagged (*FI_TAGGED*) and untagged (*FI_MSG*)
two-sided messaging interfaces. In the normal case, message matching is
performed by hardware. In certain low resource conditions, the responsibility to
perform message matching may be transferred to software. Specification
of the receive message matching mode in the environment (*FI_CXI_RX_MATCH_MODE*)
controls the initial matching mode and whether hardware matching can
transparently transition matching to software where a hybrid of hardware
and software receive matching is done.

If a Send operation arrives at a node where there is no matching Receive
operation posted, it is considered unexpected. Unexpected messages are
supported. The provider manages buffers to hold unexpected message data.

Unexpected message handling is transparent to clients. Despite that, clients
should take care to avoid excessive use of unexpected messages by pre-posting
Receive operations. An unexpected message ties up hardware and memory resources
until it is matched with a user buffer.

The CXI provider implements several message protocols internally. A message
protocol is selected based on payload length. Short messages are transferred
using the eager protocol. In the eager protocol, the entire message payload is
sent along with the message header. If an eager message arrives unexpectedly,
the entire message is buffered at the target until it is matched to a Receive
operation.

Long messages are transferred using a rendezvous protocol. The threshold at
which the rendezvous protocol is used is controlled with the
*FI_CXI_RDZV_THRESHOLD* and *FI_CXI_RDZV_GET_MIN* environment variables.

In the rendezvous protocol, a portion of the message payload is sent
along with the message header. Once the header is matched to a Receive
operation, the remainder of the payload is pulled from the source using an RDMA
Get operation. If the message arrives unexpectedly, the eager portion of the
payload is buffered at the target until it is matched to a Receive operation.
In the normal case, the Get is performed by hardware and the operation
completes without software progress.

Unexpected rendezvous protocol messages can not complete and release source side
buffer resources until a matching receive is posted at the destination and the
non-eager data is read from the source with a rendezvous get DMA. The number of
rendezvous messages that may be outstanding is limited by the minimum of the
hints->tx_attr->size value specified and the number of rendezvous operation ID
mappings available. FI_TAGGED rendezvous messages have 32K-256 ID mappings,
FI_MSG rendezvous messages are limited to 256 ID mappings. While this
works well with MPI, care should be taken that this minimum is large enough to
ensure applications written in a manner that assumes unlimited resources and
use FI_MSG rendezvous messaging do not induce a software deadlock. If FI_MSG
rendezvous messaging is done in a unexpected manner that may exceed the FI_MSG
ID mappings available, it may be sufficient to reduce the number of rendezvous
operations by increasing the rendezvous threshold. See *FI_CXI_RDZV_THRESHOLD*
for information.

Message flow-control is triggered when hardware message matching resources
become exhausted. Messages may be dropped and retransmitted in order to
recover; impacting performance significantly. Programs should be careful to avoid
posting large numbers of unmatched receive operations and to minimize the
number of outstanding unexpected messages to prevent message flow-control.
If the RX message matching mode is configured to support hybrid mode, when
resources are exhausted, hardware will transition to hybrid operation where
hardware and software share matching responsibility.

To help avoid this condition, increase Overflow buffer space using environment
variables *FI_CXI_OFLOW_\**, and for software and hybrid RX match modes
increase Request buffer space using the variables *FI_CXI_REQ_\**.

## Message Ordering

The CXI provider supports the following ordering rules:

* All message Send operations are always ordered.
* RMA Writes may be ordered by specifying *FI_ORDER_RMA_WAW*.
* AMOs may be ordered by specifying *FI_ORDER_AMO_{WAW|WAR|RAW|RAR}*.
* RMA Writes may be ordered with respect to AMOs by specifying *FI_ORDER_WAW*.
  Fetching AMOs may be used to perform short reads that are ordered with
  respect to RMA Writes.

Ordered RMA size limits are set as follows:

* *max_order_waw_size* is -1. RMA Writes and non-fetching AMOs of any size are
  ordered with respect to each other.
* *max_order_raw_size* is -1. Fetching AMOs of any size are ordered with
  respect to RMA Writes and non-fetching AMOs.
* *max_order_war_size* is -1. RMA Writes and non-fetching AMOs of any size are
  ordered with respect to fetching AMOs.

## PCIe Ordering

Generally, PCIe writes are strictly ordered. As an optimization, PCIe TLPs may
have the Relaxed Order (RO) bit set to allow writes to be reordered. Cassini
sets the RO bit in PCIe TLPs when possible. Cassini sets PCIe RO as follows:

* Ordering of messaging operations is established using completion events.
  Therefore, all PCIe TLPs related to two-sided message payloads will have RO
  set.
* Every PCIe TLP associated with an unordered RMA or AMO operation will have RO
  cleared.
* PCIe TLPs associated with the last packet of an ordered RMA or AMO operation
  will have RO cleared.
* PCIe TLPs associated with the body packets (all except the last packet of an
  operation) of an ordered RMA operation will have RO set.

## Translation

The CXI provider supports two translation mechanisms: Address Translation
Services (ATS) and NIC Translation Agent (NTA). Use the environment variable
*FI_CXI_ATS* to select between translation mechanisms.

ATS refers to NIC support for PCIe rev. 4 ATS, PRI and PASID features. ATS
enables the NIC to efficiently access the entire virtual address space of a
process. ATS mode currently supports AMD hosts using the iommu_v2 API.

The NTA is an on-NIC translation unit. The NTA supports two-level page tables
and additional hugepage sizes. Most CPUs support 2MB and 1GB hugepage sizes.
Other hugepage sizes may be supported by SW to enable the NIC to cache more
address space.

ATS and NTA both support on-demand paging (ODP) in the event of a page fault.
Use the environment variable *FI_CXI_ODP* to enable ODP.

With ODP enabled, buffers used for data transfers are not required to be backed
by physical memory. An un-populated buffer that is referenced by the NIC will
incur a network page fault. Network page faults will significantly impact
application performance. Clients should take care to pre-populate buffers used
for data-tranfer operations to avoid network page faults. Copy-on-write
semantics work as expected with ODP.

With ODP disabled, all buffers used for data transfers are backed by pinned
physical memory. Using Pinned mode avoids any overhead due to network page
faults but requires all buffers to be backed by physical memory. Copy-on-write
semantics are broken when using pinned memory. See the Fork section for more
information.

The CXI provider supports DMABUF for device memory registration. If the ROCR
and CUDA libraries support it, the CXI provider will default to use DMA-buf.
There may be situations with CUDA that may double the BAR consumption.
Until this is fixed in the CUDA stack, the environment variable
*FI_CXI_DISABLE_DMABUF_CUDA* can be used to fall back to the nvidia
peer-memory interface.
Also, *FI_CXI_DISABLE_DMABUF_ROCR* can be used to fall back to the amdgpu
peer-memory interface.

## Translation Cache

Mapping a buffer for use by the NIC is an expensive operation. To avoid this
penalty for each data transfer operation, the CXI provider maintains an internal
translation cache.

When using the ATS translation mode, the provider does not maintain translations
for individual buffers. It follows that translation caching is not required.

## Triggered Operation

The CXI provider supports triggered operations through the deferred work queue
API. The following deferred work queue operations are supported: FI_OP_SEND,
FI_OP_TSEND, FI_OP_READ, FI_OP_WRITE, FI_OP_ATOMIC, FI_OP_FETCH_ATOMIC, and
FI_OP_COMPARE_ATOMIC. FI_OP_RECV and FI_OP_TRECV are also supported, but with
only a threshold of zero.

The CXI provider backs each triggered operation by hardware resources.
Exhausting triggered operation resources leads to indeterminate behavior and
should be prevented.

The CXI provider offers two methods to prevent triggered operation resource
exhaustion.

### Experimental FI_CXI_ENABLE_TRIG_OP_LIMIT Environment Variable

When FI_CXI_ENABLE_TRIG_OP_LIMIT is enabled, the CXI provider will use
semaphores to coordinate triggered operation usage between threads and across
processes using the same service ID. When triggered operation resources are
exhausted, fi_control(FI_QUEUE_WORK) will return -FI_ENOSPC. It is up to the
libfabric user to recover from this situation.

**Note:** Preventing triggered operation resource exhaustion with this method
may be expensive and result in a negative performance impact. It is encouraged
libfabric users avoid method unless absolutely needed. By default,
FI_CXI_ENABLE_TRIG_OP_LIMIT is disabled.

**Note:** Named semaphores are used to coordinated triggered operation resource
usage across multiple processes. System/node software may need to be implemented
to ensure all semaphores are unlinked during unexpected application termination.

**Note:** This feature is considered experimental and implementation may be
subjected to changed.

### CXI Domain get_dwq_depth Extension

The CXI domain get_dwq_depth extension returns the deferred work queue queue
depth (i.e. the number of triggered operation resources assigned to the service
ID used by the fi_domain). Libfabric users can use the returned queue depth to
coordinate resource usage.

For example, suppose the job launcher has configured a service ID with for 512
triggered operation resources. Since the CXI provider needs to consume 8 per
service ID, 504 should be usable by libfabric users. If the libfabric user knows
there are *N* processes using a given service ID and NIC, it can divide the 504
triggered operation resource among all *N* processes.

**Note:** This is the preferred method to prevent triggered operation resource
exhaustion since it does not introduce semaphores into the
fi_control(FI_QUEUE_WORK) critical path.

## Fork Support

The following subsections outline the CXI provider fork support.

### RDMA and Fork Overview

Under Linux, `fork()` is implemented using copy-on-write (COW) pages, so the
only penalty that it incurs is the time and memory required to duplicate the
parent's page tables, mark all of the process’s page structs as read only and
COW, and create a unique task structure for the child.

Due to the Linux COW fork policy, both parent and child processes’ virtual
addresses are mapped to the same physical address. The first process to write
to the virtual address will get a new physical page, and thus a new physical
address, with the same content as the previous physical page.

The Linux COW fork policy is problematic for RDMA NICs. RDMA NICs require
memory to be registered with the NIC prior to executing any RDMA operations. In
user-space, memory registration results in establishing a virtual address to
physical address mapping with the RDMA NIC. This resulting RDMA NIC
mapping/memory region does not get updated when the Linux COW fork policy is
executed.

Consider the following example:
- Process A is planning to perform RDMA with virtual address 0xffff0000 and a
size of 4096. This virtual address maps to physical address 0x1000.
- Process A registers this virtual address range with the RDMA NIC. The RDMA
NIC device driver programs its page tables to establish the virtual address
0xffff0000 to physical address 0x1000 mapping.
- Process A decides to fork Process B. Virtual address 0xffff0000 will now be
subjected to COW.
- Process A decides to write to virtual address 0xffff0000 before doing the
RDMA operation. This will trigger the Linux COW fork policy resulting in the
following:
    - Process A: Virtual address 0xffff0000 maps to new physical address
    0x2000
    - Process B: Virtual address 0xffff0000 maps to previous physical address
    0x1000
- Process A now executes an RDMA operation using the mapping/memory region
associated with virtual address 0xffff0000. Since COW occurred, the RDMA NIC
executes the RDMA operation using physical address 0x1000 which belongs to
Process B. This results in data corruption.

The crux of the issue is the parent issuing forks while trying to do RDMA
operations to registered memory regions. Excluding software RDMA emulation, two
options exist for RDMA NIC vendors to resolve this data corruption issue.
- Linux `madvise()` MADV_DONTFORK and MADV_DOFORK
- RDMA NIC support for on-demand paging (ODP)

#### Linux madvise() MADV_DONTFORK and MADV_DOFORK

The generic (i.e. non-vendor specific) RDMA NIC solution to the Linux COW fork
policy and RDMA problem is to use the following `madvise()` operations during
memory registration and deregistration:
- MADV_DONTFORK: Do not make the pages in this range available to the child
after a `fork()`. This is useful to prevent copy-on-write semantics from
changing the physical location of a page if the parent writes to it after a
`fork()`. (Such page relocations cause problems for hardware that DMAs into the
page.)
- MADV_DOFORK: Undo the effect of MADV_DONTFORK, restoring the default
behavior, whereby a mapping is inherited across `fork()`.

In the Linux kernel, MADV_DONTFORK will result in the virtual memory area struct
(VMA) being marked with the VM_DONTCOPY flag. VM_DONTCOPY signals to the Linux
kernel to not duplicate this VMA on fork. This effectively leaves a hole in
child process address space. Should the child reference the virtual address
corresponding to the VMA which was not duplicated, it will segfault.

In the previous example, if Process A issued `madvise(0xffff0000, 4096,
MADV_DONTFORK)` before performing RDMA memory registration, the physical address
0x1000 would have remained with Process A. This would prevent the Process A data
corruption as well. If Process B were to reference virtual address 0xffff0000, it
will segfault due to the hole in the virtual address space.

Using `madvise()` with MADV_DONTFORK may be problematic for applications
performing RDMA and page aliasing. Paging aliasing is where the parent process
uses part or all of a page to share information with the child process. If RDMA is
also being used for a separate portion of this page, the child process will
segfault when an access causes page aliasing.

#### RDMA NIC Support for ODP

An RDMA NIC vendor specific solution to the Linux COW fork policy and RDMA
problem is to use ODP. ODP allows for the RDMA NIC to generate page requests for
translations it does not have a physical address for. The following is an
updated example with ODP:
- Process A is planning to perform RDMA with virtual address 0xffff0000 and a
size of 4096. This virtual address maps to physical address 0x1000.
- Process A registers this virtual address range with the RDMA NIC. The RDMA NIC
device driver may optionally program its page tables to establish the virtual
address 0xffff0000 to physical address 0x1000 mapping.
- Process A decides to fork Process B. Virtual address 0xffff0000 will now be
subjected to COW.
- Process A decides to write to virtual address 0xffff0000 before doing the RDMA
operation. This will trigger the Linux COW fork policy resulting in the
following:
    - Process A: Virtual address 0xffff0000 maps to new physical address 0x2000
    - Process B: Virtual address 0xffff0000 maps to previous physical address
    0x1000
    - RDMA NIC device driver: Receives MMU invalidation event for Process A
    virtual address range 0xffff0000 through 0xffff0ffe. The device driver
    updates the corresponding memory region to no longer reference physical
    address 0x1000.
- Process A now executes an RDMA operation using the memory region associated
with 0xffff0000. The RDMA NIC will recognize the corresponding memory region as
no longer having a valid physical address. The RDMA NIC will then signal to the
device driver to fault in the corresponding address, if necessary, and update
the physical address associated with the memory region. In this case, the memory
region will be updated with physical address 0x2000. Once completed, the device
driver signals to the RDMA NIC to continue the RDMA operation. Data corruption
does not occur since RDMA occurred to the correct physical address.

A RDMA NIC vendor specific solution to the Linux COW fork policy and RDMA
problem is to use ODP. ODP allows for the RDMA NIC to generate page requests
for translations it does not have a physical address for.

### CXI Provider Fork Support

The CXI provider is subjected to the Linux COW fork policy and RDMA issues
described in section *RDMA and Fork Overview*. To prevent data corruption with
fork, the CXI provider supports the following options:
- CXI specific fork environment variables to enable `madvise()` MADV_DONTFORK
and MADV_DOFORK
- ODP Support*

**Formal ODP support pending.*

#### CXI Specific Fork Environment Variables

The CXI software stack has two environment variables related to fork:
0 CXI_FORK_SAFE: Enables base fork safe support. With this environment variable
set, regardless of value, libcxi will issue `madvise()` with MADV_DONTFORK on
the virtual address range being registered for RDMA. In addition, libcxi always
align the `madvise()` to the system default page size. On x86, this is 4 KiB. To
prevent redundant `madvise()` calls with MADV_DONTFORK against the same virtual
address region, reference counting is used against each tracked `madvise()`
region. In addition, libcxi will spilt and merge tracked `madvise()` regions if
needed. Once the reference count reaches zero, libcxi will call `madvise()` with
MADV_DOFORK, and no longer track the region.
- CXI_FORK_SAFE_HP: With this environment variable set, in conjunction with
CXI_FORK_SAFE, libcxi will not assume the page size is system default page size.
Instead, libcxi will walk `/proc/<pid>/smaps` to determine the correct page size
and align the `madvise()` calls accordingly. This environment variable should be
set if huge pages are being used for RDMA. To amortize the per memory
registration walk of `/proc/<pid>/smaps`, the libfabric MR cache should be used.

Setting these environment variables will prevent data corruption when the parent
issues a fork. But it may result in the child process experiencing a segfault if
it references a virtual address being used for RDMA in the parent process.

#### ODP Support and Fork

CXI provider ODP support would allow for applications to not have to set
CXI_FORK_SAFE and CXI_FORK_SAFE_HP to prevent parent process data corruption.
Enabling ODP to resolve the RDMA and fork issue may or may not result in a
performance impact. The concern with ODP is if the rate of invalidations and ODP
page requests are relatively high and occur at the same time, ODP timeouts may
occur. This would result in application libfabric data transfer operations
failing.

Please refer to the *CXI Provider ODP Support* for more information on how to
enable/disable ODP.

#### CXI Provider Fork Support Guidance

Since the CXI provider offloads the majority of the libfabric data transfer
operations to the NIC, thus enabling end-to-end RDMA between libfabric user
buffers, it is subjected to the issue described in section *RDMA and Fork
Overview*. For comparison, software emulated RDMA libfabric providers may not
have these issues since they rely on bounce buffers to facilitate data transfer.

The following is the CXI provider fork support guidance:
- Enable CXI_FORK_SAFE. If huge pages are also used, CXI_FORK_SAFE_HP should be
enabled as well. Since enabling this will result in `madvice()` with
MADV_DONTFORK, the following steps should be taken to prevent a child process
segfault:
    - Avoid using stack memory for RDMA
    - Avoid child process having to access a virtual address range the parent
    process is performing RDMA against
    - Use page-aligned heap allocations for RDMA
- Enable ODP and run without CXI_FORK_SAFE and CXI_FORK_SAFE_HP. The
functionality and performance of ODP with fork may be application specific.
Currently, ODP is not formally supported.

The CXI provider preferred approach is to use CXI_FORK_SAFE and
CXI_FORK_SAFE_HP. While it may require the application to take certain
precautions, it will result in a more portable application regardless of RDMA
NIC.

## Heterogenous Memory (HMEM) Supported Interfaces

The CXI provider supports the following OFI iface types: FI_HMEM_CUDA, FI_HMEM_ROCR, and FI_HMEM_ZE.

### FI_HMEM_ZE Limitations

The CXI provider only supports GPU direct RDMA with ZE device buffers if implicit scaling
is disabled. The following ZE environment variables disable implicit scaling:
EnableImplicitScaling=0 NEOReadDebugKeys=1.

For testing purposes only, the implicit scaling check can be disabled by setting the
following environment variable: FI_CXI_FORCE_ZE_HMEM_SUPPORT=1. This may need to be
combined with the following environment variable to get CXI provider memory registration
to work: FI_CXI_DISABLE_HMEM_DEV_REGISTER=1.

## Collectives (accelerated)

The CXI provider supports a limited set of collective operations specifically
intended to support use of the hardware-accelerated reduction features of the
CXI-supported NIC and fabric hardware.

These features are implemented using the (experimental) OFI collectives API. The
implementation supports the following collective functions:

* **fi_query_collective**()
* **fi_join_collective**()
* **fi_barrier**()
* **fi_broadcast**()
* **fi_reduce**()
* **fi_allreduce**()

### **fi_query_collective**()

Standard implementation that exposes the features described below.

### **fi_join_collective**()

The **fi_join_collective**() implementation is provider-managed. However, the
*coll_addr* parameter is not useful to the implementation, and must be
specified as FI_ADDR_NOTAVAIL. The *set* parameter must contain fi_addr_t
values that resolve to meaningful CXI addresses in the endpoint *fi_av*
structure. **fi_join_collective**() must be called for every address in the
*set* list, and must be progressed until the join operation is complete. There
is no inherent limit on join concurrency.

The join will create a multicast tree in the fabric to manage the collective
operations. This operation requires access to a secure Fabric Manager REST API
that constructs this tree, so any application that attempts to use accelerated
collectives will bind to libcurl and associated security libraries, which must
be available on the system.

There are hard limits to the number of multicast addresses available on a
system, and administrators may impose additional limits on the number of
multicast addresses available to any given collective job.

### fi_reduction operations

Payloads are limited to 32-byte data structures, and because they all use the
same underlying hardware model, they are all synchronizing calls. Specifically,
the supported functions are all variants of fi_allreduce().

* **fi_barrier** is **fi_allreduce** using an optimized no-data operator.
* **fi_broadcast** is **fi_allreduce** using FI_BOR, with data forced to zero for all but the root rank.
* **fi_reduce** is **fi_allreduce** with a result pointer ignored by all but the root rank.

All functions must be progressed to completion on all ranks participating in
the collective group. There is a hard limit of eight concurrent reductions on
each collective group, and attempts to launch more operations will return
<nobr>-FI_EAGAIN.</nobr>

**allreduce** supports the following hardware-accelerated reduction operators:

| Operator | Supported Datatypes |
| -------- | --------- |
| FI_BOR   | FI_UINT8, FI_UINT16, FI_UINT32, FI_UINT64 |
| FI_BAND  | FI_UINT8, FI_UINT16, FI_UINT32, FI_UINT64 |
| FI_BXOR  | FI_UINT8, FI_UINT16, FI_UINT32, FI_UINT64 |
| FI_MIN   | FI_INT64, FI_DOUBLE |
| FI_MAX   | FI_INT64, FI_DOUBLE |
| FI_SUM   | FI_INT64, FI_DOUBLE |
| FI_CXI_MINMAXLOC      | FI_INT64, FI_DOUBLE |
| FI_CXI_REPSUM         | FI_DOUBLE |

Data space is limited to 32 bytes in all cases except REPSUM, which supports
only a single FI_DOUBLE.

Only unsigned bitwise operators are supported.

Only signed integer arithmetic operations are are supported.

The MINMAXLOC operators are a mixed data representation consisting of two
values, and two indices. Each rank reports its minimum value and rank index,
and its maximum value and rank index. The collective result is the global
minimum value and rank index, and the global maximum value and rank index. Data
structures for these functions can be found int the fi_cxi_ext.h file. The
*datatype* should represent the type of the minimum/maximum values, and the
*count* must be 1.

The double-precision operators provide an associative (NUM) variant for MIN,
MAX, and MINMAXLOC. Default IEEE behavior is to treat any operation with NaN as
invalid, including comparison, which has the interesting property of causing:

    MIN(NaN, value) => NaN
    MAX(NaN, value) => NaN

This means that if NaN creeps into a MIN/MAX reduction in any rank, it tends to
poison the entire result. The associative variants instead effectively ignore
the NaN, such that:

    MIN(NaN, value) => value
    MAX(NaN, value) => value

The REPSUM operator implements a reproducible (associative) sum of
double-precision values. The payload can accommodate only a single
double-precision value per reduction, so *count* must be 1.

See: [Berkeley reproducible sum algorithm](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-121.pdf)
https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-121.pdf

### double precision rounding

C99 defines four rounding modes for double-precision SUM, and some systems may
support a "flush-to-zero" mode for each of these, resulting in a total of eight
different modes for double-precision sum.

The fabric hardware supports all eight modes transparently.

Although the rounding modes have thread scope, all threads, processes, and
nodes should use the same rounding mode for any single reduction.

### reduction flags

The reduction operations supports two flags:

* **FI_MORE**
* **FI_CXI_PRE_REDUCED** (overloads **FI_SOURCE**)

The **FI_MORE** flag advises that the *result* data pointer represents an
opaque, local reduction accumulator, and will be used as the destination of the
reduction. This operation can be repeated any number of times to accumulate
results locally, and spans the full set of all supported reduction operators.
The *op*, *count*, and *datatype* values must be consistent for all calls. The
operation ignores all global or static variables &mdash; it can be treated as a
*pure* function call &mdash; and returns immediately. The caller is responsible
for protecting the accumulator memory if it is used by multiple threads or
processes on a compute node.

If **FI_MORE** is omitted, the destination is the fabric, and this will
initiate a fabric reduction through the associated endpoint. The reduction must
be progressed, and upon successful completion, the *result* data pointer will
be filled with the final reduction result of *count* elements of type
*datatype*.

The **FI_CXI_PRE_REDUCED** flag advises that the source data pointer represents
an opaque reduction accumulator containing pre-reduced data. The *count* and
*datatype* arguments are ignored.

if **FI_CXI_PRE_REDUCED** is omitted, the source is taken to be user data with
*count* elements of type *datatype*.

The opaque reduction accumulator is exposed as **struct cxip_coll_accumulator**
in the fi_cxi_ext.h file.

**Note**: The opaque reduction accumulator provides extra space for the
expanded form of the reproducible sum, which carries the extra data required to
make the operation reproducible in software.

# OPTIMIZATION

## Optimized MRs

The CXI provider has two separate MR implementations: standard and optimized.
Standard MRs are designed to support applications which require a large number
of remote memory regions. Optimized MRs are designed to support one-sided
programming models that allocate a small number of large remote memory windows.
The CXI provider can achieve higher RMA Write rates when targeting an optimized
MR.

Both types of MRs are allocated using fi_mr_reg. MRs with client-provided key in
the range [0-99] are optimized MRs. MRs with key greater or equal to 100 are
standard MRs. An application may create a mix of standard and optimized MRs. To
disable the use of optimized MRs, set environment variable
*FI_CXI_OPTIMIZED_MRS=false*. When disabled, all MR keys are available and all MRs
are implemented as standard MRs. All communicating processes must agree on the
use of optimized MRs.

When FI_MR_PROV_KEY mr_mode is specified caching of remote access MRs is enabled,
which can improve registration/de-registration performance in RPC type applications,
that wrap RMA operations within a message RPC protocol. Optimized MRs will be
preferred, but will fallback to standard MRs if insufficient hardware resources are
available.

## Optimized RMA

Optimized MRs are one requirement for the use of low overhead packet formats
which enable higher RMA Write rates. An RMA Write will use the low overhead
format when all the following requirements are met:

* The Write targets an optimized MR
* The target MR does not require remote completion notifications (no
  *FI_RMA_EVENT*)
* The Write does not have ordering requirements (no *FI_RMA_WAW*)

Theoretically, Cassini has resources to support 64k standard MRs or 2k optimized
MRs. Practically, the limits are much lower and depend greatly on application
behavior.

Hardware counters can be used to validate the use of the low overhead packets.
The counter C_CNTR_IXE_RX_PTL_RESTRICTED_PKT counts the number of low overhead
packets received at the target NIC. Counter C_CNTR_IXE_RX_PTL_UNRESTRICTED_PKT
counts the number of ordered RDMA packets received at the target NIC.

Message rate performance may be further optimized by avoiding target counting
events. To avoid counting events, do not bind a counter to the MR. To validate
optimal writes without target counting events, monitor the counter:
C_CNTR_LPE_PLEC_HITS.

## Unreliable AMOs

By default, all AMOs are resilient to intermittent packet loss in the network.
Cassini implements a connection-based reliability model to support reliable
execution of AMOs.

The connection-based reliability model may be disabled for AMOs in order to
increase message rate. With reliability disabled, a lost AMO packet will result
in operation failure. A failed AMO will be reported to the client in a
completion event as usual. Unreliable AMOs may be useful for applications that
can tolerate intermittent AMO failures or those where the benefit of increased
message rate outweighs by the cost of restarting after a failure.

Unreliable, non-fetching AMOs may be performed by specifying the
*FI_CXI_UNRELIABLE* flag. Unreliable, fetching AMOs are not supported. Unreliable
AMOs must target an optimized MR and cannot use remote completion notification.
Unreliable AMOs are not ordered.

## High Rate Put

High Rate Put (HRP) is a feature that increases message rate performance of RMA
and unreliable non-fetching AMO operations at the expense of global ordering
guarantees.

HRP responses are generated by the fabric egress port. Responses are coalesced
by the fabric to achieve higher message rates. The completion event for an HRP
operation guarantees delivery but does not guarantee global ordering. If global
ordering is needed following an HRP operation, the source may follow the
operation with a normal, fenced Put.

HRP RMA and unreliable AMO operations may be performed by specifying the
*FI_CXI_HRP* flag. HRP AMOs must also use the *FI_CXI_UNRELIABLE* flag. Monitor the
hardware counter C_CNTR_HNI_HRP_ACK at the initiator to validate that HRP is in
use.

## Counters

Cassini offloads light-weight counting events for certain types of operations.
The rules for offloading are:

* Counting events for RMA and AMO source events are always offloaded.
* Counting events for RMA and AMO target events are always offloaded.
* Counting events for Sends are offloaded when message size is less than the
  rendezvous threshold.
* Counting events for message Receives are never offloaded by default.

Software progress is required to update counters unless the criteria for
offloading are met.

# RUNTIME PARAMETERS

The CXI provider checks for the following environment variables:

*FI_CXI_ODP*
:   Enables on-demand paging. If disabled, all DMA buffers are pinned.
    If enabled and mr_mode bits in the hints exclude FI_MR_ALLOCATED,
    then ODP mode will be used.

*FI_CXI_FORCE_ODP*
:   Experimental value that can be used to force the use of ODP mode
    even if FI_MR_ALLOCATED is set in the mr_mode hint bits. This is
    intended to be used primarily for testing.

*FI_CXI_ATS*
:   Enables PCIe ATS. If disabled, the NTA mechanism is used.

*FI_CXI_ATS_MLOCK_MODE*
:   Sets ATS mlock mode. The mlock() system call may be used in conjunction
    with ATS to help avoid network page faults. Valid values are "off" and
    "all". When mlock mode is "off", the provider does not use mlock(). An
    application using ATS without mlock() may experience network page faults,
    reducing network performance. When ats_mlock_mode is set to "all", the
    provider uses mlockall() during initialization with ATS. mlockall() causes
    all mapped addresses to be locked in RAM at all times. This helps to avoid
    most network page faults. Using mlockall() may increase pressure on
    physical memory.  Ignored when ODP is disabled.

*FI_CXI_RDZV_THRESHOLD*
:   Message size threshold for rendezvous protocol.

*FI_CXI_RDZV_GET_MIN*
:   Minimum rendezvous Get payload size. A Send with length less than or equal
    to *FI_CXI_RDZV_THRESHOLD* plus *FI_CXI_RDZV_GET_MIN* will be performed
    using the eager protocol. Larger Sends will be performed using the
    rendezvous protocol with *FI_CXI_RDZV_THRESHOLD* bytes of payload sent
    eagerly and the remainder of the payload read from the source using a Get.
    *FI_CXI_RDZV_THRESHOLD* plus *FI_CXI_RDZV_GET_MIN* must be less than or
    equal to *FI_CXI_OFLOW_BUF_SIZE*.

*FI_CXI_RDZV_EAGER_SIZE*
:   Eager data size for rendezvous protocol.

*FI_CXI_RDZV_PROTO*
:   Direct the provider to use a preferred protocol to transfer non-eager
    rendezvous data.
    *FI_CXI_RDZV_PROTO*= default | alt_read

    To use an alternate protocol, the CXI driver property rdzv_get_en should be
    set to "0". The "alt_read" rendezvous protocol may help improve collective
    operation performance. Note that all rendezvous protocol use RDMA to transfer
    eager and non-eager rendezvous data.

*FI_CXI_DISABLE_NON_INJECT_MSG_IDC*
:   Experimental option to disable favoring IDC for transmit of small messages
    when FI_INJECT is not specified. This can be useful with GPU source buffers
    to avoid the host copy in cases a performant copy can not be used. The default
    is to use IDC for all messages less than IDC size.

*FI_CXI_DISABLE_HOST_REGISTER*
:   Disable registration of host buffers (overflow and request) with GPU. There
    are scenarios where using a large number of processes per GPU results in page
    locking excessive amounts of memory degrading performance and/or restricting
    process counts. The default is to register buffers with the GPU.

*FI_CXI_OFLOW_BUF_SIZE*
:   Size of overflow buffers. Increasing the overflow buffer size allows for
    more unexpected message eager data to be held in single overflow buffer.
    The default size is 2MB.

*FI_CXI_OFLOW_BUF_MIN_POSTED/FI_CXI_OFLOW_BUF_COUNT*
:   The minimum number of overflow buffers that should be posted. The default
    minimum posted count is 3. Buffers will grow unbounded to support
    outstanding unexpected messages. Care should be taken to size appropriately
    based on job scale, size of eager data, and the amount of unexpected
    message traffic to reduce the need for flow control.

*FI_CXI_OFLOW_BUF_MAX_CACHED*
:   The maximum number of overflow buffers that will be cached. The default
    maximum count is 3 * FI_CXI_OFLOW_BUF_MIN_POSTED. A value of zero indicates
    that once a overflow buffer is allocated it will be cached and used as
    needed. A non-zero value can be used with bursty traffic to shrink the
    number of allocated buffers to the maximum count when they are no longer
    needed.

*FI_CXI_SAFE_DEVMEM_COPY_THRESHOLD
:   Defines the maximum CPU memcpy size for HMEM device memory that is
    accessible by the CPU with load/store operations.

*FI_CXI_OPTIMIZED_MRS*
:   Enables optimized memory regions. See section
    *CXI Domain Control Extensions* on how to enable/disable optimized MRs at
    the domain level instead of for the global process/job.

*FI_CXI_MR_MATCH_EVENTS*
:   Enabling MR match events in a client/server environment can be used
    to ensure that memory backing a memory region cannot be remotely
    accessed after the MR has been closed, even if it that memory remains
    mapped in the libfabric MR cache. Manual progress must be made at the
    target to process the MR match event accounting and avoid event queue
    overflow. There is a slight additional cost in the creation and
    tear-down of MR. This option is disabled by default.

    See section *CXI Domain Control Extensions* on how to enable MR match
    events at the domain level instead of for the global process/job.

*FI_CXI_PROV_KEY_CACHE*
:   Enabled by default, the caching of remote MR provider keys can be
    disable by setting to 0.

    See section *CXI Domain Control Extensions* on how to disable the
    remote provider key cache at the domain level instead of for the
    global process/job.

*FI_CXI_LLRING_MODE*
:   Set the policy for use of the low-latency command queue ring mechanism.
    This mechanism improves the latency of command processing on an idle
    command queue.  Valid values are idle, always, and never.

*FI_CXI_CQ_POLICY*
:   Experimental. Set Command Queue write-back policy. Valid values are always,
    high_empty, low_empty, and low. "always", "high", and "low" refer to the
    frequency of write-backs. "empty" refers to whether a write-back is
    performed when the queue becomes empty.

*FI_CXI_DEFAULT_VNI*
:   Default VNI value used only for service IDs where the VNI is not restricted.

*FI_CXI_RNR_MAX_TIMEOUT_US*
:   When using the endpoint FI_PROTO_CXI_RNR protocol, this setting is used to
    control the maximum time from the original posting of the message that the
    message should be retried. A value of 0 will return an error completion
    on the first RNR ack status.

*FI_CXI_EQ_ACK_BATCH_SIZE*
:   Number of EQ events to process before writing an acknowledgement to HW.
    Batching ACKs amortizes the cost of event acknowledgement over multiple
    network operations.

*FI_CXI_RX_MATCH_MODE*
:   Specify the receive message matching mode to be utilized.
    *FI_CXI_RX_MATCH_MODE=*hardware | software | hybrid

    *hardware* - Message matching is fully offloaded, if resources become
    exhausted flow control will be performed and existing unexpected message
    headers will be onloaded to free resources.

    *software* - Message matching is fully onloaded.

    *hybrid* - Message matching begins fully offloaded, if resources become
    exhuasted hardware will transition message matching to a hybrid of
    hardware and software matching.

    For both *"hybrid"* and *"software"* modes and care should be taken to
    minimize the threshold for rendezvous processing
    (i.e. *FI_CXI_RDZV_THRESHOLD* + *FI_CXI_RDZV_GET_MIN*). When running in
    software endpoint mode the environment variables *FI_CXI_REQ_BUF_SIZE*
    and *FI_CXI_REQ_BUF_MIN_POSTED* are used to control the size and number
    of the eager request buffers posted to handle incoming unmatched messages.

*FI_CXI_HYBRID_PREEMPTIVE*
:   When in hybrid mode, this variable can be used to enable preemptive
    transitions to software matching. This is useful at scale for poorly
    written applications with a large number of unexpected messages
    where reserved resources may be insufficient to prevent to prevent
    starvation of software request list match entries. Default is 0, disabled.

*FI_CXI_HYBRID_RECV_PREEMPTIVE*
:   When in hybrid mode, this variable can be used to enable preemptive
    transitions to software matching. This is useful at scale for poorly
    written applications with a large number of unmatched posted receives
    where reserved resources may be insufficient to prevent starvation of
    software request list match entries. Default is 0, disabled.

*FI_CXI_HYBRID_POSTED_RECV_PREEMPTIVE*
:   When in hybrid mode, this variable can be used to enable preemptive
    transitions to software matching when the number of posted receives
    exceeds the user requested RX size attribute. This is useful for
    applications where they may not know the exact number of posted receives
    and they are expereincing application termination due to event queue
    overflow. Default is 0, disabled.

*FI_CXI_HYBRID_UNEXPECTED_MSG_PREEMPTIVE*
:   When in hybrid mode, this variable can be used to enable preemptive
    transitions to software matching when the number of hardware unexpected
    messages exceeds the user requested RX size attribute. This is useful for
    applications where they may not know the exact number of posted receives
    and they are expereincing application termination due to event queue
    overflow. Default is 0, disabled.

*FI_CXI_REQ_BUF_SIZE*
:   Size of request buffers. Increasing the request buffer size allows for more
    unmatched messages to be sent into a single request buffer. The default
    size is 2MB.

*FI_CXI_REQ_BUF_MIN_POSTED*
:   The minimum number of request buffers that should be posted. The default
    minimum posted count is 4. The number of buffers will grow unbounded to
    support outstanding unexpected messages. Care should be taken to size
    appropriately based on job scale and the size of eager data to reduce
    the need for flow control.

*FI_CXI_REQ_BUF_MAX_CACHED/FI_CXI_REQ_BUF_MAX_COUNT*
:   The maximum number of request buffers that will be cached. The default
    maximum count is 0. A value of zero indicates that once a request buffer
    is allocated it will be cached and used as needed. A non-zero value can
    be used with bursty traffic to shrink the number of allocated buffers to
    a maximum count when they are no longer needed.

*FI_CXI_MSG_LOSSLESS*
:   Enable or disable lossless receive matching. If hardware resources are
    exhausted, hardware will pause the associated traffic class until a
    overflow buffer (hardware match mode) or request buffer (software match
    mode or hybrid match mode) is posted. This is considered experimental and
    defaults to disabled.

*FI_CXI_FC_RETRY_USEC_DELAY*
:   Number of micro-seconds to sleep before retrying a dropped side-band, flow
    control message. Setting to zero will disable any sleep.

*FI_UNIVERSE_SIZE*
:   Defines the maximum number of processes that will be used by distribute
    OFI application. Note that this value is used in setting the default
    control EQ size, see FI_CXI_CTRL_RX_EQ_MAX_SIZE.

*FI_CXI_CTRL_RX_EQ_MAX_SIZE*
:   Max size of the receive event queue used for side-band/control messages.
    Default receive event queue size is based on FI_UNIVERSE_SIZE. Increasing the
    receive event queue size can help prevent side-band/control messages from
    being dropped and retried but at the cost of additional memory usage. Size is
    always aligned up to a 4KiB boundary.

*FI_CXI_DEFAULT_CQ_SIZE*
:   Change the provider default completion queue size expressed in entries. This
    may be useful for applications which rely on middleware, and middleware defaults
    the completion queue size to the provider default.

*FI_CXI_DISABLE_EQ_HUGETLB/FI_CXI_DISABLE_CQ_HUGETLB*
:   By default, the provider will attempt to allocate 2 MiB hugetlb pages for
    provider event queues. Disabling hugetlb support will cause the provider
    to fallback to memory allocators using host page sizes.
    FI_CXI_DISABLE_EQ_HUGETLB replaces FI_CXI_DISABLE_CQ_HUGETLB, however use
    of either is still supported.

*FI_CXI_DEFAULT_TX_SIZE*
:   Set the default tx_attr.size field to be used by the provider if the size
    is not specified in the user provided fi_info hints.

*FI_CXI_DEFAULT_RX_SIZE*
:   Set the default rx_attr.size field to be used by the provider if the size
    is not specified in the user provided fi_info hints.

*FI_CXI_SW_RX_TX_INIT_MAX*
:   Debug control to override the number of TX operations that can be
    outstanding that are initiated by software RX processing. It has no impact
    on hardware initiated RX rendezvous gets.

*FI_CXI_DEVICE_NAME*
:   Restrict CXI provider to specific CXI devices. Format is a comma separated
    list of CXI devices (e.g. cxi0,cxi1).

*FI_CXI_TELEMETRY*
:   Perform a telemetry delta between fi_domain open and close. Format is a
    comma separated list of telemetry files as defined in
    /sys/class/cxi/cxi*/device/telemetry/. The ALL-in-binary file in this
    directory is invalid. Note that these are per CXI interface counters and not
    per CXI process per interface counters.

*FI_CXI_TELEMETRY_RGID*
:   Resource group ID (RGID) to restrict the telemetry collection to. Value less
    than 0 is no restrictions.

*FI_CXI_CQ_FILL_PERCENT*
:   Fill percent of underlying hardware event queue used to determine when
    completion queue is saturated. A saturated completion queue results in the
    provider returning -FI_EAGAIN for data transfer and other related libfabric
    operations.

*FI_CXI_COMPAT*
:   Temporary compatibility to allow use of pre-upstream values for FI_ADDR_CXI and
    FI_PROTO_CXI. Compatibility can be disabled to verify operation with upstream
    constant values and to enable access to conflicting provider values. The default
    setting of 1 specifies both old and new constants are supported. A setting of 0
    disables support for old constants and can be used to test that an application is
    compatible with the upstream values. A setting of 2 is a safety fallback that if
    used the provider will only export fi_info with old constants and will be incompatible
    with libfabric clients that been recompiled.

*FI_CXI_COLL_FABRIC_MGR_URL*
:   **accelerated collectives:** Specify the HTTPS address of the fabric manager REST API
    used to create specialized multicast trees for accelerated collectives. This parameter
    is **REQUIRED** for accelerated collectives, and is a fixed, system-dependent value.

*FI_CXI_COLL_TIMEOUT_USEC*
:   **accelerated collectives:** Specify the reduction engine timeout. This should be
    larger than the maximum expected compute cycle in repeated reductions, or acceleration
    can create incast congestion in the switches. The relative performance benefit of
    acceleration declines with increasing compute cycle time, dropping below one percent at
    32 msec (32000). Using acceleration with compute cycles larger than 32 msec is not
    recommended except for experimental purposes. Default is 32 msec (32000), maximum is
    20 sec (20000000).

*FI_CXI_COLL_USE_DMA_PUT*
:   **accelerated collectives:** Use DMA for collective packet put. This uses DMA to
    inject reduction packets rather than IDC, and is considered experimental. Default
    is false.

*FI_CXI_DISABLE_HMEM_DEV_REGISTER*
:   Disable registering HMEM device buffer for load/store access. Some HMEM devices
    (e.g. AMD, Nvidia, and Intel GPUs) support backing the device memory by the PCIe BAR.
    This enables software to perform load/stores to the device memory via the BAR instead
    of using device DMA engines. Direct load/store access may improve performance.

*FI_CXI_FORCE_ZE_HMEM_SUPPORT*
:   Force the enablement of ZE HMEM support. By default, the CXI provider will only
    support ZE memory registration if implicit scaling is disabled (i.e. the environment
    variables EnableImplicitScaling=0 NEOReadDebugKeys=1 are set). Set
    FI_CXI_FORCE_ZE_HMEM_SUPPORT to 1 will cause the CXI provider to skip the implicit
    scaling checks. GPU direct RDMA may or may not work in this case.

*FI_CXI_ENABLE_TRIG_OP_LIMIT*
:   Enable enforcement of triggered operation limit. Doing this can prevent
    fi_control(FI_QUEUE_WORK) deadlocking at the cost of performance.

Note: Use the fi_info utility to query provider environment variables:
<code>fi_info -p cxi -e</code>

# CXI EXTENSIONS

The CXI provider supports various fabric-specific extensions. Extensions are
accessed using the fi_open_ops function.

### CXI Domain Control Extensions

The **fi_control**() function is extended for domain FIDs to query and override
global environment settings for a specific domain. This is useful for example
where the application process also includes a client API that has different
optimizations and protections.

Command *FI_OPT_CXI_GET_OPTIMIZED* where the argument is a pointer to a bool.
The call returns the setting for optimized MR usage for the domain. The default
is determined by the environment setting of *FI_CXI_OPTIMIZED_MRS*.

Command *FI_OPT_CXI_SET_OPTIMIZED* where the argument is a pointer to a bool
initialized to true or false. The call enables or disables the use of optimized
MRs for the domain. If the domain is not configured for FI_MR_PROV_KEY MR mode,
the call will fail with -FI_EINVAL, it is not supported for client generated
keys. It must be called prior to MR being created.

Command *FI_OPT_CXI_GET_MR_MATCH_EVENTS* where the argument is a pointer to a
bool. The call returns the setting for MR Match Event accounting for the
domain. The default is determined by the environment setting of
*FI_CXI_MR_MATCH_EVENTS*.

Command *FI_OPT_CXI_SET_MR_MATCH_EVENTS* where the argument is a pointer to a
bool initialized to true or false. This call enables or disables the use of MR
Match Event counting. This ensures that memory backing a MR cannot be accessed
after invoking fi_close() on the MR, even if that memory remains in the
libfabric MR cache. Manual progress must be made to process events at the RMA
destination. It can only be changed prior to any EP or MR being created.

Command *FI_OPT_CXI_GET_PROV_KEY_CACHE* where the argument is a pointer to a
bool. The call returns the setting for enabling use of the remote MR
cache for provider keys for the domain. The default is determined by the
environment setting of *FI_CXI_PROV_KEY_CACHE* and is only valid if
FI_MR_PROV_KEY MR mode is used.

Command *FI_OPT_CXI_SET_PROV_KEY_CACHE* where the argument is a pointer to a
bool initialized to true or false. This call enables or disables the use of
the remote MR cache for provider keys for the domain. By default the cache
is enabled and can be used for provider keys that do not require events.
The command will fail with -FI_EINVAL if FI_MR_PROV_KEY MR mode is not in use.
It can only be changed prior to any MR being created.

## CXI Domain Extensions

CXI domain extensions have been named *FI_CXI_DOM_OPS_6*. The flags parameter
is ignored. The fi_open_ops function takes a `struct fi_cxi_dom_ops`. See an
example of usage below:

```c
struct fi_cxi_dom_ops *dom_ops;

ret = fi_open_ops(&domain->fid, FI_CXI_DOM_OPS_4, 0, (void **)&dom_ops, NULL);
```

The following domain extensions are defined:

```c
struct fi_cxi_dom_ops {
	int (*cntr_read)(struct fid *fid, unsigned int cntr, uint64_t *value,
		      struct timespec *ts);
	int (*topology)(struct fid *fid, unsigned int *group_id,
			unsigned int *switch_id, unsigned int *port_id);
	int (*enable_hybrid_mr_desc)(struct fid *fid, bool enable);
	size_t (*ep_get_unexp_msgs)(struct fid_ep *fid_ep,
				    struct fi_cq_tagged_entry *entry,
				    size_t count, fi_addr_t *src_addr,
				    size_t *ux_count);
	int (*get_dwq_depth)(struct fid *fid, size_t *depth);
};
```

*cntr_read* extension is used to read hardware counter values. Valid values
of the cntr argument are found in the Cassini-specific header file
cassini_cntr_defs.h. Note that Counter accesses by applications may be
rate-limited to 1HZ.

*topology* extension is used to return CXI NIC address topology information
for the domain. Currently only a dragonfly fabric topology is reported.

The enablement of hybrid MR descriptor mode allows for libfabric users
to optionally pass in a valid MR desc for local communications operations.

The get unexpected message function is used to obtain a list of
unexpected messages associated with an endpoint. The list is returned
as an array of CQ tagged entries set in the following manner:

```
struct fi_cq_tagged_entry {
	.op_context = NULL,
	.flags = any of [FI_TAGGED | FI_MSG | FI_REMOTE_CQ_DATA],
	.len = message length,
	.buf = NULL,
	.data = CQ data if FI_REMOTE_CQ_DATA set
	.tag = tag if FI_TAGGED set
};
```

If the src_addr or entry array is NULL, only the ux_count of
available unexpected list entries will be returned. The parameter
count specifies the size of the array provided, if it is 0 then only
the ux_count will be returned. The function returns the number of
entries written to the array or a negative errno. On successful return,
ux_count will always be set to the total number of unexpected messages available.

*enable_hybrid_mr_desc* is used to enable hybrid MR descriptor mode. Hybrid MR
desc allows for libfabric users to optionally pass in a valid MR desc for local
communication operations. This is currently only used for RMA and AMO transfers.

*get_dwq_depth* is used to get the depth of the deferred work queue. The depth
is the number of triggered operation commands which can be queued to hardware.
The depth is not per fi_domain but rather per service ID. Since a single service
ID is intended to be shared between all processing using the same NIC in a job
step, the triggered operations are shared across processes.

*enable_mr_match_events* and *enable_optimized_mrs* have been deprecated
in favor of using the fi_control() API. While the can be still be called via
the domain ops, They will be removed from the domain opts prior to software
release 2.2.

## CXI Counter Extensions

CXI counter extensions have been named *FI_CXI_COUNTER_OPS*. The flags parameter
is ignored. The fi_open_ops function takes a `struct fi_cxi_cntr_ops`. See an
example of usage below.

```c
struct fi_cxi_cntr_ops *cntr_ops;

ret = fi_open_ops(&cntr->fid, FI_CXI_COUNTER_OPS, 0, (void **)&cntr_ops, NULL);
```

The following domain extensions are defined:

```c
struct fi_cxi_cntr_ops {
	/* Set the counter writeback address to a client provided address. */
	int (*set_wb_buffer)(struct fid *fid, const void *buf, size_t len);

	/* Get the counter MMIO region. */
	int (*get_mmio_addr)(struct fid *fid, void **addr, size_t *len);
};
```

## CXI Counter Writeback Flag

If a client is using the CXI counter extensions to define a counter writeback
buffer, the CXI provider will not update the writeback buffer success or
failure values for each hardware counter success or failure update. This can
especially create issues when clients expect the completion of a deferred
workqueue operation to generate a counter writeback. To support this, the flag
*FI_CXI_CNTR_WB* can be used in conjunction with a deferred workqueue operation
to force a writeback at the completion of the deferred workqueue operation. See
an example of usage below.

```c
struct fi_op_rma rma = {
  /* Signal to the provider the completion of the RMA should trigger a
   * writeback.
   */
  .flags = FI_CXI_CNTR_WB,
};

struct fi_deferred_work rma_work = {
  .op_type = FI_OP_READ,
  .triggering_counter = cntr,
  .completion_cntr = cntr,
  .threshold = 1,
  .op.rma = &rma,
};

ret = fi_control(&domain->fid, FI_QUEUE_WORK, &rma_work);
```

**Note:** Using *FI_CXI_CNTR_WB* will lead to additional hardware usage. To
conserve hardware resources, it is recommended to only use the *FI_CXI_CNTR_WB*
when a counter writeback is absolutely required.

## CXI Alias EP Overrides

A transmit alias endpoint can be created and configured to utilize
a different traffic class than the original endpoint. This provides a
lightweight mechanism to utilize multiple traffic classes within a process.
Message order between the original endpoint and the alias endpoint is
not defined/guaranteed. See example usage below for setting the traffic
class of a transmit alias endpoint.

```c
#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cxi_ext.h>     // Ultimately fi_ext.h

struct fid_ep *ep;
. . .

struct fid_ep *alias_ep = NULL;
uint32_t tclass = FI_TC_LOW_LATENCY;
uint64_t op_flags = FI_TRANSMIT | desired data operation flags;

ret = fi_ep_alias(ep, &alias_ep, op_flags);
if (ret)
    error;

ret = fi_set_val(&alias_ep->fid, FI_OPT_CXI_SET_TCLASS, (void *)&tlcass);
if (ret)
    error;
```

In addition, the alias endpoint message order may be modified to override
the default endpoint message order. Message order between the modified
alias endpoint and the original endpoint is not guaranteed. See example
usage below for setting the traffic class of a transmit alias endpoint.

```c
uint64_t msg_order = FI_ORDER_RMA_WAW;

ret = fi_set_val(&alias_ep->fid, FI_OPT_CXI_SET_MSG_ORDER,
                 (void *)&msg_order);
if (ret)
    error;
```

When an endpoint does not support FI_FENCE (e.g. optimized MR), a provider
specific transmit flag, FI_CXI_WEAK_FENCE, may be specified on an alias EP
to issue a FENCE operation to create a data ordering point for the alias.
This is supported for one-sided operations only.

Alias EP must be closed prior to closing the original EP.

## PCIe Atomics
The CXI provider has the ability to issue a given libfabric atomic memory
operation as a PCIe operation as compared to a NIC operation. The CXI
provider extension flag FI_CXI_PCIE_AMO is used to signify this.

Since not all libfabric atomic memory operations can be executed as a PCIe
atomic memory operation, `fi_query_atomic()` could be used to query if a
given libfabric atomic memory operation could be executed as PCIe atomic
memory operation.

The following is a query to see if a given libfabric operation can be a
PCIe atomic operation.
```c
int ret;
struct fi_atomic_attr out_attrs;

/* Query if non-fetching PCIe atomic is supported. */
ret = fi_query_atomic(domain, FI_UINT32, FI_SUM, &out_attrs, FI_CXI_PCIE_AMO);

/* Query if fetching PCIe atomic is supported. */
ret = fi_query_atomic(domain, FI_UINT32, FI_SUM, &out_attrs,
                      FI_FETCH_ATOMIC | FI_CXI_PCIE_AMO);
```

The following is how to issue a PCIe atomic operation.
```c
ssize_t ret;
struct fi_msg_atomic msg;
struct fi_ioc resultv;
void *result_desc;
size_t result_count;

ret = fi_fetch_atomicmsg(ep, &msg, &resultv, &result_desc, result_count,
                         FI_CXI_PCIE_AMO);
```

**Note:** The CXI provider only supports PCIe fetch add for UINT32_T, INT32_t,
UINT64_T, and INT64_t. This support requires enablement of PCIe fetch add in
the CXI driver, and it comes at the cost of losing NIC atomic support for another
libfabric atomic operation.

**Note:** Ordering between PCIe atomic operations and NIC atomic/RMA operations is
undefined.

To enable PCIe fetch add for libfabric, the following CXI driver kernel module
parameter must be set to non-zero.

```
/sys/module/cxi_core/parameters/amo_remap_to_pcie_fadd
```

The following are the possible values for this kernel module and the impact of
each value:
- -1: Disable PCIe fetch add support. FI_CXI_PCIE_AMO is not supported.
- 0: Enable PCIe fetch add support. FI_MIN is not supported.
- 1: Enable PCIe fetch add support. FI_MAX is not supported.
- 2: Enable PCIe fetch add support. FI_SUM is not supported.
- 4: Enable PCIe fetch add support. FI_LOR is not supported.
- 5: Enable PCIe fetch add support. FI_LAND is not supported.
- 6: Enable PCIe fetch add support. FI_BOR is not supported.
- 7: Enable PCIe fetch add support. FI_BAND is not supported.
- 8: Enable PCIe fetch add support. FI_LXOR is not supported.
- 9: Enable PCIe fetch add support. FI_BXOR is not supported.
- 10: Enable PCIe fetch add support. No loss of default CXI provider AMO
functionality.

Guidance is to default amo_remap_to_pcie_fadd to 10.

# FABTESTS

The CXI provider does not currently support fabtests which depend on IP
addressing.

fabtest RDM benchmarks are supported, like:

```c
# Start server by specifying source PID and interface
./fabtests/benchmarks/fi_rdm_tagged_pingpong -B 10 -s cxi0

# Read server NIC address
CXI0_ADDR=$(cat /sys/class/cxi/cxi0/device/properties/nic_addr)

# Start client by specifying server PID and NIC address
./fabtests/benchmarks/fi_rdm_tagged_pingpong -P 10 $CXI0_ADDR

# The client may be bound to a specific interface, like:
./fabtests/benchmarks/fi_rdm_tagged_pingpong -B 10 -s cxi1 -P 10 $CXI0_ADDR
```

Some functional fabtests are supported (including fi_bw). Others use IP sockets
and are not yet supported.

multinode fabtests are not yet supported.

ubertest is supported for test configs matching the provider's current
capabilities.

unit tests are supported where the test feature set matches the CXI provider's
current capabilities.

# ERRATA

* Fetch and compare type AMOs with FI_DELIVERY_COMPLETE or FI_MATCH_COMPLETE
  completion semantics are not supported with FI_RMA_EVENT.

# Libfabric CXI Provider User Programming and Troubleshooting Guide

The scope of the following subsection is to provide guidance and/or troubleshooting tips
for users of the libfabric CXI provider. The scope of this section is not a full guide
for user libfabric.

## Sizing Libfabric Objects Based on Expected Usage

The CXI provider uses various libfabric object attribute size and/or libfabric enviroment
variables to size hardware related resources accordingly. Failure to size resources properly
can result in the CXI provider frequently returning -FI_EAGAIN which may negatively impact
performance. The following subsection outline important sizing related attributes and
environment variables.

### Completion Queue Size Attribute

The CXI provider uses completion queue attribute size to size various software and hardware
event queues used to generate libfabric completion events. While the size of the software
queues may grow, hardware event queue sizes are static. Failing to size hardware queues
properly may result in CXI provider returning -FI_EAGAIN frequently for data transfer
operations. When this error is returned, user should progress the corresponding endpoint
completion queues by calling fi_cq_read().

Users are encouraged to set the completion queue size attribute based on the expected
number of inflight RDMA operations to and from a single endpoint. For users which are
relying on the provider default value (e.g. MPI), the FI_CXI_DEFAULT_CQ_SIZE environment
variable can be used to override the provider default value.

### Endpoint Recieve Size Attribute

The CXI provider uses the endpoint receive size attribute to size internal command
and hardware event queues. Failing to size the either command queue correctly can result
in the CXI provider returning -FI_EAGAIN frequently for data transfer operations. When
this error is returned, user should progress the corresponding endpoint completion queues
by calling fi_cq_read().

Users are encouraged to set the endpoint receive size attribute based on the expected
numbfer of inflight untagged and tagged RDMA operations. For users which are relying on the
provider default value (e.g. MPI), the FI_CXI_DEFAULT_RX_SIZE environment variable can be
used to override the provider default value.

### Endpoint Transmit Size Attribute

The CXI provider uses the endpoint transmit size attribute to size internal command
and hardware event queues. Failing to size the either command queue correctly can result
in the CXI provider returning -FI_EAGAIN frequently for data transfer operations. When
this error is returned, user should progress the corresponding endpoint completion queues
by calling fi_cq_read().

At a minimum, users are encouraged to set the endpoint transmit size attribute based on
the expected numbfer of inflight, initiator RDMA operations. If users are going to be
issuing message opeartions over the CXI provider rendezvous limit (FI_CXI_RDZV_THRESHOLD),
the transmit size attribute must also include the number of outstanding, unexpected
rendezvous operations (i.e. inflight, initiator RDMA operations + outstanding, unexpected
rendezvous operations).

For users which are relying on the provider default value (e.g. MPI), the
FI_CXI_DEFAULT_TX_SIZE environment variable can be used to override the provider default
value.

### FI_UNIVERSE_SIZE Environment Variable

The libfabric FI_UNIVERSE_SIZE environment variable defines the number of expected ranks/peers
an application needs to communicate with. The CXI provider may use this environment variable
to size resources tied to number of peers. Users are encourage to set this environment
variable accordingly.

## Selecting Proper Receive Match Mode

As mentioned in the *Runtime Parameters* section, the CXI provider supports 3 different
operational modes: hardware, hybrid, and software.

Hardware match mode is approriate for users who can ensure the sum of unexpected messages
and posted receives does not exceed the configured hardware receive resource limit for the
application. When resources are consumed, the endpoint will transition into a flow control
operational mode which requires side-band messaging to recover from. Recovery will involve
the CXI provider trying to reclaim hardware receive resources to help prevent future
transition into flow control. If the CXI provider is unable to reclaim hardware receive
resoures, this can lead to a cycle of entering and exiting flow control which may present
itself as a hang to the libfabric user. Running with FI_LOG_LEVEL=warn and FI_LOG_PROV=cxi
will report if this flow control transition is happening.

Hybrid match mode is approriate for users who are unsure if the sum of unexpected messages
and posted receives will not exceed the configure hardware receive resource limit for the
application but want to ensure they application still functions if hardware receive resources
are consumed. Hybrid match mode extends hardware match by allowing for an automated
transition into software match mode if resources are consumed.

Sofftware match mode is approriate for user who know the sum of unexpected messages
and posted receives will exceed the configured hardware receive resource limit for the
application. In software match mode, the CXI provider maintains the a software unexpected and
posted receive list rather than offloading to hardware. This avoids having to allocated a
hardware receive resource for each unxpected messsage and posted receive.

*Note*: In practice, dependent processes (e.g. parallel job) will most likely be sharing a
recieve hardware resource pool.

*Note*: Each match mode may still enter flow control. For example, if a user is not draining
the libfabric completion queue at a reasonable rate, corresponding hardware events may fill
up which will trigger flow control.

## Using Hybrid Match Mode Preemptive Options

The high-level objective of the hybrid match mode preemptive environment variables (i.e.
FI_CXI_HYBRID_PREEMPTIVE, FI_CXI_HYBRID_RECV_PREEMPTIVE,
FI_CXI_HYBRID_POSTED_RECV_PREEMPTIVE, and FI_CXI_HYBRID_UNEXPECTED_MSG_PREEMPTIVE) is to
ensure a process requiring more hardware receives resource does not force other process
requiring less hardware receive resource to be force into software match mode due to no
available hardware receive resources available.

For example, considered a parallel application which has multiple processes (i.e. ranks)
per NIC all sharing the same hardware receive resource pool. Suppose that the application
communication pattern results in an all-to-one communication to only a single rank (e.g.
rank 0) while other ranks may be doing communication amongst each other. If the width of
the all-to-one exceeds hardware resource consumptions, all ranks on the target NIC will
transition to software match mode. The preemptive options may help ensure that only
rank 0 would transition to software match mode instead of all the ranks on the target NIC.

The FI_CXI_HYBRID_POSTED_RECV_PREEMPTIVE and FI_CXI_HYBRID_UNEXPECTED_MSG_PREEMPTIVE
environment variables will force the transition to software match mode if the user
requested endpoint recieve size attribute is exceeded. The benefit of running with
these enabled is that software match mode transition is 100% in control of the libfabric
user through the receive size attribute. One approach users could take here is set
receive size attribute to expected usage, and if this expected usage is exceeded, only
the offending endpoints will transition to software match mode.

FI_CXI_HYBRID_PREEMPTIVE and FI_CXI_HYBRID_RECV_PREEMPTIVE environment variables will
force the transition to software match mode if hardware receive resources in the pool
are running low. The CXI provider will do a multi-step process to transition the libfabric
endpoint to software match mode. The benefit of running with these enabled is that the
number of endpoints transitioning to software match mode may be smaller when compared to
forced software match mode transition due to zero hardware resources available.

## Preventing Messaging Flow Control Due to Hardware Event Queue Sizing

As much as possible, CXI provider message flow control should be avoided. Flow control
results in expensive, side-band, CXI provider internal messaging to recover from. One
cause for flow control is due to improper hardware event queue sizing. If the hardware
event queue is undersized resulting it filling quicker than expected, the next incoming
message operation targeting a full event queue will result in the message operation
being dropped and flow control triggered.

The default CXI provider behavior is to size hardware event queues based on endpoint
transmit and receive size attributes. Thus, it is critical for users to set these
attributes accordingly.

The CQ size can be used to override the CXI provider calcuatled hardware event queue
size based on endpoint transmit and receive size attributes. If the CQ size is greater
than the CXI proviuder calcuation, the value from the CQ size will be used.

The CQ fill percent can be used to define a threshold for when no new RDMA operations
can be queued until the libfabric CQ a progressed thus draining hardware event queues.

## Interrupting CXI Provider CQ Error Event Errno

The following are the libfabric errno value which may be returned in an RDMA CQ error event.

FI_ETRUNC: Receive message truncation.

FI_EHOSTUNREACH: Target is unreachable. This is due to connectivity issues, such as downed
links, between the two peers.

FI_ENOTCONN: Cannot communicate due to no libfabric endpoint configure. In this case, the
target NIC is reachable.

FI_EIO: Catch all errno.

# SEE ALSO

[`fabric`(7)](fabric.7.html),
[`fi_provider`(7)](fi_provider.7.html),
