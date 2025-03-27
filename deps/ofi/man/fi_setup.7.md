---
layout: page
title: fi_setup(7)
tagline: Libfabric Programmer's Guide - Setup
---
{% include JB/setup %}

# NAME

fi_setup \- libfabric setup and initialization

# OVERVIEW

A full description of the libfabric API is documented in the relevant man pages.
This section provides an introduction to select interfaces, including how they
may be used.  It does not attempt to capture all subtleties or use cases,
nor describe all possible data structures or fields.  However, it is useful
for new developers trying to kick-start using libfabric.

# fi_getinfo()

The fi_getinfo() call is one of the first calls that applications invoke.
It is designed to be easy to use for simple applications, but extensible
enough to configure a network for optimal performance.  It serves several
purposes. First, it abstracts away network implementation and addressing
details.  Second, it allows an application to specify which features they
require of the network.  Last, it provides a mechanism for a provider to
report how an application can use the network in order to achieve the best
performance.  fi_getinfo() is loosely based on the getaddrinfo() call.

```
/* API prototypes */
struct fi_info *fi_allocinfo(void);

int fi_getinfo(int version, const char *node, const char *service,
    uint64_t flags, struct fi_info *hints, struct fi_info **info);
```

```
/* Sample initialization code flow */
struct fi_info *hints, *info;

hints = fi_allocinfo();

/* hints will point to a cleared fi_info structure
 * Initialize hints here to request specific network capabilities
 */

fi_getinfo(FI_VERSION(1, 16), NULL, NULL, 0, hints, &info);
fi_freeinfo(hints);

/* Use the returned info structure to allocate fabric resources */
```

The hints parameter is the key for requesting fabric services.  The
fi_info structure contains several data fields, plus pointers to a wide
variety of attributes.  The fi_allocinfo() call simplifies the creation
of an fi_info structure and is strongly recommended for use.  In this
example, the application is merely attempting to get a list of what
providers are available in the system and the features that they support.
Note that the API is designed to be extensible.  Versioning information is
provided as part of the fi_getinfo() call.  The version is used by libfabric
to determine what API features the application is aware of.  In this case,
the application indicates that it can properly handle any feature that was
defined for the 1.16 release (or earlier).

Applications should _always_ hard code the version that they are written
for into the fi_getinfo() call.  This ensures that newer versions of libfabric
will provide backwards compatibility with that used by the application.
Newer versions of libfabric must support applications that were compiled
against an older version of the library.  It must also support applications
written against header files from an older library version, but re-compiled
against newer header files.  Among other things, the version parameter allows
libfabric to determine if an application is aware of new fields that may have
been added to structures, or if the data in those fields may be uninitialized.

Typically, an application will initialize the hints parameter to list the
features that it will use.

```
/* Taking a peek at the contents of fi_info */
struct fi_info {
    struct fi_info *next;
    uint64_t caps;
    uint64_t mode;
    uint32_t addr_format;
    size_t src_addrlen;
    size_t dest_addrlen;
    void *src_addr;
    void *dest_addr;
    fid_t handle;
    struct fi_tx_attr *tx_attr;
    struct fi_rx_attr *rx_attr;
    struct fi_ep_attr *ep_attr;
    struct fi_domain_attr *domain_attr;
    struct fi_fabric_attr *fabric_attr;
    struct fid_nic *nic;
};
```

The fi_info structure references several different attributes, which correspond
to the different libfabric objects that an application allocates.  For basic
applications, modifying or accessing most attribute fields are unnecessary.
Many applications will only need to deal with a few fields of fi_info, most
notably the endpoint type, capability (caps) bits, and mode bits.  These are
defined in more detail below.

On success, the fi_getinfo() function returns a linked list of fi_info
structures.  Each entry in the list will meet the conditions specified
through the hints parameter.  The returned entries may come from different
network providers, or may differ in the returned attributes.  For example,
if hints does not specify a particular endpoint type, there may be an entry
for each of the three endpoint types.  As a general rule, libfabric attempts
to return the list of fi_info structures in order from most desirable to least.
High-performance network providers are listed before more generic providers.

## Capabilities (fi_info::caps)

The fi_info caps field is used to specify the features and services that the
application requires of the network.  This field is a bit-mask of desired
capabilities.  There are capability bits for each of the data transfer services
previously mentioned: FI_MSG, FI_TAGGED, FI_RMA, FI_ATOMIC, and FI_COLLECTIVE.
Applications should set each bit for each set of operations that it will use.
These bits are often the only caps bits set by an application.

Capabilities are grouped into three general categories: primary, secondary,
and primary modifiers.  Primary capabilities must explicitly be requested
by an application, and a provider must enable support for only those primary
capabilities which were selected.  This is required for both performance and
security reasons.  Primary modifiers are used to limit a primary capability,
such as restricting an endpoint to being send-only.

Secondary capabilities may optionally be requested by an application.  If
requested, a provider must support a capability if it is asked for or fail
the fi_getinfo request.  A provider may optionally report non-requested
secondary capabilities if doing so would not compromise performance or
security.  That is, a provider may grant an application a secondary capability,
whether the application.  The most commonly accessed secondary capability bits
indicate if provider communication is restricted to the local node Ifor example,
the shared memory provider only supports local communication) and/or remote
nodes (which can be the case for NICs that lack loopback support).  Other
secondary capability bits mostly deal with features targeting highly-scalable
applications, but may not be commonly supported across multiple providers.

