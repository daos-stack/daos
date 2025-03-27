---
layout: page
title: fi_tcp(7)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_tcp \- Provider that runs over TCP/IP

# OVERVIEW

The tcp provider is usable on all operation systems supported by libfabric.
It runs over TCP (SOCK_STREAM) sockets, and includes the ability
to leverage operation specific features, such as support for zero-copy
and io_uring.  The provider implements a custom protocol over TCP/IP needed to
support the libfabric communication APIs.

# SUPPORTED FEATURES

The following features are supported

*Endpoint types*
: *FI_EP_MSG*
: *FI_EP_RDM*

*Endpoint capabilities*
: *FI_MSG*, *FI_RMA*, *FI_TAGGED*, *FI_RMA_PMEM*, *FI_RMA_EVENT*,
  *FI_MULTI_RECV*, *FI_DIRECTED_RECV*

*Shared Rx Context*
: The tcp provider supports shared receive context

# RUNTIME PARAMETERS

The tcp provider may be configured using several environment variables.  A
subset of supported variables is defined below.  For a full list, use
the fi_info utility application.  For example, 'fi_info -g tcp' will
show all environment variables defined for the tcp provider.

*FI_TCP_IFACE*
: A specific network interface can be requested with this variable

*FI_TCP_PORT_LOW_RANGE/FI_TCP_PORT_HIGH_RANGE*
: These variables are used to set the range of ports to be used by the
  tcp provider for its passive endpoint creation. This is useful where
  only a range of ports are allowed by firewall for tcp connections.

*FI_TCP_TX_SIZE*
: Transmit context size.  This is the number of transmit requests that
  an application may post to the provider before receiving -FI_EAGAIN.
  Default: 256 for msg endpoints, 64k for rdm.

*FI_TCP_RX_SIZE*
: Receive context size.  This is the number of receive buffers that
  the application may post to the provider.  Default: 256 for msg
  endpoints, 64k for rdm.

*FI_TCP_MAX_INJECT*
: Maximum size of inject messages and the maximum size of an unexpected
  message that may be buffered at the receiver.  Default 128 bytes.

*FI_TCP_STAGING_SBUF_SIZE*
: Size of buffer used to coalesce iovec's or send requests before posting
  to the kernel.  The staging buffer is used when the socket is busy and
  cannot accept new data.  In that case, the data can be queued in the
  staging buffer until the socket resumes sending.  This optimizes transfering
  a series of back-to-back small messages to the same target.  Default: 9000
  bytes.  Set to 0 to disable.

*FI_TCP_PREFETCH_RBUF_SIZE*
: Size of the buffer used to prefetch received data from the kernel.
  When starting to receive a new message, the provider will request that
  the kernel fill the prefetch buffer and process received data from there.
  This reduces the number of kernel calls needed to receive a series of
  small messages.  Default: 9000 bytes.  Set to 0 to disable.

*FI_TCP_ZEROCOPY_SIZE*
: Lower threshold where zero copy transfers will be used, if supported by
  the platform, set to -1 to disable.  Default: disabled.

*FI_TCP_TRACE_MSG*
: If enabled, will log transport message information on all sent and
  received messages.  Must be paired with FI_LOG_LEVEL=trace to
  print the message details.

*FI_TCP_IO_URING*
: Uses io_uring for socket operations if available, rather than going
  through the standard socket APIs (i.e. connect, accept, send, recv).
  Default: disabled.

# NOTES

The tcp provider supports both msg and rdm endpoints directly.  Support
for rdm endpoints is available starting at libfabric version v1.18.0, and
comes from the merge back of the net provider found in libfabric versions
v1.16 and v1.17.  For compatibility with older libfabric versions, the tcp
provider may be paired with the ofi_rxm provider as an alternative solution
for rdm endpoint support.  It is recommended that applications that do not
need wire compatibility with older versions of libfabric use the rdm
endpoint support directly from the tcp provider.  This will provide the
best performance.

# SEE ALSO

[`fabric`(7)](fabric.7.html),
[`fi_provider`(7)](fi_provider.7.html),
[`fi_getinfo`(3)](fi_getinfo.3.html)
