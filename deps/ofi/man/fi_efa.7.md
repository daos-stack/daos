---
layout: page
title: fi_efa(7)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_efa \- The Amazon Elastic Fabric Adapter (EFA) Provider

# OVERVIEW

The EFA provider supports the Elastic Fabric Adapter (EFA) device on
Amazon EC2.  EFA provides reliable and unreliable datagram send/receive
with direct hardware access from userspace (OS bypass).

# SUPPORTED FEATURES

The following features are supported:

*Endpoint types*
: The provider supports endpoint type *FI_EP_DGRAM*, and *FI_EP_RDM* on a new
  Scalable (unordered) Reliable Datagram protocol (SRD). SRD provides support
  for reliable datagrams and more complete error handling than typically seen
  with other Reliable Datagram (RD) implementations. The EFA provider provides
  segmentation, reassembly of out-of-order packets to provide send-after-send
  ordering guarantees to applications via its *FI_EP_RDM* endpoint.

*RDM Endpoint capabilities*
: The following data transfer interfaces are supported via the *FI_EP_RDM*
  endpoint: *FI_MSG*, *FI_TAGGED*, and *FI_RMA*. *FI_SEND*, *FI_RECV*,
  *FI_DIRECTED_RECV*, *FI_MULTI_RECV*, and *FI_SOURCE* capabilities are supported.
  The endpoint provides send-after-send guarantees for data operations. The
  *FI_EP_RDM* endpoint does not have a maximum message size.

*DGRAM Endpoint capabilities*
: The DGRAM endpoint only supports *FI_MSG* capability with a maximum
  message size of the MTU of the underlying hardware (approximately 8 KiB).

*Address vectors*
: The provider supports *FI_AV_TABLE* and *FI_AV_MAP* address vector types.
  *FI_EVENT* is unsupported.

*Completion events*
: The provider supports *FI_CQ_FORMAT_CONTEXT*, *FI_CQ_FORMAT_MSG*, and
  *FI_CQ_FORMAT_DATA*. *FI_CQ_FORMAT_TAGGED* is supported on the RDM
  endpoint. Wait objects are not currently supported.

*Modes*
: The provider requires the use of *FI_MSG_PREFIX* when running over
  the DGRAM endpoint, and requires *FI_MR_LOCAL* for all memory
  registrations on the DGRAM endpoint.

*Memory registration modes*
: The RDM endpoint does not require memory registration for send and receive
  operations, i.e. it does not require *FI_MR_LOCAL*. Applications may specify
  *FI_MR_LOCAL* in the MR mode flags in order to use descriptors provided by the
  application. The *FI_EP_DGRAM* endpoint only supports *FI_MR_LOCAL*.

*Progress*
: RDM and DGRAM endpoints support *FI_PROGRESS_MANUAL*.
  EFA erroneously claims the support for *FI_PROGRESS_AUTO*, despite not properly
  supporting automatic progress. Unfortunately, some Libfabric consumers also ask
  for *FI_PROGRESS_AUTO* when they only require *FI_PROGRESS_MANUAL*, and fixing
  this bug would break those applications. This will be fixed in a future version
  of the EFA provider by adding proper support for *FI_PROGRESS_AUTO*.

*Threading*
: The RDM endpoint supports *FI_THREAD_SAFE*, the DGRAM endpoint supports
  *FI_THREAD_DOMAIN*, i.e. the provider is not thread safe when using the DGRAM
  endpoint.

# LIMITATIONS

The DGRAM endpoint does not support *FI_ATOMIC* interfaces. For RMA operations,
completion events for RMA targets (*FI_RMA_EVENT*) is not supported. The DGRAM
endpoint does not fully protect against resource overruns, so resource
management is disabled for this endpoint (*FI_RM_DISABLED*).

No support for selective completions.

No support for counters for the DGRAM endpoint.

No support for inject.

