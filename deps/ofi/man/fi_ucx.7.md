---
layout: page
title: fi_ucx(7)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_ucx \- The UCX Fabric Provider

# OVERVIEW

The *ucx* provider runs over the UCX library
that is currently supported by the NVIDIA Infiniband fabrics.
The *ucx* provider makes use of UCX tag matching API in order to
implement a limited set of the libfabric data transfer APIs.

Supported UCP API version: 1.0

# LIMITATIONS

The *ucx* provider doesn't support all the features defined in the
libfabric API. Here are some of the limitations:

Endpoint types
: The only supported type is *FI_EP_RDM*.

Endpoint capabilities
: Endpoints support data transfer capabilities *FI_MSG*, *FI_TAGGED*,
  *FI_RMA* and *FI_MULTI_RECV*.

Threading
: The supported threading mode is *FI_THREAD_DOMAIN*, i.e. the *ucx*
  provider is not thread safe.

# RUNTIME PARAMETERS

*FI_UCX_CONFIG*
: The path to the UCX configuration file (default: none).

*FI_UCX_TINJECT_LIMIT*
: Maximal tinject message size (default: 1024).

*FI_UCX_NS_ENABLE*
: Enforce usage of name server functionality for UCX provider
  (default: disabled).

*FI_UCX_NS_PORT*
: UCX provider's name server port (default: 12345).

*FI_UCX_NS_IFACE*
: IPv4 network interface for UCX provider's name server
  (default: any).

*FI_UCX_CHECK_REQ_LEAK*
: Check request leak (default: disabled).

# SEE ALSO

[`fabric`(7)](fabric.7.html),
[`fi_provider`(7)](fi_provider.7.html),
