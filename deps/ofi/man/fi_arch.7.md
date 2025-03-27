---
layout: page
title: fi_arch(7)
tagline: Libfabric Programmer's Guide - Architecture
---
{% include JB/setup %}

# NAME

fi_arch \- libfabric architecture

# OVERVIEW

Libfabric APIs define application facing communication semantics without
mandating the underlying implementation or wire protocols. It is architected
such that applications can have direct access to network hardware without
operating system intervention, but does not mandate such an implementation.
The APIs have been defined specifically to allow multiple implementations.

The following diagram highlights the general architecture of the interfaces
exposed by libfabric.

```
                 Applications and Middleware
       [MPI]   [SHMEM]   [PGAS]   [Storage]   [Other]

--------------------- libfabric API ---------------------

/  Core  \ + /Communication\ + /  Data  \ + <Completion>
\Services/   \    Setup    /   \Transfer/

----------------- libfabric Provider API ----------------

                    libfabric providers
   [TCP]   [UDP]   [Verbs]    [EFA]    [SHM]   [Other]

---------------------------------------------------------

     Low-level network hardware and software interfaces
```
Details of each libfabric component is described below.

## Core Services

libfabric can be divided between the libfabric core and providers.  The
core defines defines the APIs that applications use and implements what is
referred to as discovery services.  Discovery services are responsible
for identifying what hardware is available in the system, platform features,
operating system features, associated communication and computational
libraries, and so forth.  Providers are optimized implementations of the
libfabric API.  One of the goals of the libfabric core is to link upper
level applications and middleware with specific providers best suited for
their needs.

From the viewpoint of an application, the core libfabric services are
accessed primarily by the fi_getinfo() API.  See
[`fi_getinfo`(3)](fi_getinfo.3.html).

## Providers

Unlike many libraries, the libfabric core does not implement most of the APIs
called by its users.  Instead, that responsibility instead falls to what
libfabric calls providers.  The bulk of the libfabric API is implemented by each
provider.  When an application calls a libfabric API, that function call is
routed directly into a specific provider.  This is done using function
pointers associated with specific libfabric defined objects.  The object
model is describe in more detail below.

The benefit of this approach is that each provider can optimize the libfabric
defined communication semantics according to their available network hardware,
operating system, platform, and network protocol features.

In general, each provider focuses on supporting the libfabric API over a
specific lower-level communication API or NIC.  See
[`fi_provider`(7)](fi_provider.7.html) for a discussion on the different
types of providers available and the provider architecture.

## Communication Setup

At a high-level, communication via libfabric may be either connection-
oriented or connectionless.  This is similar to choosing between using TCP
or UDP sockets, though libfabric supports reliable-connectionless
communication semantics.  Communication between two processes occurs over
a construct known as an endpoint.  Conceptually, an endpoint is equivalent
to a socket in the socket API world.

Specific APIs and libfabric objects are designed to manage and setup
communication between nodes.  It includes calls for connection management (CM),
as well as functionality used to address connection-less endpoints.

The CM APIs are modeled after APIs used to connect
TCP sockets: connect(), bind(), listen(), and accept().  A main difference
is that libfabric calls are designed to always operate asynchronously.
CM APIs are discussed in [`fi_cm`(3)](fi_cm.3.html).

For performance and scalability reasons discussed in the
[`fi_intro`(7)](fi_intro.7.html) page, connection-less endpoints use a unique
model to setup communication.  These are based on a concept referred to as
address vectors, where the term vector means table or array.  Address vectors
are discussed in detail later, but target applications needing to talk with
potentially thousands to millions of peers.

## Data Transfer Services

libfabric provides several data transfer semantics to meet different
application requirements.  There are five basic sets of data transfer APIs:
messages, tagged messages, RMA, atomics, and collectives.

*Messages*
: Message APIs expose the ability to send and receive data with message
  boundaries being maintained.  Message transfers act as FIFOs, with sent
  messages matched with receive buffers in the order that messages are
  received at the target.  The message APIs are modeled after socket APIs,
  such as send(). sendto(), sendmsg(), recv(), recvmsg(), etc.  For more
  information see [`fi_msg`(3)](fi_msg.3.html).

