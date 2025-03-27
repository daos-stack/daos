---
layout: page
title: fi_provider(7)
tagline: Libfabric Programmer's Manual
---
{% include JB/setup %}

# NAME

fi_provider \- Fabric Interface Providers

# OVERVIEW

See [`fi_arch`(7)](fi_arch.7.html) for a brief description of how
providers fit into the libfabric architecture.

Conceptually, a fabric provider implements and maps the libfabric
API over lower-level network software and/or hardware.  Most application
calls into the libfabric API go directly into a provider's implementation
of that API.

Libfabric providers are grouped into different type: core, utility,
hooking, and offload.  These are describe in more detail below.  The
following diagram illustrates the architecture between the provider
types.

```
---------------------------- libfabric API ---------------------------- 
  [core]   provider|<- [hooking provider]
[services]   API   |  --- libfabric API --- 
                   |<- [utility provider]
                   |  ---------------- libfabric API ------------------ 
                   |<-  [core provider] <-peer API-> [offload provider]

```
All providers plug into libfabric using an exported provider API.  libfabric
supports both internal providers, which ship with the library for user
convenience, as well as external providers.  External provider libraries
must be in the library search path, end with the suffix "-fi", and export
the function fi_prov_ini().

Once registered with the libfabric core, a provider will be reported to
applications as part of the discovery process.  Hooking and utility providers
will intercept libfabric calls from the application to perform some task
before calling through to the next provider.  If there's no need to intercept
a specific API call, the application will call directly to the core provider.
Where possible provider to provider communication is done using the libfabric
APIs itself, including the use of provider specific extensions to reduce
call overhead.

libfabric defines a set of APIs that specifically target providers that may
be used as peers.  These APIs are oddly enough called peer APIs.  Peer APIs
are technically part of the external libfabric API, but are not designed for
direct use by applications and are not considered stable for API backwards
compatibility.

# Core Providers

Core providers are stand-alone providers that usually target a specific
class of networking devices.  That is, a specific NIC, class of network
hardware, or lower-level software interface.  The core providers
are usually what most application developers are concerned with.  Core
providers may only support libfabric features and interfaces that map
efficiently to the underlying hardware or network protocols.

The following core providers are built into libfabric by default, assuming
all build pre-requisites are met.  That is, necessary libraries are installed,
operating system support is available, etc.  This list is not exhaustive.

*CXI*
: Provider for Cray's Slingshot network. See
  [`fi_cxi`(7)](fi_cxi.7.html) for more information.

*EFA*
: A provider for the [Amazon EC2 Elastic Fabric Adapter
  (EFA)](https://aws.amazon.com/hpc/efa/), a custom-built OS bypass
  hardware interface for inter-instance communication on EC2.
  See [`fi_efa`(7)](fi_efa.7.html) for more information.

*OPX*
: Supports Omni-Path networking from Cornelis Networks.  See
  [`fi_opx`(7)](fi_opx.7.html) for more information.

*PSM2*
: Older provider for Omni-Path networks.  See
  [`fi_psm2`(7)](fi_psm2.7.html) for more information.

*PSM3*
: Provider for Ethernet networking from Intel.  See
  [`fi_psm3`(7)](fi_psm3.7.html) for more information.

*SHM*
: A provider for intra-node communication using shared memory.
  See [`fi_shm`(7)](fi_shm.7.html) for more information.

*TCP*
: A provider which runs over the TCP/IP protocol and is available on
  multiple operating systems.  This provider enables develop of libfabric
  applications on most platforms.
  See [`fi_tcp`(7)](fi_tcp.7.html) for more information.

*UCX*
: A provider which runs over the UCX library which is currently supported
  by Infiniband fabrics from NVIDIA.
  See [`fi_ucx`(7)](fi_ucx.7.html) for more information.

*UDP*
: A provider which runs over the UDP/IP protocol and is available on
  multiple operating systems.  This provider enables develop of libfabric
  applications on most platforms.
  See [`fi_udp`(7)](fi_udp.7.html) for more information.

*Verbs*
: This provider targets RDMA NICs for both Linux and Windows platforms.
  See [`fi_verbs`(7)](fi_verbs.7.html) for more information.

# Utility Providers

Utility providers are named with a starting prefix of "ofi_".
Utility providers are distinct from core providers in that they are not
associated with specific classes of devices.  They instead work with
core providers to expand their features and interact with core providers
through libfabric interfaces internally.  Utility providers are used
to support a specific endpoint type over a simpler endpoint type.

Utility providers show up as part of the return's provider's name.
See [`fi_fabric`(3)](fi_fabric.3.html).  Utility providers are
enabled automatically for core providers that do not support the feature
set requested by an application.

*RxM*
: Implements RDM endpoint semantics over MSG endpoints.
  See [`fi_rxm`(7)](fi_rxm.7.html) for more information.

*RxD*
: Implements RDM endpoint semantis over DGRAM endpoints.
  See [`fi_rxd`(7)](fi_rxd.7.html) for more information.

# Hooking Providers

Hooking providers are mostly used for debugging purposes.  Since
hooking providers are built and included in release versions of
libfabric, they are always available and have no impact on performance
unless enabled.  Hooking providers can layer over all other providers
and intercept, or hook, their calls in order to perform some dedicated
task, such as gathering performance data on call paths or providing
debug output.

See [`fi_hook`(7)](fi_hook.7.html) for more information.

# Offload Providers

Offload providers start with the naming prefix "off_".  An offload provider
is meant to be paired with other core and/or utility providers.
An offload provider is intended to accelerate specific types of communication,
generally by taking advantage of network services that have been offloaded
into hardware, though actual hardware offload support is not a requirement.

# SEE ALSO

[`fabric`(7)](fabric.7.html)
[`fi_provider`(3)](fi_provider.3.html)