When using FI_HMEM for AWS Neuron or Habana SynapseAI buffers, the provider
requires peer to peer transaction support between the EFA and the FI_HMEM
device. Therefore, the FI_HMEM_P2P_DISABLED option is not supported by the EFA
provider for AWS Neuron or Habana SynapseAI.

# PROVIDER SPECIFIC ENDPOINT LEVEL OPTION

*FI_OPT_EFA_RNR_RETRY*
: Defines the number of RNR retry. The application can use it to reset RNR retry
  counter via the call to fi_setopt. Note that this option must be set before
  the endpoint is enabled. Otherwise, the call will fail. Also note that this
  option only applies to RDM endpoint.

*FI_OPT_EFA_EMULATED_READ, FI_OPT_EFA_EMULATED_WRITE, FI_OPT_EFA_EMULATED_ATOMICS - bool*
: These options only apply to the fi_getopt() call.
  They are used to query the EFA provider to determine if the endpoint is
  emulating Read, Write, and Atomic operations (return value is true), or if
  these operations are assisted by hardware support (return value is false).

*FI_OPT_EFA_USE_DEVICE_RDMA - bool*
: Only available if the application selects a libfabric API version >= 1.18.
  This option allows an application to change libfabric's behavior
  with respect to RDMA transfers.  Note that there is also an environment
  variable FI_EFA_USE_DEVICE_RDMA which the user may set as well.  If the
  environment variable and the argument provided with this variable are in
  conflict, then fi_setopt will return -FI_EINVAL, and the environment variable
  will be respected.  If the hardware does not support RDMA and the argument
  is true, then fi_setopt will return -FI_EOPNOTSUPP.  If the application uses
  API version < 1.18, the argument is ignored and fi_setopt returns
  -FI_ENOPROTOOPT.
  The default behavior for RDMA transfers depends on API version.  For
  API >= 1.18 RDMA is enabled by default on any hardware which supports it.
  For API<1.18, RDMA is enabled by default only on certain newer hardware
  revisions.

*FI_OPT_EFA_SENDRECV_IN_ORDER_ALIGNED_128_BYTES - bool*
: It is used to force the endpoint to use in-order send/recv operation for each 128 bytes
  aligned block. Enabling the option will guarantee data inside each 128 bytes
  aligned block being sent and received in order, it will also guarantee data
  to be delivered to the receive buffer only once. If endpoint is not able to
  support this feature, it will return -FI_EOPNOTSUPP for the call to fi_setopt().


*FI_OPT_EFA_WRITE_IN_ORDER_ALIGNED_128_BYTES - bool*
: It is used to set the endpoint to use in-order RDMA write operation for each 128 bytes
  aligned block. Enabling the option will guarantee data inside each 128 bytes
  aligned block being written in order, it will also guarantee data to be
  delivered to the target buffer only once. If endpoint is not able to support
  this feature, it will return -FI_EOPNOTSUPP for the call to fi_setopt().

# PROVIDER SPECIFIC DOMAIN OPS
The efa provider exports extensions for operations
that are not provided by the standard libfabric interface. These extensions
are available via the "`fi_ext_efa.h`" header file.

## Domain Operation Extension

Domain operation extension is obtained by calling `fi_open_ops`
(see [`fi_domain(3)`](fi_domain.3.html))
```c
int fi_open_ops(struct fid *domain, const char *name, uint64_t flags,
    void **ops, void *context);
```
and requesting `FI_EFA_DOMAIN_OPS` in `name`. `fi_open_ops` returns `ops` as
the pointer to the function table `fi_efa_ops_domain` defined as follows:

```c
struct fi_efa_ops_domain {
	int (*query_mr)(struct fid_mr *mr, struct fi_efa_mr_attr *mr_attr);
};
```

It contains the following operations

### query_mr
This op query an existing memory registration as input, and outputs the efa
specific mr attribute which is defined as follows

```c
struct fi_efa_mr_attr {
    uint16_t ic_id_validity;
    uint16_t recv_ic_id;
    uint16_t rdma_read_ic_id;
    uint16_t rdma_recv_ic_id;
};
```

