---
layout: page
title: fi_shm(7)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_shm \- The SHM Fabric Provider

# OVERVIEW

The SHM provider is a complete provider that can be used on Linux
systems supporting shared memory and process_vm_readv/process_vm_writev
calls.  The provider is intended to provide high-performance communication
between processes on the same system.

# SUPPORTED FEATURES

This release contains an initial implementation of the SHM provider that
offers the following support:

*Endpoint types*
: The provider supports only endpoint type *FI_EP_RDM*.

*Endpoint capabilities*
: Endpoints cna support any combinations of the following data transfer
capabilities: *FI_MSG*, *FI_TAGGED*, *FI_RMA*, amd *FI_ATOMICS*.  These
capabilities can be further defined by *FI_SEND*, *FI_RECV*, *FI_READ*,
*FI_WRITE*, *FI_REMOTE_READ*, and *FI_REMOTE_WRITE* to limit the direction
of operations.

*Modes*
: The provider does not require the use of any mode bits.

*Progress*
: The SHM provider supports *FI_PROGRESS_MANUAL*.  Receive side data buffers are
  not modified outside of completion processing routines.  The provider processes
  messages using three different methods, based on the size of the message.
  For messages smaller than 4096 bytes, tx completions are generated immediately
  after the send.  For larger messages, tx completions are not generated until
  the receiving side has processed the message.

*Address Format*
: The SHM provider uses the address format FI_ADDR_STR, which follows the general
  format pattern "[prefix]://[addr]".  The application can provide addresses
  through the node or hints parameter.  As long as the address is in a valid
  FI_ADDR_STR format (contains "://"), the address will be used as is.  If the
  application input is incorrectly formatted or no input was provided, the SHM
  provider will resolve it according to the following SHM provider standards:

  (flags & FI_SOURCE) ? src_addr : dest_addr =
   - if (node && service) : "fi_ns://node:service"
   - if (service) : "fi_ns://service"
   - if (node && !service) : "fi_shm://node"
   - if (!node && !service) : "fi_shm://PID"

   !(flags & FI_SOURCE)
   - src_addr = "fi_shm://PID"

  In other words, if the application provides a source and/or destination
  address in an acceptable FI_ADDR_STR format (contains "://"), the call
  to util_getinfo will successfully fill in src_addr and dest_addr with
  the provided input.  If the input is not in an ADDR_STR format, the
  shared memory provider will then create a proper FI_ADDR_STR address
  with either the "fi_ns://" (node/service format) or "fi_shm://" (shm format)
  prefixes signaling whether the addr is a "unique" address and does or does
  not need an extra endpoint name identifier appended in order to make it
  unique.  For the shared memory provider, we assume that the service
  (with or without a node) is enough to make it unique, but a node alone is
  not sufficient.  If only a node is provided, the "fi_shm://" prefix  is used
  to signify that it is not a unique address.  If no node or service are
  provided (and in the case of setting the src address without FI_SOURCE and
  no hints), the process ID will be used as a default address.
  On endpoint creation, if the src_addr has the "fi_shm://" prefix, the provider
  will append ":[uid]:[ep_idx]" as a unique endpoint name (essentially,
  in place of a service).  In the case of the "fi_ns://" prefix (or any other
  prefix if one was provided by the application), no supplemental information
  is required to make it unique and it will remain with only the
  application-defined address.  Note that the actual endpoint name will not
  include the FI_ADDR_STR "*://" prefix since it cannot be included in any
  shared memory region names. The provider will strip off the prefix before
  setting the endpoint name. As a result, the addresses
  "fi_prefix1://my_node:my_service" and "fi_prefix2://my_node:my_service"
  would result in endpoints and regions of the same name.
  The application can also override the endpoint name after creating an
  endpoint using setname() without any address format restrictions.

*Msg flags*
  The provider currently only supports the FI_REMOTE_CQ_DATA msg flag.

