---
layout: page
title: fi_opx(7)
tagline: Libfabric Programmer's Manual
---
{%include JB/setup %}

# NAME

fi_opx \- The Omni-Path Express Fabric Provider

# OVERVIEW

The *opx* provider is a native libfabric provider suitable for
use with Omni-Path fabrics.  OPX features great scalability and
performance when running libfabric-enabled message layers.
OPX requires 3 additonal external development libraries to build:
libuuid, libnuma, and the Linux kernel headers.


# SUPPORTED FEATURES

The OPX provider supports most features defined for the libfabric API.

Key features include:

Endpoint types
: The Omni-Path HFI hardware is connectionless and reliable.
  The OPX provider only supports the *FI_EP_RDM* endpoint type.

Capabilities
: Supported capabilities include *FI_MSG*, *FI_RMA, *FI_TAGGED*, *FI_ATOMIC*,
  *FI_NAMED_RX_CTX*, *FI_SOURCE*, *FI_SEND*, *FI_RECV*, *FI_MULTI_RECV*,
  *FI_DIRECTED_RECV*, *FI_SOURCE*.

  Notes on *FI_DIRECTED_RECV* capability: The immediate data which is sent
  within the "senddata" call to support *FI_DIRECTED_RECV* for OPX
  must be exactly 4 bytes, which OPX uses to completely identify the
  source address to an exascale\-level number of ranks for tag matching on
  the recv and can be managed within the MU packet.
  Therefore the domain attribute "cq_data_size" is set to 4 which is the OFI
  standard minimum.

Modes
: Two modes are defined: *FI_CONTEXT2* and *FI_ASYNC_IOV*.
  The OPX provider requires *FI_CONTEXT2*.

Additional features
: Supported additional features include *FABRIC_DIRECT*, *scalable endpoints*,
  and *counters*.

Progress
: *FI_PROGRESS_MANUAL* and *FI_PROGRESS_AUTO* are supported, for best performance, use
  *FI_PROGRESS_MANUAL* when possible. *FI_PROGRESS_AUTO* will spawn 1 thread per CQ.

Address vector
: *FI_AV_MAP* and *FI_AV_TABLE* are both supported. *FI_AV_MAP* is default.

Memory registration modes
: Only *FI_MR_SCALABLE* is supported.

# UNSUPPORTED FEATURES

Endpoint types
: Unsupported endpoint types include *FI_EP_DGRAM* and *FI_EP_MSG*.

Capabilities
: The OPX provider does not support *FI_RMA_EVENT* and *FI_TRIGGER*
  capabilities.

# LIMITATIONS

OPX supports the following MPI versions:

Intel MPI from Parallel Studio 2020, update 4.
Intel MPI from OneAPI 2021, update 3.
Open MPI 4.1.2a1 (Older version of Open MPI will not work).
MPICH 3.4.2 and later.

Usage:

If using with OpenMPI 4.1.x, disable UCX and openib transports.
OPX is not compatible with Open MPI 4.1.x PML/BTL.

# CONFIGURATION OPTIONS

*OPX_AV*
: OPX supports the option of setting the AV mode to use in a build.
  3 settings are supported:
  - table
  - map
  - runtime

  Using table or map will only allow OPX to use FI_AV_TABLE or FI_AV_MAP.
  Using runtime will allow OPX to use either AV mode depending on what the
  application requests. Specifying map or table however may lead to a slight
  performance improvement depending on the application.

  To change OPX_AV, add OPX_AV=table, OPX_AV=map, or OPX_AV=runtime to the
  configure command. For example, to create a new build with OPX_AV=table:\
  OPX_AV=table ./configure\
  make install\
\
  There is no way to change OPX_AV after it is set. If OPX_AV is not set in
  the configure, the default value is runtime.

# RUNTIME PARAMETERS

*FI_OPX_UUID*
: OPX requires a unique ID for each job. In order for all processes in a
  job to communicate with each other, they require to use the same UUID.
  This variable can be set with FI_OPX_UUID=${RANDOM}
  The default UUID is 00112233445566778899aabbccddeeff.

*FI_OPX_FORCE_CPUAFFINITY*
: Boolean (0/1, on/off, true/false, yes/no). Causes the thread to bind
  itself to the cpu core it is running on. Defaults to "No"

*FI_OPX_RELIABILITY_SERVICE_USEC_MAX*
: Integer. This setting controls how frequently the reliability/replay
  function will issue PING requests to a remote connection. Reducing this
  value may improve performance at the expense of increased traffic on the
  OPX fabric.
  Default setting is 500.

*FI_OPX_RELIABILITY_SERVICE_PRE_ACK_RATE*
: Integer. This setting controls how frequently a receiving rank will send ACKs
  for packets it has received without being prompted through a PING request.
  A non-zero value N tells the receiving rank to send an ACK for the
  last N packets every Nth packet. Used in conjunction with an increased
  value for FI_OPX_RELIABILITY_SERVICE_USEC_MAX may improve performance.

  Valid values are 0 (disabled) and powers of 2 in the range of 1-32,768, inclusive.

  Default setting is 64.

*FI_OPX_SELINUX*
: Boolean (0/1, on/off, true/false, yes/no). Set to true if you're running a
  security-enhanced Linux. This enables updating the Jkey used based on system
  settings. Defaults to "No"