*ic_id_validity*
:	Validity mask of interconnect id fields. Currently the following bits are supported in the mask:

	FI_EFA_MR_ATTR_RECV_IC_ID:
		recv_ic_id has a valid value.

	FI_EFA_MR_ATTR_RDMA_READ_IC_ID:
		rdma_read_ic_id has a valid value.

	FI_EFA_MR_ATTR_RDMA_RECV_IC_ID:
		rdma_recv_ic_id has a valid value.

*recv_ic_id*
:	Physical interconnect used by the device to reach the MR for receive operation. It is only valid when `ic_id_validity` has the `FI_EFA_MR_ATTR_RECV_IC_ID` bit.

*rdma_read_ic_id*
:	Physical interconnect used by the device to reach the MR for RDMA read operation. It is only valid when `ic_id_validity` has the `FI_EFA_MR_ATTR_RDMA_READ_IC_ID` bit.

*rdma_recv_ic_id*
:	Physical interconnect used by the device to reach the MR for RDMA write receive. It is only valid when `ic_id_validity` has the `FI_EFA_MR_ATTR_RDMA_RECV_IC_ID` bit.

#### Return value
**query_mr()** returns 0 on success, or the value of errno on failure
(which indicates the failure reason).


# RUNTIME PARAMETERS

*FI_EFA_TX_SIZE*
: Maximum number of transmit operations before the provider returns -FI_EAGAIN.
  For only the RDM endpoint, this parameter will cause transmit operations to
  be queued when this value is set higher than the default and the transmit queue
  is full.

*FI_EFA_RX_SIZE*
: Maximum number of receive operations before the provider returns -FI_EAGAIN.

# RUNTIME PARAMETERS SPECIFIC TO RDM ENDPOINT

These OFI runtime parameters apply only to the RDM endpoint.

*FI_EFA_RX_WINDOW_SIZE*
: Maximum number of MTU-sized messages that can be in flight from any
  single endpoint as part of long message data transfer.

*FI_EFA_TX_QUEUE_SIZE*
: Depth of transmit queue opened with the NIC. This may not be set to a value
  greater than what the NIC supports.

*FI_EFA_RECVWIN_SIZE*
: Size of out of order reorder buffer (in messages).  Messages
  received out of this window will result in an error.

*FI_EFA_CQ_SIZE*
: Size of any cq created, in number of entries.

*FI_EFA_MR_CACHE_ENABLE*
: Enables using the mr cache and in-line registration instead of a bounce
  buffer for iov's larger than max_memcpy_size. Defaults to true. When
  disabled, only uses a bounce buffer

*FI_EFA_MR_MAX_CACHED_COUNT*
: Sets the maximum number of memory registrations that can be cached at
  any time.

*FI_EFA_MR_MAX_CACHED_SIZE*
: Sets the maximum amount of memory that cached memory registrations can
  hold onto at any time.

*FI_EFA_MAX_MEMCPY_SIZE*
: Threshold size switch between using memory copy into a pre-registered
  bounce buffer and memory registration on the user buffer.

*FI_EFA_MTU_SIZE*
: Overrides the default MTU size of the device.

*FI_EFA_RX_COPY_UNEXP*
: Enables the use of a separate pool of bounce-buffers to copy unexpected
  messages out of the pre-posted receive buffers.

*FI_EFA_RX_COPY_OOO*
: Enables the use of a separate pool of bounce-buffers to copy out-of-order RTS
  packets out of the pre-posted receive buffers.

*FI_EFA_MAX_TIMEOUT*
: Maximum timeout (us) for backoff to a peer after a receiver not ready error.

*FI_EFA_TIMEOUT_INTERVAL*
: Time interval (us) for the base timeout to use for exponential backoff
  to a peer after a receiver not ready error.