Because different providers support different sets of capabilities, applications
that desire optimal network performance may need to code for a capability being
either present or absent.  When present, such capabilities can offer a
scalability or performance boost.  When absent, an application may prefer to
adjust its protocol or implementation to work around the network limitations.
Although providers can often emulate features, doing so can impact overall
performance, including the performance of data transfers that otherwise appear
unrelated to the feature in use.  For example, if a provider needs to insert
protocol headers into the message stream in order to implement a given
capability, the insertion of that header could negatively impact the
performance of all transfers. By exposing such limitations to the application,
the application developer has better control over how to best emulate the
feature or work around its absence.

It is recommended that applications code for only those capabilities required
to achieve the best performance.  If a capability would have little to no
effect on overall performance, developers should avoid using such features as
part of an initial implementation.  This will allow the application to work
well across the widest variety of hardware.  Application optimizations can
then add support for less common features.  To see which features are supported
by which providers, see the libfabric
[Provider Feature Maxtrix](https://github.com/ofiwg/libfabric/wiki/Provider-Feature-Matrix)
for the relevant release.

## Mode Bits (fi_info::mode)

Where capability bits represent features desired by applications, mode bits
correspond to behavior needed by the provider.  That is, capability bits are top
down requests, whereas mode bits are bottom up restrictions.  Mode bits are set
by the provider to request that the application use the API in a specific way
in order to achieve optimal performance.  Mode bits often imply that the
additional work to implement certain communication semantics needed by the
application will be less if implemented by the applicaiton than forcing that
same implementation down into the provider.  Mode bits arise as a result of
hardware implementation restrictions.

An application developer decides which mode bits they want to or can easily
support as part of their development process.  Each mode bit describes a
particular behavior that the application must follow to use various interfaces.
Applications set the mode bits that they support when calling fi_getinfo().
If a provider requires a mode bit that isn't set, that provider will be skipped
by fi_getinfo().  If a provider does not need a mode bit that is set, it will
respond to the fi_getinfo() call, with the mode bit cleared.  This indicates
that the application does not need to perform the action required by the
mode bit.

One of common mode bit needed by providers is FI_CONTEXT (and FI_CONTEXT2).
This mode bit requires that applications pass in a libfabric defined data
structure (struct fi_context) into any data transfer function.  That
structure must remain valid and unused by the application until the data
transfer operation completes.  The purpose behind this mode bit is that the
struct fi_context provides "scratch" space that the provider can use to
track the request.  For example, it may need to insert the request into a
linked list while it is pending, or track the number of times that an
outbound transfer has been retried.  Since many applications already track
outstanding operations with their own data structure, by embedding the
struct fi_context into that same structure, overall performance can be
improved.  This avoids the provider needing to allocate and free internal
structures for each request.

Continuing with this example, if an application does not already track
outstanding requests, then it would leave the FI_CONTEXT mode bit unset.
This would indicate that the provider needs to get and release its own
structure for tracking purposes.  In this case, the costs would essentially
be the same whether it were done by the application or provider.

For the broadest support of different network technologies, applications
should attempt to support as many mode bits as feasible.  It is recommended
that providers support applications that cannot support any mode bits, with
as small an impact as possible.  However, implementation of mode bit avoidance
in the provider can still impact performance, even when the mode bit is
disabled.  As a result, some providers may always require specific mode bits
be set.

# FIDs (fid_t)

FID stands for fabric identifier.  It is the base object type assigned to all
libfabric API objects.  All fabric resources are represented by a fid structure,
and all fid's are derived from a base fid type.  In object-oriented terms, a
fid would be the parent class.  The contents of a fid are visible to the
application.

```
/* Base FID definition */
enum {
    FI_CLASS_UNSPEC,
    FI_CLASS_FABRIC,
    FI_CLASS_DOMAIN,
    ...
};

struct fi_ops {
    size_t size;
    int (*close)(struct fid *fid);
    ...
};

/* All fabric interface descriptors must start with this structure */
struct fid {
    size_t fclass;
    void *context;
    struct fi_ops *ops;
};

```

The fid structure is designed as a trade-off between minimizing memory
footprint versus software overhead.  Each fid is identified as a specific
object class, which helps with debugging.  Examples are given above
(e.g. FI_CLASS_FABRIC).  The context field is an application defined data
value, assigned to an object during its creation.  The use of the context
field is application specific, but it is meant to be read by applications.
Applications often set context to a corresponding structure that it's
allocated.  The context field is the only field that applications are
recommended to access directly.  Access to other fields should be done
using defined function calls (for example, the close() operation).

The ops field points to a set of function pointers.  The fi_ops structure
defines the operations that apply to that class.  The size field in the
fi_ops structure is used for extensibility, and allows the fi_ops structure
to grow in a backward compatible manner as new operations are added.  The
fid deliberately points to the fi_ops structure, rather than embedding the
operations directly.  This allows multiple fids to point to the same set
of ops, which minimizes the memory footprint of each fid. (Internally,
providers usually set ops to a static data structure, with the fid structure
dynamically allocated.)

Although it's possible for applications to access function pointers directly,
it is strongly recommended that the static inline functions defined in the
man pages be used instead.  This is required by applications that may be built
using the FABRIC_DIRECT library feature.  (FABRIC_DIRECT is a compile time
option that allows for highly optimized builds by tightly coupling an
application with a specific provider.)

Other OFI classes are derived from this structure, adding their own set of operations.

```
/* Example of deriving a new class for a fabric object */
struct fi_ops_fabric {
    size_t size;
    int (*domain)(struct fid_fabric *fabric, struct fi_info *info,
        struct fid_domain **dom, void *context);
    ...
};

struct fid_fabric {
    struct fid fid;
    struct fi_ops_fabric *ops;
};
```

Other fid classes follow a similar pattern as that shown for fid_fabric.  The
base fid structure is followed by zero or more pointers to operation sets.

# Fabric (fid_fabric)

The top-level object that applications open is the fabric identifier.  The
fabric can mostly be viewed as a container object by applications, though
it does identify which provider(s) applications use.

Opening a fabric is usually a straightforward call after calling fi_getinfo().

```
int fi_fabric(struct fi_fabric_attr *attr, struct fid_fabric **fabric, void *context);
```

The fabric attributes can be directly accessed from struct fi_info.  The newly
opened fabric is returned through the 'fabric' parameter.  The 'context'
parameter appears in many operations.  It is a user-specified value that is
associated with the fabric.  It may be used to point to an application specific
structure and is retrievable from struct fid_fabric.

## Attributes (fi_fabric_attr)

The fabric attributes are straightforward.

```
struct fi_fabric_attr {
    struct fid_fabric *fabric;
    char *name;
    char *prov_name;
    uint32_t prov_version;
    uint32_t api_version;
};
```

The only field that applications are likely to use directly is the prov_name.
This is a string value that can be used by hints to select a specific provider
for use.  On most systems, there will be multiple providers available.  Only
one is likely to represent the high-performance network attached to the system.
Others are generic providers that may be available on any system, such as the
TCP socket and UDP providers.

The fabric field is used to help applications manage opened fabric resources.
If an application has already opened a fabric that can support the returned
fi_info structure, this will be set to that fabric.

# Domains (fid_domain)

Domains frequently map to a specific local network interface adapter.  A domain
may either refer to the entire NIC, a port on a multi-port NIC, a virtual
device exposed by a NIC, multiple NICs being used in a multi-rail fashion,
and so forth.  Although it's convenient to think of a domain as referring to
a NIC, such an association isn't expected by libfabric.  From the viewpoint
of the application, a domain identifies a set of resources that may be used
together.

Similar to a fabric, opening a domain is straightforward after calling fi_getinfo().

```
int fi_domain(struct fid_fabric *fabric, struct fi_info *info,
    struct fid_domain **domain, void *context);
```

The fi_info structure returned from fi_getinfo() can be passed directly to
fi_domain() to open a new domain.

## Attributes (fi_domain_attr)

One of the goals of a domain is to define the relationship between data
transfer services (endpoints) and completion services (completion queues
and counters).  Many of the domain attributes describe that relationship
and its impact to the application.

```
struct fi_domain_attr {
    struct fid_domain *domain;
    char *name;
    enum fi_threading threading;
    enum fi_progress control_progress;
    enum fi_progress data_progress;
    enum fi_resource_mgmt resource_mgmt;
    enum fi_av_type av_type;
    enum fi_mr_mode mr_mode;
    size_t mr_key_size;
    size_t cq_data_size;
    size_t cq_cnt;
    size_t ep_cnt;
    size_t tx_ctx_cnt;
    size_t rx_ctx_cnt;
    ...
```

Full details of the domain attributes and their meaning are in the fi_domain
man page.  Information on select attributes and their impact to the application
are described below.

## Threading (fi_threading)

libfabric defines a unique threading model.  The libfabric design is heavily
influenced by object-oriented programming concepts.  A multi-threaded
application must determine how libfabric objects (domains, endpoints,
completion queues, etc.) will be allocated among its threads, or if any
thread can access any object.  For example, an application may spawn a new
thread to handle each new connected endpoint.  The domain threading field
provides a mechanism for an application to identify which objects may be
accessed simultaneously by different threads.  This in turn allows a provider
to optimize or, in some cases, eliminate internal synchronization and locking
around those objects.

Threading defines where providers could optimize synchronization primitives.
However, providers may still implement more serialization than is needed
by the application.  (This is usually a result of keeping the provider
implementation simpler).

It is recommended that applications target either FI_THREAD_SAFE (full thread
safety implemented by the provider) or FI_THREAD_DOMAIN (objects associated
with a single domain will only be accessed by a single thread).

## Progress (fi_progress)

Progress models are a result of using the host processor in order to perform
some portion of the transport protocol.  In order to simplify development,
libfabric defines two progress models: automatic or manual.  It does not
attempt to identify which specific interface features may be offloaded, or
what operations require additional processing by the application's thread.

Automatic progress means that an operation initiated by the application will
eventually complete, even if the application makes no further calls into the
libfabric API.  The operation is either offloaded entirely onto hardware,
the provider uses an internal thread, or the operating system kernel may
perform the task.  The use of automatic progress may increase system overhead
and latency in the latter two cases.  For control operations, such as
connection setup, this is usually acceptable.  However, the impact to data
 transfers may be measurable, especially if internal threads are required
 to provide automatic progress.

The manual progress model can avoid this overhead for providers that do not
offload all transport features into hardware.  With manual progress the
provider implementation will handle transport operations as part of specific
libfabric functions.  For example, a call to fi_cq_read() which retrieves an
array completed operations may also be responsible for sending ack messages
to notify peers that a message has been received.  Since reading the
completion queue is part of the normal operation of an application, there is
minimal impact to the application and additional threads are avoided.

Applications need to take care when using manual progress, particularly if
they link into libfabric multiple times through different code paths or library
dependencies.  If application threads are used to drive progress, such as
responding to received data with ACKs, then it is critical that the application
thread call into libfabric in a timely manner.

## Memory Registration (fid_mr)

RMA, atomic, and collective operations can read and write memory that is owned
by a peer process, and neither require the involvement of the target processor.
Because the memory can be modified over the network, an application must opt
into exposing its memory to peers.  This is handled by the memory registration
process.  Registered memory regions associate memory buffers with permissions
granted for access by fabric resources. A memory buffer must be registered
before it can be used as the target of a remote RMA, atomic, or collective
data transfer.  Additionally, a fabric provider may require that data buffers
be registered before being used even in the case of local transfers.  The latter
is necessary to ensure that the virtual to physical page mappings do not change
while network hardware is performing the transfer.

In order to handle diverse hardware requirements, there are a set of mr_mode
bits associated with memory registration.  The mr_mode bits behave similar to
fi_info mode bits.  Applications indicate which types of restrictions they
can support, and the providers clear those bits which aren't needed.

For hardware that requires memory registration, managing registration is
critical to achieving good performance and scalability.  The act of registering
memory is costly and should be avoided on a per data transfer basis.
libfabric has extensive internal support for managing memory registration,
hiding registration from user application, caching registration to reduce per
transfer overhead, and detecting when cached registrations are no longer valid.
It is recommended that applications that are not natively designed to account
for registering memory to make use of libfabric's registration cache.  This
can be done by simply not setting the relevant mr_mode bits.

### Memory Region APIs

The following APIs highlight how to allocate and access a registered memory
region.  Note that this is not a complete list of memory region (MR) calls,
and for full details on each API, readers should refer directly to the fi_mr
man page.

```
int fi_mr_reg(struct fid_domain *domain, const void *buf, size_t len,
    uint64_t access, uint64_t offset, uint64_t requested_key, uint64_t flags,
    struct fid_mr **mr, void *context);

void * fi_mr_desc(struct fid_mr *mr);
uint64_t fi_mr_key(struct fid_mr *mr);
```

By default, memory regions are associated with a domain.  A MR is accessible
by any endpoint that is opened on that domain.  A region starts at the address
specified by 'buf', and is 'len' bytes long.  The 'access' parameter are
permission flags that are OR'ed together.  The permissions indicate which
type of operations may be invoked against the region (e.g. FI_READ, FI_WRITE,
FI_REMOTE_READ, FI_REMOTE_WRITE).  The 'buf' parameter typically references
allocated virtual memory.

A MR is associated with local and remote protection keys.  The local key is
referred to as a memory descriptor and may be retrieved by calling fi_mr_desc().
This call is only needed if the FI_MR_LOCAL mr_mode bit has been set.  The
memory descriptor is passed directly into data transfer operations, for example:

```
/* fi_mr_desc() example using fi_send() */
fi_send(ep, buf, len, fi_mr_desc(mr), 0, NULL);
```

The remote key, or simply MR key, is used by the peer when targeting the MR
with an RMA or atomic operation.   In many cases, the key will need to be sent
in a separate message to the initiating peer.  libfabric API uses a 64-bit key
where one is used.  The actual key size used by a provider is part of its domain
attributes  Support for larger key sizes, as required by some providers, is
conveyed through an mr_mode bit, and requires the use of extended MR API calls
that map the larger size to a 64-bit value.

# Endpoints

Endpoints are transport level communication portals.  Opening an endpoint is
trivial after calling fi_getinfo().

## Active (fid_ep)

Active endpoints may be connection-oriented or connection-less.  They are
considered active as they may be used to perform data transfers.  All data
transfer interfaces – messages (fi_msg), tagged messages (fi_tagged),
RMA (fi_rma), atomics (fi_atomic), and collectives (fi_collective) – are
associated with active endpoints.  Though an individual endpoint may not
be enabled to use all data transfers.  In standard configurations, an
active endpoint has one transmit and one receive queue.  In general,
operations that generate traffic on the fabric are posted to the transmit
queue. This includes all RMA and atomic operations, along with sent messages
and sent tagged messages. Operations that post buffers for receiving incoming
data are submitted to the receive queue.

Active endpoints are created in the disabled state.  The endpoint must first
be configured prior to it being enabled.  Endpoints must transition into
an enabled state before accepting data transfer operations, including posting
of receive buffers. The fi_enable() call is used to transition an active endpoint
into an enabled state. The fi_connect() and fi_accept() calls will also
transition an endpoint into the enabled state, if it is not already enabled.

```
int fi_endpoint(struct fid_domain *domain, struct fi_info *info,
    struct fid_ep **ep, void *context);
```

### Enabling (fi_enable)

In order to transition an endpoint into an enabled state, it must be bound to
one or more fabric resources.  This includes binding the endpoint to a
completion queue and event queue.  Unconnected endpoints must also be bound to
an address vector.

```
/* Example to enable an unconnected endpoint */

/* Allocate an address vector and associated it with the endpoint */
fi_av_open(domain, &av_attr, &av, NULL);
fi_ep_bind(ep, &av->fid, 0);

/* Allocate and associate completion queues with the endpoint */
fi_cq_open(domain, &cq_attr, &cq, NULL);
fi_ep_bind(ep, &cq->fid, FI_TRANSMIT | FI_RECV);

fi_enable(ep);
```

In the above example, we allocate an AV and CQ.  The attributes for the AV
and CQ are omitted (additional discussion below).  Those are then associated
with the endpoint through the fi_ep_bind() call.  After all necessary resources
have been assigned to the endpoint, we enable it.  Enabling the endpoint
indicates to the provider that it should allocate any hardware and software
resources and complete the initialization for the endpoint.  (If the endpoint
is not bound to all necessary resources, the fi_enable() call will fail.)

The fi_enable() call is always called for unconnected endpoints.  Connected
endpoints may be able to skip calling fi_enable(), since fi_connect() and
fi_accept() will enable the endpoint automatically.  However, applications
may still call fi_enable() prior to calling fi_connect() or fi_accept().
Doing so allows the application to post receive buffers to the endpoint,
which ensures that they are available to receive data in the case the peer
endpoint sends messages immediately after it establishes the connection.

## Passive (fid_pep)

Passive endpoints are used to listen for incoming connection requests.
Passive endpoints are of type FI_EP_MSG, and may not perform any data transfers.
An application wishing to create a passive endpoint typically calls fi_getinfo()
using the FI_SOURCE flag, often only specifying a 'service' address.  The
service address corresponds to a TCP port number.

Passive endpoints are associated with event queues.  Event queues report
connection requests from peers.  Unlike active endpoints, passive endpoints are
not associated with a domain.  This allows an application to listen for
connection requests across multiple domains, though still restricted to a
single provider.

```
/* Example passive endpoint listen */
fi_passive_ep(fabric, info, &pep, NULL);

fi_eq_open(fabric, &eq_attr, &eq, NULL);
fi_pep_bind(pep, &eq->fid, 0);

fi_listen(pep);
```

A passive endpoint must be bound to an event queue before calling listen.
This ensures that connection requests can be reported to the application.
To accept new connections, the application waits for a request, allocates a
new active endpoint for it, and accepts the request.

```
/* Example accepting a new connection */

/* Wait for a CONNREQ event */
fi_eq_sread(eq, &event, &cm_entry, sizeof cm_entry, -1, 0);
assert(event == FI_CONNREQ);

/* Allocate a new endpoint for the connection */
if (!cm_entry.info->domain_attr->domain)
    fi_domain(fabric, cm_entry.info, &domain, NULL);
fi_endpoint(domain, cm_entry.info, &ep, NULL);

fi_ep_bind(ep, &eq->fid, 0);
fi_cq_open(domain, &cq_attr, &cq, NULL);
fi_ep_bind(ep, &cq->fid, FI_TRANSMIT | FI_RECV);

fi_enable(ep);
fi_recv(ep, rx_buf, len, NULL, 0, NULL);

fi_accept(ep, NULL, 0);
fi_eq_sread(eq, &event, &cm_entry, sizeof cm_entry, -1, 0);
assert(event == FI_CONNECTED);
```

The connection request event (FI_CONNREQ) includes information about the
type of endpoint to allocate, including default attributes to use.  If a
domain has not already been opened for the endpoint, one must be opened.
Then the endpoint and related resources can be allocated.  Unlike the
unconnected endpoint example above, a connected endpoint does not have an AV,
but does need to be bound to an event queue.  In this case, we use the same
EQ as the listening endpoint.  Once the other EP resources (e.g. CQ) have
been allocated and bound, the EP can be enabled.

To accept the connection, the application calls fi_accept().  Note that because
of thread synchronization issues, it is possible for the active endpoint to
receive data even before fi_accept() can return.  The posting of receive
buffers prior to calling fi_accept() handles this condition, which avoids
network flow control issues occurring immediately after connecting.

The fi_eq_sread() calls are blocking (synchronous) read calls to the event
queue.  These calls wait until an event occurs, which in this case are
connection request and establishment events.

## EP Attributes (fi_ep_attr)

The properties of an endpoint are specified using endpoint attributes.
These are attributes for the endpoint as a whole.  There are additional
attributes specifically related to the transmit and receive contexts
underpinning the endpoint (details below).

```
struct fi_ep_attr {
    enum fi_ep_type type;
    uint32_t        protocol;
    uint32_t        protocol_version;
    size_t          max_msg_size;
    ...
};
```

A full description of each field is available in the fi_endpoint man page,
with selected details listed below.

### Endpoint Type (fi_ep_type)

This indicates the type of endpoint: reliable datagram (FI_EP_RDM),
reliable-connected (FI_EP_MSG), or unreliable datagram (FI_EP_DGRAM).
Nearly all applications will want to specify the endpoint type as a hint
passed into fi_getinfo, as most applications will only be coded to support
a single endpoint type.

### Maximum Message Size (max_msg_size)

This size is the maximum size for any data transfer operation that goes over
the endpoint. For unreliable datagram endpoints, this is often the MTU of the
underlying network. For reliable endpoints, this value is often a restriction
of the underlying transport protocol.  A common minimum maximum message size
is 2GB, though some providers support an arbitrarily large size.  Applications
that require transfers larger than the maximum reported size are required to
break up a single, large transfer into multiple operations.

Providers expose their hardware or network limits to the applications, rather
than segmenting large transfers internally, in order to minimize completion
overhead. For example, for a provider to support large message segmentation
internally, it would need to emulate all completion mechanisms (queues and
counters) in software, even if transfers that are larger than the transport
supported maximum were never used.

### Message Order Size (max_order_xxx_size)

These fields specify data ordering.   They define the delivery order of
transport data into target memory for RMA and atomic operations.  Data
ordering requires message ordering.  If message ordering is not specified,
these fields do not apply.

For example, suppose that an application issues two RMA write operations to
the same target memory location.  (The application may be writing a time stamp
value every time a local condition is met, for instance).  Message ordering
indicates that the first write as initiated by the sender is the first write
processed by the receiver.  Data ordering indicates whether the _data_ from
the first write updates memory before the second write updates memory.

The max_order_xxx_size fields indicate how large a message may be while still
achieving data ordering.  If a field is 0, then no data ordering is guaranteed.
If a field is the same as the max_msg_size, then data order is guaranteed for
all messages.

Providers may support data ordering up to max_msg_size for back to back operations
that are the same.  For example, an RMA write followed by an RMA write may have data
ordering regardless of the size of the data transfer (max_order_waw_size =
max_msg_size).  Mixed operations, such as a read followed by a write, are often
restricted.  This is because RMA read operations may require acknowledgments from
the _initiator_, which impacts the re-transmission protocol.

For example, consider an RMA read followed by a write.  The target will process
the read request, retrieve the data, and send a reply.  While that is occurring,
a write is received that wants to update the same memory location accessed
by the read. If the target processes the write, it will overwrite the memory
used by the read. If the read response is lost, and the read is retried, the
target will be unable to re-send the data. To handle this, the target either
needs to: defer handling the write until it receives an acknowledgment for
the read response, buffer the read response so it can be re-transmitted, or
indicate that data ordering is not guaranteed.

Because the read or write operation may be gigabytes in size, deferring the
write may add significant latency, and buffering the read response may be
impractical. The max_order_xxx_size fields indicate how large back to back
operations may be with ordering still maintained. In many cases, read after
write and write and read ordering may be significantly limited, but still
usable for implementing specific algorithms, such as a global locking mechanism.

## Rx/Tx Context Attributes (fi_rx_attr / fi_tx_attr)

The endpoint attributes define the overall abilities for the endpoint;
however, attributes that apply specifically to receive or transmit contexts
are defined by struct fi_rx_attr and fi_tx_attr, respectively:

```
struct fi_rx_attr {
    uint64_t caps;
    uint64_t mode;
    uint64_t op_flags;
    uint64_t msg_order;
    uint64_t comp_order;
    ...
};

struct fi_tx_attr {
    uint64_t caps;
    uint64_t mode;
    uint64_t op_flags;
    uint64_t msg_order;
    uint64_t comp_order;
    size_t inject_size;
    ...
};
```

Rx/Tx context capabilities must be a subset of the endpoint capabilities. For
many applications, the default attributes returned by the provider will be
sufficient, with the application only needing to specify endpoint attributes.

Both context attributes include an op_flags field. This field is used by
applications to specify the default operation flags to use with any call.
For example, by setting the transmit context’s op_flags to FI_INJECT, the
application has indicated to the provider that all transmit operations should
assume ‘inject’ behavior is desired.  I.e. the buffer provided to the call
must be returned to the application upon return from the function.  The
op_flags applies to all operations that do not provide flags as part of
the call (e.g. fi_sendmsg).  One use of op_flags is to specify the
default completion semantic desired (discussed next) by the application.  By
setting the default op_flags at initialization time, we can avoid passing the
flags as arguments into some data transfer calls, avoid parsing the flags, and
can prepare submitted commands ahead of time.

It should be noted that some attributes are dependent upon the peer endpoint
having supporting attributes in order to achieve correct application behavior.
For example, message order must be the compatible between the initiator’s
transmit attributes and the target’s receive attributes. Any mismatch may
result in incorrect behavior that could be difficult to debug.

# Completions

Data transfer operations complete asynchronously. Libfabric defines two
mechanism by which an application can be notified that an operation has
completed: completion queues and counters.  Regardless of which mechanism
is used to notify the application that an operation is done, developers
must be aware of what a completion indicates.

In all cases, a completion indicates that it is safe to reuse the buffer(s)
associated with the data transfer. This completion mode is referred to as
_inject_ complete and corresponds to the operational flags FI_INJECT_COMPLETE.
However, a completion may also guarantee stronger semantics.

Although libfabric does not define an implementation, a provider can meet the
requirement for inject complete by copying the application’s buffer into a
network buffer before generating the completion. Even if the transmit
operation is lost and must be retried, the provider can resend the
original data from the copied location. For large transfers, a provider
may not mark a request as inject complete until the data has been
acknowledged by the target. Applications, however, should only infer that
it is safe to reuse their data buffer for an inject complete operation.

Transmit complete is a completion mode that provides slightly stronger
guarantees to the application. The meaning of transmit complete depends
on whether the endpoint is reliable or unreliable. For an unreliable
endpoint (FI_EP_DGRAM), a transmit completion indicates that the request
has been delivered to the network. That is, the message has been delivered at
least as far as hardware queues on the local NIC. For reliable endpoints,
a transmit complete occurs when the request has reached the target
endpoint. Typically, this indicates that the target has acked the
request. Transmit complete maps to the operation flag FI_TRANSMIT_COMPLETE.

A third completion mode is defined to provide guarantees beyond transmit
complete. With transmit complete, an application knows that the message is
no longer dependent on the local NIC or network (e.g. switches). However,
the data may be buffered at the remote NIC and has not necessarily been
written to the target memory. As a result, data sent in the request may
not be visible to all processes. The third completion mode is delivery
complete.

Delivery complete indicates that the results of the operation are available
to all processes on the fabric. The distinction between transmit and delivery
complete is subtle, but important. It often deals with _when_ the target
endpoint generates an acknowledgment to a message. For providers that offload
transport protocol to the NIC, support for transmit complete is common.
Delivery complete guarantees are more easily met by providers that implement
portions of their protocol on the host processor. Delivery complete
corresponds to the FI_DELIVERY_COMPLETE operation flag.

Applications can request a default completion mode when opening an endpoint
by setting one of the above mentioned complete flags as an op_flags for the
context’s attributes. However, it is usually recommended that application
use the provider’s default flags for best performance, and amend its protocol
to achieve its completion semantics. For example, many applications will
perform a ‘finalize’ or ‘commit’ procedure as part of their operation,
which synchronizes the processing of all peers and guarantees that all
previously sent data has been received.

A full discussion of completion semantics is given in the fi_cq man page.

## CQs (fid_cq)

Completion queues often map directly to provider hardware mechanisms, and
libfabric is designed around minimizing the software impact of accessing
those mechanisms. Unlike other objects discussed so far (fabrics, domains,
endpoints), completion queues are not part of the fi_info structure or
involved with the fi_getinfo() call.

All active endpoints must be bound with one or more completion queues.
This is true even if completions will be suppressed by the application
(e.g. using the FI_SELECTIVE_COMPLETION flag). Completion queues are
needed to report operations that complete in error and help drive progress
in the case of manual progress.

CQs are allocated separately from endpoints and are associated with endpoints
through the fi_ep_bind() function.

## CQ Format (fi_cq_format)

In order to minimize the amount of data that a provider must report, the
type of completion data written back to the application is select-able.
This limits the number of bytes the provider writes to memory, and allows
necessary completion data to fit into a compact structure. Each CQ format
 maps to a specific completion structure. Developers should analyze each
 structure, select the smallest structure that contains all of the data
 it requires, and specify the corresponding enum value as the CQ format.

For example, if an application only needs to know which request completed,
along with the size of a received message, it can select the following:

```
cq_attr->format = FI_CQ_FORMAT_MSG;

struct fi_cq_msg_entry {
    void      *op_context;
    uint64_t  flags;
    size_t    len;
};
```

Once the format has been selected, the underlying provider will assume that
read operations against the CQ will pass in an array of the corresponding
structure.  The CQ data formats are designed such that a structure that
reports more information can be cast to one that reports less.

## Reading Completions (fi_cq_read)

Completions may be read from a CQ by using one of the non-blocking calls,
fi_cq_read / fi_cq_readfrom, or one of the blocking calls, fi_cq_sread /
fi_cq_sreadfrom. Regardless of which call is used, applications pass in
an array of completion structures based on the selected CQ format. The
CQ interfaces are optimized for batch completion processing, allowing the
application to retrieve multiple completions from a single read call.
The difference between the read and readfrom calls is that readfrom
returns source addressing data, if available. The readfrom derivative
of the calls is only useful for unconnected endpoints, and only if the
corresponding endpoint has been configured with the FI_SOURCE capability.

FI_SOURCE requires that the provider use the source address available
in the raw completion data, such as the packet's source address, to retrieve
a matching entry in the endpoint’s address vector. Applications that carry
some sort of source identifier as part of their data packets can avoid
 the overhead associated with using FI_SOURCE.

### Retrieving Errors

Because the selected completion structure is insufficient to report all
data necessary to debug or handle an operation that completes in error,
failed operations are reported using a separate fi_cq_readerr() function.
This call takes as input a CQ error entry structure, which allows the provider
to report more information regarding the reason for the failure.

```
/* read error prototype */
fi_cq_readerr(struct fid_cq *cq, struct fi_cq_err_entry *buf, uint64_t flags);

/* error data structure */
struct fi_cq_err_entry {
    void      *op_context;
    uint64_t  flags;
    size_t    len;
    void      *buf;
    uint64_t  data;
    uint64_t  tag;
    size_t    olen;
    int       err;
    int       prov_errno;
    void      *err_data;
    size_t    err_data_size;
};

/* Sample error handling */
struct fi_cq_msg_entry entry;
struct fi_cq_err_entry err_entry;
char err_data[256];
int ret;

err_entry.err_data = err_data;
err_entry.err_data_size = 256;

ret = fi_cq_read(cq, &entry, 1);
if (ret == -FI_EAVAIL)
    ret = fi_cq_readerr(cq, &err_entry, 0);
```

As illustrated, if an error entry has been inserted into the completion
 queue, then attempting to read the CQ will result in the read call
 returning -FI_EAVAIL (error available).  This indicates that the application
 must use the fi_cq_readerr() call to remove the failed operation's
 completion information before other completions can be reaped from the CQ.

A fabric error code regarding the failure is reported as the err field
in the fi_cq_err_entry structure.  A provider specific error code is also
available through the prov_errno field.  This field can be decoded into a
displayable string using the fi_cq_strerror() routine. The err_data
field is provider specific data that assists the provider in decoding
the reason for the failure.

# Address Vectors (fid_av)

A primary goal of address vectors is to allow applications to communicate with
thousands to millions of peers while minimizing the amount of data needed to
store peer addressing information. It pushes fabric specific addressing details
away from the application to the provider. This allows the provider to optimize
how it converts addresses into routing data, and enables data compression
techniques that may be difficult for an application to achieve without being
aware of low-level fabric addressing details. For example, providers may be able
to algorithmically calculate addressing components, rather than storing the data
locally. Additionally, providers can communicate with resource management
entities or fabric manager agents to obtain quality of service or other
information about the fabric, in order to improve network utilization.

An equally important objective is ensuring that the resulting interfaces,
particularly data transfer operations, are fast and easy to use. Conceptually,
an address vector converts an endpoint address into an fi_addr_t. The fi_addr_t
(fabric interface address datatype) is a 64-bit value that is used in all
‘fast-path’ operations – data transfers and completions.

Address vectors are associated with domain objects. This allows providers to
implement portions of an address vector, such as quality of service mappings,
in hardware.

## AV Type (fi_av_type)

There are two types of address vectors. The type refers to the format of the
returned fi_addr_t values for addresses that are inserted into the AV. With
type FI_AV_TABLE, returned addresses are simple indices, and developers may
think of the AV as an array of addresses. Each address that is inserted into
the AV is mapped to the index of the next free array slot. The advantage of
FI_AV_TABLE is that applications can refer to peers using a simple index,
eliminating an application’s need to store any addressing data. I.e. the
application can generate the fi_addr_t values themselves.  This type maps
well to applications, such as MPI, where a peer is referenced by rank.

The second type is FI_AV_MAP. This type does not define any specific format
for the fi_addr_t value. Applications that use type map are required to provide
the correct fi_addr_t for a given peer when issuing a data transfer operation.
The advantage of FI_AV_MAP is that a provider can use the fi_addr_t to encode
the target’s address, which avoids retrieving the data from memory. As a simple
example, consider a fabric that uses TCP/IPv4 based addressing. An fi_addr_t
is large enough to contain the address, which allows a provider to copy the
data from the fi_addr_t directly into an outgoing packet.

## Sharing AVs Between Processes

Large scale parallel programs typically run with multiple processes allocated
on each node. Because these processes communicate with the same set of peers,
the addressing data needed by each process is the same. Libfabric defines a
mechanism by which processes running on the same node may share their address
vectors. This allows a system to maintain a single copy of addressing data,
rather than one copy per process.

Although libfabric does not require any implementation for how an address
vector is shared, the interfaces map well to using shared memory. Address
vectors which will be shared are given an application specific name. How an
application selects a name that avoid conflicts with unrelated processes,
or how it communicates the name with peer processes is outside the scope of
libfabric.

In addition to having a name, a shared AV also has a base map address --
map_addr. Use of map_addr is only important for address vectors that are of
type FI_AV_MAP, and allows applications to share fi_addr_t values. From the
viewpoint of the application, the map_addr is the base value for all fi_addr_t
values. A common use for map_addr is for the process that creates the initial
address vector to request a value from the provider, exchange the returned
map_addr with its peers, and for the peers to open the shared AV using the
same map_addr. This allows the fi_addr_t values to be stored in shared
memory that is accessible by all peers.

# Using Native Wait Objects: TryWait

There is an important difference between using libfabric completion objects,
versus sockets, that may not be obvious from the discussions so far. With
sockets, the object that is signaled is the same object that abstracts the
queues, namely the file descriptor. When data is received on a socket, that
data is placed in a queue associated directly with the fd. Reading from the
fd retrieves that data. If an application wishes to block until data arrives
on a socket, it calls select() or poll() on the fd. The fd is signaled when
a message is received, which releases the blocked thread, allowing it to
read the fd.

By associating the wait object with the underlying data queue, applications
are exposed to an interface that is easy to use and race free. If data is
available to read from the socket at the time select() or poll() is called,
those calls simply return that the fd is readable.

There are a couple of significant disadvantages to this approach, which have
been discussed previously, but from different perspectives. The first is that
every socket must be associated with its own fd. There is no way to share a
wait object among multiple sockets. (This is a main reason for the development
of epoll semantics). The second is that the queue is maintained in the kernel,
so that the select() and poll() calls can check them.

Libfabric allows for the separation of the wait object from the data queues.
For applications that use libfabric interfaces to wait for events, such as
fi_cq_sread, this separation is mostly hidden from the application. The
exception is that applications may receive a signal, but no events are retrieved
when a queue is read.  This separation allows the queues to reside in the
application's memory space, while wait objects may still use kernel components.
A reason for the latter is that wait objects may be signaled as part of
system interrupt processing, which would go through a kernel driver.

Applications that want to use native wait objects (e.g. file descriptors)
directly in operating system calls must perform an additional step in their
processing. In order to handle race conditions that can occur between inserting
an event into a completion or event object and signaling the corresponding wait
object, libfabric defines an ‘fi_trywait()’ function. The fi_trywait
implementation is responsible for handling potential race conditions which could
result in an application either losing events or hanging. The following example
demonstrates the use of fi_trywait().

```
/* Get the native wait object -- an fd in this case */
fi_control(&cq->fid, FI_GETWAIT, (void *) &fd);
FD_ZERO(&fds);
FD_SET(fd, &fds);

while (1) {
    ret = fi_trywait(fabric, &cq->fid, 1);
    if (ret == FI_SUCCESS) {
        /* It’s safe to block on the fd */
        select(fd + 1, &fds, NULL, &fds, &timeout);
    } else if (ret == -FI_EAGAIN) {
        /* Read and process all completions from the CQ */
        do {
            ret = fi_cq_read(cq, &comp, 1);
        } while (ret > 0);
    } else {
        /* something really bad happened */
    }
}
```

In this example, the application has allocated a CQ with an fd as its wait
object. It calls select() on the fd. Before calling select(), the application
must call fi_trywait() successfully (return code of FI_SUCCESS). Success
indicates that a blocking operation can now be invoked on the native wait
object without fear of the application hanging or events being lost. If
fi_trywait() returns –FI_EAGAIN, it usually indicates that there are queued
events to process.

# Environment Variables

Environment variables are used by providers to configure internal options
for optimal performance or memory consumption.  Libfabric provides an interface
for querying which environment variables are usable, along with an application
to display the information to a command window.  Although environment variables
are usually configured by an administrator, an application can query for
variables programmatically.

```
/* APIs to query for supported environment variables */
enum fi_param_type {
    FI_PARAM_STRING,
    FI_PARAM_INT,
    FI_PARAM_BOOL,
    FI_PARAM_SIZE_T,
};

struct fi_param {
    /* The name of the environment variable */
    const char *name;
    /* What type of value it stores */
    enum fi_param_type type;
    /* A description of how the variable is used */
    const char *help_string;
    /* The current value of the variable */
    const char *value;
};

int fi_getparams(struct fi_param **params, int *count);
void fi_freeparams(struct fi_param *params);
```

The modification of environment variables is typically a tuning activity done
on larger clusters.  However there are a few values that are useful for
developers.  These can be seen by executing the fi_info command.

```
$ fi_info -e
# FI_LOG_LEVEL: String
# Specify logging level: warn, trace, info, debug (default: warn)

# FI_LOG_PROV: String
# Specify specific provider to log (default: all)

# FI_PROVIDER: String
# Only use specified provider (default: all available)
```

The fi_info application, which ships with libfabric, can be used to list all
environment variables for all providers.  The '-e' option will list all
variables, and the '-g' option can be used to filter the output to only
those variables with a matching substring.  Variables are documented directly
in code with the description available as the help_string output.

The FI_LOG_LEVEL can be used to increase the debug output from libfabric and
the providers.  Note that in the release build of libfabric, debug output from
data path operations (transmit, receive, and completion processing) may not
be available.  The FI_PROVIDER variable can be used to enable or disable
specific providers.  This is useful to ensure that a given provider will be
used.