*MR registration mode*
  The provider implements FI_MR_VIRT_ADDR memory mode.

*Atomic operations*
  The provider supports all combinations of datatype and operations as long
  as the message is less than 4096 bytes (or 2048 for compare operations).

# DSA
Intel Data Streaming Accelerator (DSA) is an integrated accelerator in Intel
Xeon processors starting with Sapphire Rapids generation. One of the
capabilities of DSA is to offload memory copy operations from the CPU.  A
system may have one or more DSA devices. Each DSA device may have one or more
work queues. The DSA specification can be found
[here](https://www.intel.com/content/www/us/en/develop/articles/intel-data-streaming-accelerator-architecture-specification.html).

The SAR protocol of SHM provider is enabled to take advantage of DSA to offload
memory copy operations into and out of SAR buffers in shared memory regions. To
fully take advantage of the DSA offload capability, memory copy operations are
performed asynchronously. Copy initiator thread constructs the DSA commands and
submits to work queues. A copy operation may consists of more than one DSA
commands. In such case, commands are spread across all available work queues in
round robin fashion. The progress thread checks for DSA command completions. If
the copy command successfully completes, it then notifies the peer to consume
the data. If DSA encountered a page fault during command execution, the page
fault is reported via completion records. In such case, the progress thread
accesses the page to resolve the page fault and resubmits the command after
adjusting for partial completions. One of the benefits of making memory copy
operations asynchronous is that now data transfers between different target
endpoints can be initiated in parallel. Use of Intel DSA in SAR protocol is
disabled by default and can be enabled using an environment variable. Note that
CMA must be disabled, e.g. FI_SHM_DISABLE_CMA=0, in order for DSA to be used.
See the RUNTIME PARAMETERS section.

Compiling with DSA capabilities depends on the accel-config library which can
be found [here](https://github.com/intel/idxd-config). Running with DSA
requires using Linux Kernel 5.19.0-rc3 or later.

DSA devices need to be setup just once before runtime.  [This configuration
file](https://github.com/intel/idxd-config/blob/stable/contrib/configs/os_profile.conf)
can be used as a template with accel-config utility to configure the DSA
devices.

# LIMITATIONS

The SHM provider has hard-coded maximums for supported queue sizes and data
transfers.  These values are reflected in the related fabric attribute
structures

EPs must be bound to both RX and TX CQs.

No support for counters.

# RUNTIME PARAMETERS

The *shm* provider checks for the following environment variables:

*FI_SHM_SAR_THRESHOLD*
: Maximum message size to use segmentation protocol before switching
  to mmap (only valid when CMA is not available). Default: SIZE_MAX
  (18446744073709551615)

*FI_SHM_TX_SIZE*
: Maximum number of outstanding tx operations. Default 1024

*FI_SHM_RX_SIZE*
: Maximum number of outstanding rx operations. Default 1024

*FI_SHM_DISABLE_CMA*
: Manually disables CMA. Default false

*FI_SHM_USE_DSA_SAR*
: Enables memory copy offload to Intel DSA in SAR protocol. Default false

*FI_SHM_USE_XPMEM*
 : SHM can use SAR, CMA or XPMEM for host memory transfer. If
   FI_SHM_USE_XPMEM is set to 1, the provider will select XPMEM over CMA if
   XPMEM is available.  Otherwise, if neither CMA nor XPMEM are available
   SHM shall default to the SAR protocol. Default 0

*FI_XPMEM_MEMCPY_CHUNKSIZE*
 :  The maximum size which will be used with a single memcpy call. XPMEM
    copy performance improves when buffers are divided into smaller
    chunks. This environment variable is provided to fine tune performance
    on different systems. Default 262144

# SEE ALSO

[`fabric`(7)](fabric.7.html),
[`fi_provider`(7)](fi_provider.7.html),
[`fi_getinfo`(3)](fi_getinfo.3.html)