*FI_EFA_ENABLE_SHM_TRANSFER*
: Enable SHM provider to provide the communication across all intra-node processes.
  SHM transfer will be disabled in the case where
  [`ptrace protection`](https://wiki.ubuntu.com/SecurityTeam/Roadmap/KernelHardening#ptrace_Protection)
  is turned on. You can turn it off to enable shm transfer.

  FI_EFA_ENABLE_SHM_TRANSFER is parsed during the fi_domain call and is related to the FI_OPT_SHARED_MEMORY_PERMITTED endpoint option.
  If FI_EFA_ENABLE_SHM_TRANSFER is set to true, the FI_OPT_SHARED_MEMORY_PERMITTED endpoint
  option overrides FI_EFA_ENABLE_SHM_TRANSFER. If FI_EFA_ENABLE_SHM_TRANSFER is set to false,
  but the FI_OPT_SHARED_MEMORY_PERMITTED is set to true, the FI_OPT_SHARED_MEMORY_PERMITTED
  setopt call will fail with -FI_EINVAL.

*FI_EFA_SHM_AV_SIZE*
: Defines the maximum number of entries in SHM provider's address vector.

*FI_EFA_SHM_MAX_MEDIUM_SIZE*
: Defines the switch point between small/medium message and large message. The message
  larger than this switch point will be transferred with large message protocol.
  NOTE: This parameter is now deprecated.

*FI_EFA_INTER_MAX_MEDIUM_MESSAGE_SIZE*
: The maximum size for inter EFA messages to be sent by using medium message protocol. Messages which can fit in one packet will be sent as eager message. Messages whose sizes are smaller than this value will be sent using medium message protocol. Other messages will be sent using CTS based long message protocol.

*FI_EFA_FORK_SAFE*
: Enable fork() support. This may have a small performance impact and should only be set when required. Applications that require to register regions backed by huge pages and also require fork support are not supported.

*FI_EFA_RUNT_SIZE*
: The maximum number of bytes that will be eagerly sent by inflight messages uses runting read message protocol (Default 307200).

*FI_EFA_INTER_MIN_READ_MESSAGE_SIZE*
: The minimum message size in bytes for inter EFA read message protocol. If instance support RDMA read, messages whose size is larger than this value will be sent by read message protocol. (Default 1048576).

*FI_EFA_INTER_MIN_READ_WRITE_SIZE*
: The mimimum message size for emulated inter EFA write to use read write protocol. If firmware support RDMA read, and FI_EFA_USE_DEVICE_RDMA is 1, write requests whose size is larger than this value will use the read write protocol (Default 65536). If the firmware supports RDMA write, device RDMA write will always be used.

*FI_EFA_USE_DEVICE_RDMA*
: Specify whether to require or ignore RDMA features of the EFA device.
- When set to 1/true/yes/on, all RDMA features of the EFA device are used. But if EFA device does not support RDMA and FI_EFA_USE_DEVICE_RDMA is set to 1/true/yes/on, user's application is aborted and a warning message is printed.
- When set to 0/false/no/off, libfabric will emulate all fi_rma operations instead of offloading them to the EFA network device. Libfabric will not use device RDMA to implement send/receive operations.
- If not set, RDMA operations will occur when available based on RDMA device ID/version.

*FI_EFA_USE_HUGE_PAGE*
: Specify Whether EFA provider can use huge page memory for internal buffer.
Using huge page memory has a small performance advantage, but can
cause system to run out of huge page memory. By default, EFA provider
will use huge page unless FI_EFA_FORK_SAFE is set to 1/on/true.

*FI_EFA_USE_ZCPY_RX*
: Enables the use of application's receive buffers in place of bounce-buffers when feasible.
(Default: 1). Setting this environment variable to 0 can disable this feature.
Explicitly setting this variable to 1 does not guarantee this feature
due to other requirements. See
https://github.com/ofiwg/libfabric/blob/main/prov/efa/docs/efa_rdm_protocol_v4.md
for details.

# SEE ALSO

[`fabric`(7)](fabric.7.html),
[`fi_provider`(7)](fi_provider.7.html),
[`fi_getinfo`(3)](fi_getinfo.3.html)