*FI_OPX_HFI_SELECT*
: String. Controls how OPX chooses which HFI to use when opening a context.
  Has two forms:
  - `<hfi-unit>` Force OPX provider to use `hfi-unit`.
  - `<selector1>[,<selector2>[,...,<selectorN>]]` Select HFI based on first matching `selector`

  Where `selector` is one of the following forms:
  - `default` to use the default logic
  - `fixed:<hfi-unit>` to fix to one `hfi-unit`
  - `<selector-type>:<hfi-unit>:<selector-data>`

  The above fields have the following meaning:
  - `selector-type` The selector criteria the caller opening the context is evaluated against.
  - `hfi-unit` The HFI to use if the caller matches the selector.
  - `selector-data` Data the caller must match (e.g. NUMA node ID).

  Where `selector-type` is one of the following:
  - `numa` True when caller is local to the NUMA node ID given by `selector-data`.
  - `core` True when caller is local to the CPU core given by `selector-data`.

  And `selector-data` is one of the following:
  - `value` The specific value to match
  - `<range-start>-<range-end>` Matches with any value in that range

  In the second form, when opening a context, OPX uses the `hfi-unit` of the
  first-matching selector. Selectors are evaluated left-to-right. OPX will
  return an error if the caller does not match any selector.

  In either form, it is an error if the specified or selected HFI is not in the
  Active state. In this case, OPX will return an error and execution will not
  continue.

  With this option, it is possible to cause OPX to try to open more contexts on
  an HFI than there are free contexts on that HFI. In this case, one or more of
  the context-opening calls will fail and OPX will return an error.
  For the second form, as which HFI is selected depends on properties of the
  caller, deterministic HFI selection requires deterministic caller properties.
  E.g.  for the `numa` selector, if the caller can migrate between NUMA domains,
  then HFI selection will not be deterministic.

  The logic used will always be the first valid in a selector list. For example, `default` and
  `fixed` will match all callers, so if either are in the beginning of a selector list, you will
  only use `fixed` or `default` regardles of if there are any more selectors.

  Examples:
  - `FI_OPX_HFI_SELECT=0` all callers will open contexts on HFI 0.
  - `FI_OPX_HFI_SELECT=1` all callers will open contexts on HFI 1.
  - `FI_OPX_HFI_SELECT=numa:0:0,numa:1:1,numa:0:2,numa:1:3` callers local to NUMA nodes 0 and 2 will use HFI 0, callers local to NUMA domains 1 and 3 will use HFI 1.
  - `FI_OPX_HFI_SELECT=numa:0:0-3,default` callers local to NUMA nodes 0 thru 3 (including 0 and 3) will use HFI 0, and all else will use default selection logic.
  - `FI_OPX_HFI_SELECT=core:1:0,fixed:0` callers local to CPU core 0 will use HFI 1, and all others will use HFI 0.
  - `FI_OPX_HFI_SELECT=default,core:1:0` all callers will use default HFI selection logic.

*FI_OPX_DELIVERY_COMPLETION_THRESHOLD*
: Integer. Will be deprecated. Please use FI_OPX_SDMA_BOUNCE_BUF_THRESHOLD.

*FI_OPX_SDMA_BOUNCE_BUF_THRESHOLD*
: Integer. The maximum message length in bytes that will be copied to the SDMA bounce buffer.
  For messages larger than this threshold, the send will not be completed until receiver
  has ACKed. Value must be between 16385 and 2147483646. Defaults to 16385.

*FI_OPX_SDMA_DISABLE*
: Integer. Disables SDMA offload hardware. Default is 0

*FI_OPX_SDMA_MIN_PAYLOAD_BYTES*
: Integer. The minimum length in bytes where SDMA will be used.
  For messages smaller than this threshold, the send will be completed using PIO.
  Value must be between 64 and 2147483646. Defaults to 16385.

*FI_OPX_RZV_MIN_PAYLOAD_BYTES*
: Integer. The minimum length in bytes where rendezvous will be used.
  For messages smaller than this threshold, the send will first try to be completed using eager or multi-packet eager.
  Value must be between 64 and 65536. Defaults to 16385.

*FI_OPX_MP_EAGER_DISABLE*
: Integer. Disables multi-packet eager. Defaults to 0.

*FI_OPX_EXPECTED_RECEIVE_ENABLE*
: Boolean (0/1, on/off, true/false, yes/no). Enables expected receive rendezvous using Token ID (TID).
  Defaults to "No". This feature is not currently supported.

*FI_OPX_PROG_AFFINITY*
: String. This sets the affinity to be used for any progress threads. Set as a colon-separated
  triplet as `start:end:stride`, where stride controls the interval between selected cores.
  For example, `1:5:2` will have cores 1, 3, and 5 as valid cores for progress threads. By default
  no affinity is set.

*FI_OPX_AUTO_PROGRESS_INTERVAL_USEC*
: Integer. This setting controls the time (in usecs) between polls for auto progress threads.
  Default is 1.

*FI_OPX_PKEY*
: Integer. Partition key, a 2 byte positive integer. Default is 0x8001

*FI_OPX_SL*
: Integer. Service Level. This will also determine Service Class and Virtual Lane.  Default is 0

*FI_OPX_DEV_REG_SEND_THRESHOLD*
: Integer. The individual packet threshold where lengths above do not use a device
  registered copy when sending data from GPU.
  The default threshold is 4096.
  This has no meaning if Libfabric was not configured with GDRCopy or ROCR support.

*FI_OPX_DEV_REG_RECV_THRESHOLD*
: Integer. The individual packet threshold where lengths above do not use a device
  registered copy when receiving data into GPU.
  The default threshold is 8192.
  This has no meaning if Libfabric was not configured with GDRCopy or ROCR support.

# SEE ALSO

[`fabric`(7)](fabric.7.html),
[`fi_provider`(7)](fi_provider.7.html),
[`fi_getinfo`(7)](fi_getinfo.7.html),