*Tagged Messages*
: Tagged messages are similar to messages APIs, with the exception of how
  messages are matched at the receiver.  Tagged messages maintain message
  boundaries, same as the message API.  The tag matching APIs differ from
  the message APIs in that received messages are directed into buffers
  based on small steering tags that are specified and carried in the sent
  message.  All message buffers, posted to send or receive data,
  are associated with a tag value.  Sent messages are matched with buffers
  at the receiver that have the same tag.  For more information, see
  [`fi_tagged`(3)](fi_tagged.3.html).

*RMA*
: RMA stands for remote memory access. RMA transfers allow an application
  to write data directly into a specific memory location in a target process
  or to read memory from a specific address at the target process and
  return the data into a local buffer.  RMA is also known as RDMA (remote
  direct memory access); however, RDMA originally defined a specific
  transport implementation of RMA.  For more information, see
  [`fi_rma`(3)](fi_rma.3.html).

*Atomics*
: Atomic operations add arithmetic operations to RMA transfers.  Atomics
  permit direct access and manipulation of memory on the target process.
  libfabric defines a wide range of arithmetic operations that may be
  invoked as part of a data transfer operation.  For more information,
  see [`fi_atomic`(3)](fi_atomic.3.html).

*Collectives*
: The above data transfer APIs perform point-to-point communication.
  Data transfers occur between exactly one initiator and one target.
  Collective operations are coordinated atomic operations among an
  arbitrarily large number of peers.  For more information, see
  [`fi_collective`(3)](fi_collective.3.html).

## Memory Registration

One of the objective of libfabric is to allow network hardware direct
access to application data buffers.  This is accomplished through an
operation known as memory registration.

In order for a NIC to read or write application memory directly, it must
access the physical memory pages that back the application's address space.
Modern operating systems employ page files that swap out virtual pages from
one process with the virtual pages from another.  As a result, a physical
memory page may map to different virtual addresses depending on when it
is accessed.  Furthermore, when a virtual page is swapped in, it may be
mapped to a new physical page.  If a NIC attempts to read or write
application memory without being linked into the virtual address manager,
it could access the wrong data, possibly corrupting an application's memory.
Memory registration can be used to avoid this situation from occurring.
For example, registered pages can be marked such that the operating system
locks the virtual to physical mapping, avoiding any possibility of the
virtual page being paged out or remapped.

Memory registration is also the security mechanism used to grant a remote
peer access to local memory buffers.  Registered memory regions associate
memory buffers with permissions granted for access by fabric resources.
A memory buffer must be registered before it can be used as the target
of an RMA or atomic data transfer.  Memory registration provides a simple
protection mechanism.  (Advanced scalable networks employ other mechanisms,
which are considered out of scope for the purposes of this discussion.)
After a memory buffer has been registered, that registration request
(buffer's address, buffer length, and access permission) is given a
registration key.  Peers that issue RMA or atomic operations against that
memory buffer must provide this key as part of their operation.  This helps
protects against unintentional accesses to the region.

## Completion Services

libfabric data transfers operate asynchronously. Completion services are
used to report the results of submitted data transfer operations.
Completions may be reported using the cleverly named completions queues,
which provide details about the operation that completed. Or, completions
may be reported using completion counters that simply return the number
of operations that have completed.

Completion services are designed with high-performance, low-latency in mind.
The calls map directly into the providers, and data structures are defined
to minimize memory writes and cache impact.  Completion services do not have
corresponding socket APIs.  However, for Windows developers, they are similar
to IO completion ports.

# Object Model

libfabric follows an object-oriented design model.  Although the interfaces
are written in C, the structures and implementation have a C++ feel to them.
The following diagram shows a high-level view of notable libfabric objects
and object dependencies.

```
/ Passive \ ---> <Fabric> <--- /Event\
\Endpoints/         ^          \Queue/
                    |
  /Address\ ---> <Domain> <--- /Completion\
  \Vector /       ^  ^         \  Queue   /
                  |  |
      /Memory\ ---    --- / Active \
      \Region/            \Endpoint/

```

*Fabric*
: A fabric represents a collection of hardware and software resources that
  access a single physical or virtual network.  For example, a fabric may
  be a single network subnet or cluster.  All network ports on a system that
  can communicate with each other through the fabric belong to the same
  fabric. A fabric shares network addresses and can span multiple providers.
  Fabrics are the top level object from which other objects are allocated.

*Domain*
: A domain represents a logical connection into a fabric.  In the simplest
  case, a domain may correspond to a physical or virtual NIC; however a domain
  could include multiple NICs (in the case of a multi-rail provider), or no
  NIC at all (in the case of shared memory).  A domain defines the boundary
  within which other resources may be associated.  Active endpoints and
  completion queues must be part of the same domain in order to be related
  to each other.

*Passive Endpoint*
: Passive endpoints are used by connection-oriented protocols to listen for
  incoming connection requests. Passive endpoints often map to software
  constructs and may span multiple domains.  They are best represented by
  a listening socket.

*Event Queues*
: Event queues (EQs) are used to collect and report the completion of
  asynchronous operations and events. Event queues handle _control_ events,
  that is, operations which are not directly associated with data transfer
  operations. The reason for separating control events from data transfer
  events is for performance reasons.  Event queues are often implemented
  entirely in software using operating system constructs.  Control events
  usually occur during an application's initialization phase, or at a rate
  that's several orders of magnitude smaller than data transfer events.
  Event queues are most commonly used by connection-oriented protocols for
  notification of connection request or established events.

*Active Endpoint*
: Active endpoints are data transfer communication portals.  They are
  conceptually similar to a TCP or UDP socket.  Active endpoints are used to
  perform data transfers.  Active endpoints implement the network protocol.

*Completion Queue*
: Completion queues (CQs) are high-performance queues used to report the
  completion of data transfer operations.  Unlike event queues, completion
  queues are often fully or partially implemented in hardware.  Completion
  queue interfaces are designed to minimize software overhead.

*Memory Region*
: Memory regions describe application’s local memory buffers. In order for
  fabric resources to access application memory, the application must first
  grant permission to the fabric provider by constructing a memory region.
  Memory regions are required for specific types of data transfer operations,
  such as RMA and atomic operations.

*Address Vectors*
: Address vectors are used by connection-less endpoints. They map higher level
  addresses, such as IP addresses or hostnames, which may be more natural for
  an application to use, into fabric specific addresses. The use of address
  vectors allows providers to reduce the amount of memory required to
  maintain large address look-up tables, and eliminate expensive address
  resolution and look-up methods during data transfer operations.

# Communication Model

Endpoints represent communication portals, and all data transfer operations
are initiated on endpoints. libfabric defines the conceptual model for how
endpoints are exposed to applications.  It supports three main communication
endpoint types.  The endpoint names are borrowed from socket API naming.

*FI_EP_MSG*
: Reliable-connected

*FI_EP_DGRAM*
: Unreliable datagram

*FI_EP_RDM*
: Reliable-unconnected

Communication setup is based on whether the endpoint is connected or
unconnected.  Reliability is a feature of the endpoint's data transfer
protocol.

## Connected Communications

The following diagram highlights the general usage behind connection-oriented
communication. Connected communication is based on the flow used to connect
TCP sockets, with improved asynchronous support.

```
         1 listen()              2 connect()
             |                      |
         /Passive \  <---(3)--- / Active \
         \Endpoint/             \Endpoint/
         /                               \
        / (4 CONNREQ)                     \
/Event\                                     /Event\
\Queue/                                     \Queue/
                                           /
         5 accept()         (8 CONNECTED) /
             |                           /
         / Active \  ------(6)--------->
         \Endpoint/  <-----(7)----------
         /
        / (9 CONNECTED)
/Event\
\Queue/

```
Connections require the use of both passive and active endpoints.
In order to establish a connection, an application must first create a
passive endpoint and associate it with an event queue. The event queue
will be used to report the connection management events. The application
then calls listen on the passive endpoint. A single passive endpoint can
be used to form multiple connections.

The connecting peer allocates an active endpoint, which is also
associated with an event queue. Connect is called on the active
endpoint, which results in sending a connection request (CONNREQ)
message to the passive endpoint. The CONNREQ event is inserted into
the passive endpoint’s event queue, where the listening application can
process it.

Upon processing the CONNREQ, the listening application will allocate
an active endpoint to use with the connection. The active endpoint is
bound with an event queue. Although the diagram shows the use of a
separate event queue, the active endpoint may use the same event queue
as used by the passive endpoint. Accept is called on the active endpoint
to finish forming the connection. It should be noted that the OFI accept
call is different than the accept call used by sockets. The differences
result from OFI supporting process direct I/O.

libfabric does not define the connection establishment protocol, but
does support a traditional three-way handshake used by many technologies.
After calling accept, a response is sent to the connecting active endpoint.
That response generates a CONNECTED event on the remote event queue. If a
three-way handshake is used, the remote endpoint will generate an
acknowledgment message that will generate a CONNECTED event for the accepting
endpoint. Regardless of the connection protocol, both the active and passive
sides of the connection will receive a CONNECTED event that signals that the
connection has been established.

## Connectionless Communications

Connectionless communication allows data transfers between active endpoints
without going through a connection setup process. The diagram below shows
the basic components needed to setup connection-less communication.
Connectionless communication setup differs from UDP sockets in that it
requires that the remote addresses be stored with libfabric.

```
  1 insert_addr()              2 send()
         |                        |
     /Address\ <--3 lookup--> / Active \
     \Vector /                \Endpoint/

```
libfabric requires the addresses of peer endpoints be inserted into a local
addressing table, or address vector, before data transfers can be initiated
against the remote endpoint. Address vectors abstract fabric specific
addressing requirements and avoid long queuing delays on data transfers
when address resolution is needed. For example, IP addresses may need to be
resolved into Ethernet MAC addresses. Address vectors allow this resolution
to occur during application initialization time. libfabric does not define
how an address vector be implemented, only its conceptual model.

All connection-less endpoints that transfer data must be associated with an
address vector.

# Endpoints

At a low-level, endpoints are usually associated with a transmit context, or
queue, and a receive context, or queue.  Although the terms transmit and
receive queues are easier to understand, libfabric uses the terminology
context, since queue like behavior of acting as a FIFO (first-in, first-out)
is not guaranteed.  Transmit and receive contexts may be implemented using
hardware queues mapped directly into the process’s address space.  An endpoint
may be configured only to transmit or receive data.  Data transfer requests
are converted by the underlying provider into commands that are inserted into
hardware transmit and/or receive contexts.

Endpoints are also associated with completion queues. Completion queues are
used to report the completion of asynchronous data transfer operations.

## Shared Contexts

An advanced usage model allows for sharing resources among multiple endpoints.
The most common form of sharing is having multiple connected endpoints
make use of a single receive context.  This can reduce receive side buffering
requirements, allowing the number of connected endpoints that an application
can manage to scale to larger numbers.

# Data Transfers

Obviously, a primary goal of network communication is to transfer data between
processes running on different systems. In a similar way that the socket API
defines different data transfer semantics for TCP versus UDP sockets, that is,
streaming versus datagram messages, libfabric defines different types of data
transfers. However, unlike sockets, libfabric allows different semantics over
a single endpoint, even when communicating with the same peer.

libfabric uses separate API sets for the different data transfer semantics;
although, there are strong similarities between the API sets.  The differences
are the result of the parameters needed to invoke each type of data transfer.

## Message transfers

Message transfers are most similar to UDP datagram transfers, except that
transfers may be sent and received reliably.  Message transfers may also be
gigabytes in size, depending on the provider implementation.  The sender
requests that data be transferred as a single transport operation to a peer.
Even if the data is referenced using an I/O vector, it is treated as a single
logical unit or message.  The data is placed into a waiting receive buffer
at the peer, with the receive buffer usually chosen using FIFO ordering.
Note that even though receive buffers are selected using FIFO ordering, the
received messages may complete out of order.  This can occur as a result of
data between and within messages taking different paths through the network,
handling lost or retransmitted packets, etc.

Message transfers are usually invoked using API calls that contain the string
"send" or "recv".  As a result they may be referred to simply as sent or
received messages.

Message transfers involve the target process posting memory buffers to the
receive (Rx) context of its endpoint.  When a message arrives from the network,
a receive buffer is removed from the Rx context, and the data is copied from
the network into the receive buffer.  Messages are matched with posted receives
in the order that they are received.  Note that this may differ from the order
that messages are sent, depending on the transmit side's ordering semantics.

Conceptually, on the transmit side, messages are posted to a transmit (Tx)
context.  The network processes messages from the Tx context, packetizing
the data into outbound messages.  Although many implementations process the
Tx context in order (i.e. the Tx context is a true queue), ordering guarantees
specified through the libfabric API determine the actual processing order.  As
a general rule, the more relaxed an application is on its message and data
ordering, the more optimizations the networking software and hardware can
leverage, providing better performance.

## Tagged messages

Tagged messages are similar to message transfers except that the messages
carry one additional piece of information, a message tag.  Tags are application
defined values that are part of the message transfer protocol and are used to
route packets at the receiver.  At a high level, they are roughly similar to
message ids.  The difference is that tag values are set by the application,
may be any value, and duplicate tag values are allowed.

Each sent message carries a single tag value, which is used to select a receive
buffer into which the data is copied.  On the receiving side, message buffers
are also marked with a tag.  Messages that arrive from the network search
through the posted receive messages until a matching tag is found.

Tags are often used to identify virtual communication groups or roles.
In practice, message tags are typically divided into fields.  For example, the
upper 16 bits of the tag may indicate a virtual group, with the lower 16 bits
identifying the message purpose.  The tag message interface in libfabric is
designed around this usage model.  Each sent message carries exactly one tag
value, specified through the API.  At the receiver, buffers are associated
with both a tag value and a mask.  The mask is used as part of the buffer
matching process.  The mask is applied against the received tag value carried
in the sent message prior to checking the tag against the receive buffer.  For
example, the mask may indicate to ignore the lower 16-bits of a tag.  If
the resulting values match, then the tags are said to match.  The received
data is then placed into the matched buffer.

For performance reasons, the mask is specified as 'ignore' bits. Although
this is backwards from how many developers think of a mask (where the bits
that are valid would be set to 1), the definition ends up mapping well with
applications.  The actual operation performed when matching tags is:

```
send_tag | ignore == recv_tag | ignore

/* this is equivalent to:
 * send_tag & ~ignore == recv_tag & ~ignore
 */
```

Tagged messages are equivalent of message transfers if a single tag value is
used.  But tagged messages require that the receiver perform a matching
operation at the target, which can impact performance versus untagged messages.

## RMA

RMA operations are architected such that they can require no processing by the
CPU at the RMA target.  NICs which offload transport functionality can perform
RMA operations without impacting host processing.  RMA write operations transmit
data from the initiator to the target.  The memory location where the data
should be written is carried within the transport message itself, with
verification checks at the target to prevent invalid access.

RMA read operations fetch data from the target system and transfer it back to
the initiator of the request, where it is placed into memory.  This too can be
done without involving the host processor at the target system when the NIC
supports transport offloading.

The advantage of RMA operations is that they decouple the processing of the
peers.  Data can be placed or fetched whenever the initiator is ready without
necessarily impacting the peer process.

Because RMA operations allow a peer to directly access the memory of a process,
additional protection mechanisms are used to prevent unintentional or unwanted
access.  RMA memory that is updated by a write operation or is fetched by a read
operation must be registered for access with the correct permissions specified.

## Atomic operations

Atomic transfers are used to read and update data located in remote memory
regions in an atomic fashion. Conceptually, they are similar to local atomic
operations of a similar nature (e.g. atomic increment, compare and swap, etc.).
The benefit of atomic operations is they enable offloading basic arithmetic
capabilities onto a NIC.  Unlike other data transfer operations, which merely
need to transfer bytes of data, atomics require knowledge of the format of
the data being accessed.

A single atomic function operates across an array of data, applying an atomic
operation to each entry.  The atomicity of an operation is limited to a single
data type or entry, however, not across the entire array.  libfabric defines a
wide variety of atomic operations across all common data types.  However
support for a given operation is dependent on the provider implementation.

## Collective operations

In general, collective operations can be thought of as coordinated atomic
operations between a set of peer endpoints, almost like a multicast
atomic request.  A single collective operation can result in data being
collected from multiple peers, combined using a set of atomic primitives,
and the results distributed to all peers.   A collective operation is a
group communication exchange.  It involves multiple peers exchanging data
with other peers participating in the collective call.  Collective operations
require close coordination by all participating members, and
collective calls can strain the fabric, as well as local and remote data
buffers.

Collective operations are an area of heavy research, with dedicated libraries
focused almost exclusively on implementing collective operations efficiently.
Such libraries are a specific target of libfabric.  The main object of
the libfabric collection APIs is to expose network acceleration features
for implementing collectives to higher-level libraries and applications.
It is recommended that applications needing collective communication target
higher-level libraries, such as MPI, instead of using libfabric collective
APIs for that purpose.
