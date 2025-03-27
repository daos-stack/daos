---
layout: page
title: fi_intro(7)
tagline: Libfabric Programmer's Guide - Introduction
---
{% include JB/setup %}

# NAME

fi_intro \- libfabric introduction

# OVERVIEW

This introduction is part of the libfabric's programmer's guide.  See
[`fi_guide`(7)](fi_guide.7.html).  This section provides insight
into the motivation for the libfabric design and underlying networking
features that are being exposed through the API.

# Review of Sockets Communication

The sockets API is a widely used networking API.  This guide assumes that a
reader has a working knowledge of programming to sockets.  It makes reference
to socket based communications throughout in an effort to help explain
libfabric concepts and how they relate or differ from the socket API. To be
clear, the intent of this guide is not to criticize the socket API, but
reference sockets as a starting point in order to explain certain network
features or limitations.  The following sections provide a high-level overview
of socket semantics for reference.

## Connected (TCP) Communication

The most widely used type of socket is SOCK_STREAM.  This sort of socket
usually runs over TCP/IP, and as a result is often referred to as a 'TCP'
socket.  TCP sockets are connection-oriented, requiring an explicit
connection setup before data transfers can occur.  A single TCP socket can only
transfer data to a single peer socket.  Communicating with multiple peers
requires the use of one socket per peer.

Applications using TCP sockets are typically labeled as either a client or
server.  Server applications listen for connection requests, and accept them
when they occur.  Clients, on the other hand, initiate connections to the
server.  In socket API terms, a server calls listen(), and the client calls
connect().  After a connection has been established, data transfers between
a client and server are similar.  The following code segments highlight the
general flow for a sample client and server.  Error handling and some
subtleties of the socket API are omitted for brevity.

```
/* Example server code flow to initiate listen */
struct addrinfo *ai, hints;
int listen_fd;

memset(&hints, 0, sizeof hints);
hints.ai_socktype = SOCK_STREAM;
hints.ai_flags = AI_PASSIVE;
getaddrinfo(NULL, "7471", &hints, &ai);

listen_fd = socket(ai->ai_family, SOCK_STREAM, 0);
bind(listen_fd, ai->ai_addr, ai->ai_addrlen);
freeaddrinfo(ai);

fcntl(listen_fd, F_SETFL, O_NONBLOCK);
listen(listen_fd, 128);
```

In this example, the server will listen for connection requests on port 7471
across all addresses in the system.  The call to getaddrinfo() is used to form
the local socket address.  The node parameter is set to NULL, which result in
a wild card IP address being returned.  The port is hard-coded to 7471.  The
AI_PASSIVE flag signifies that the address will be used by the listening side
of the connection.  That is, the address information should be relative to the
local node.

This example will work with both IPv4 and IPv6.  The getaddrinfo() call
abstracts the address format away from the server, improving its portability.
Using the data returned by getaddrinfo(), the server allocates a socket of
type SOCK_STREAM, and binds the socket to port 7471.

In practice, most enterprise-level applications make use of non-blocking
sockets.  This is needed for a single application thread to manage multiple
socket connections.  The fcntl() command sets the listening socket to
non-blocking mode.  This will affect how the server processes connection
requests (shown below).  Finally, the server starts listening for connection
requests by calling listen.  Until listen is called, connection requests
that arrive at the server will be rejected by the operating system.

```
/* Example client code flow to start connection */
struct addrinfo *ai, hints;
int client_fd;

memset(&hints, 0, sizeof hints);
hints.ai_socktype = SOCK_STREAM;
getaddrinfo("10.31.20.04", "7471", &hints, &ai);

client_fd = socket(ai->ai_family, SOCK_STREAM, 0);
fcntl(client_fd, F_SETFL, O_NONBLOCK);

connect(client_fd, ai->ai_addr, ai->ai_addrlen);
freeaddrinfo(ai);
```

Similar to the server, the client makes use of getaddrinfo().  Since the
AI_PASSIVE flag is not specified, the given address is treated as that of
the destination.  The client expects to reach the server at IP address
10.31.20.04, port 7471.  For this example the address is hard-coded into
the client.  More typically, the address will be given to the client via
the command line, through a configuration file, or from a service.
Often the port number will be well-known, and the client will find the
server by name, with DNS (domain name service) providing the name to address
resolution.  Fortunately, the getaddrinfo call can be used to convert
host names into IP addresses.

Whether the client is given the server's network address directly or a name
which must be translated into the network address, the mechanism used to
provide this information to the client varies widely.  A simple mechanism
that is commonly used is for users to provide the server's address using a
command line option.  The problem of telling applications where its peers are
located increases significantly for applications that communicate with hundreds
to millions of peer processes, often requiring a separate, dedicated
application to solve.  For a typical client-server socket application, this
is not an issue, so we will defer more discussion until later.

Using the getaddrinfo() results, the client opens a socket, configures it for
non-blocking mode, and initiates the connection request.  At this point, the
network stack has sent a request to the server to establish the connection.
Because the socket has been set to non-blocking, the connect call will return
immediately and not wait for the connection to be established.  As a result
any attempt to send data at this point will likely fail.

```
/* Example server code flow to accept a connection */
struct pollfd fds;
int server_fd;

fds.fd = listen_fd;
fds.events = POLLIN;

poll(&fds, -1);

server_fd = accept(listen_fd, NULL, 0);
fcntl(server_fd, F_SETFL, O_NONBLOCK);
```

Applications that use non-blocking sockets use select(), poll(), or an
equivalent such as epoll() to receive notification of when a socket
is ready to send or receive data.  In this case, the server wishes to
know when the listening socket has a connection request to process.
It adds the listening socket to a poll set, then waits until a connection
request arrives (i.e. POLLIN is true).  The poll() call blocks until POLLIN
is set on the socket.  POLLIN indicates that the socket has data to accept.
Since this is a listening socket, the data is a connection request.  The
server accepts the request by calling accept().  That returns a new socket
to the server, which is ready for data transfers.  Finally, the server sets
the new socket to non-blocking mode.

```
/* Example client code flow to establish a connection */
struct pollfd fds;
int err;
socklen_t len;

fds.fd = client_fd;
fds.events = POLLOUT;

poll(&fds, -1);

len = sizeof err;
getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &err, &len);
```

The client is notified that its connection request has completed when its
connecting socket is 'ready to send data' (i.e. POLLOUT is true).  The poll()
call blocks until POLLOUT is set on the socket, indicating the connection
attempt is done.  Note that the connection request may have completed with
an error, and the client still needs to check if the connection attempt was
successful.  That is not conveyed to the application by the poll() call.
The getsockopt() call is used to retrieve the result of the connection attempt.
If err in this example is set to 0, then the connection attempt succeeded.
The socket is now ready to send and receive data.

After a connection has been established, the process of sending or receiving
data is the same for both the client and server.  The examples below differ
only by name of the socket variable used by the client or server application.

```
/* Example of client sending data to server */
struct pollfd fds;
size_t offset, size, ret;
char buf[4096];

fds.fd = client_fd;
fds.events = POLLOUT;

size = sizeof(buf);
for (offset = 0; offset < size; ) {
    poll(&fds, -1);

    ret = send(client_fd, buf + offset, size - offset, 0);
    offset += ret;
}
```

Network communication involves buffering of data at both the sending and
receiving sides of the connection. TCP uses a credit based scheme to manage
flow control to ensure that there is sufficient buffer space at the receive
side of a connection to accept incoming data.  This flow control is hidden
from the application by the socket API.  As a result, stream based sockets
may not transfer all the data that the application requests to send as part
of a single operation.

In this example, the client maintains an offset into the buffer that it
wishes to send.  As data is accepted by the network, the offset increases.
The client then waits until the network is ready to accept more data before
attempting another transfer.  The poll() operation supports this.  When the
client socket is ready for data, it sets POLLOUT to true.  This indicates
that send will transfer some additional amount of data.  The client issues
a send() request for the remaining amount of buffer that it wishes to transfer.
If send() transfers less data than requested, the client updates the offset,
waits for the network to become ready, then tries again.

```
/* Example of server receiving data from client */
struct pollfd fds;
size_t offset, size, ret;
char buf[4096];

fds.fd = server_fd;
fds.events = POLLIN;

size = sizeof(buf);
for (offset = 0; offset < size; ) {
    poll(&fds, -1);

    ret = recv(client_fd, buf + offset, size - offset, 0);
    offset += ret;
}
```

The flow for receiving data is similar to that used to send it.  Because of
the streaming nature of the socket, there is no guarantee that the receiver
will obtain all of the available data as part of a single call.  The server
instead must wait until the socket is ready to receive data (POLLIN), before
calling receive to obtain what data is available.  In this example, the
server knows to expect exactly 4 KB of data from the client.  More generally,
a client and server will exchange communication protocol headers at the start
of all messages, and the header will include the size of the message.

It is worth noting that the previous two examples are written so that they
are simple to understand.  They are poorly constructed when considering
performance.  In both cases, the application always precedes a data transfer
call (send or recv) with poll().  The impact is even if the network is ready
to transfer data or has data queued for receiving, the application will always
experience the latency and processing overhead of poll().  A better approach
is to call send() or recv() prior to entering the for() loops, and only enter
the loops if needed.

## Connection-less (UDP) Communication

As mentioned, TCP sockets are connection-oriented.  They may be used to
communicate between exactly 2 processes.  For parallel applications that need
to communicate with thousands peer processes, the overhead of managing this
many simultaneous sockets can be significant, to the point where the
application performance may decrease as more processes are added.

To support communicating with a large number of peers, or for applications
that do not need the overhead of reliable communication, sockets offers
another commonly used socket option, SOCK_DGRAM.  Datagrams are unreliable,
connectionless messages.  The most common type of SOCK_DGRAM socket runs
over UDP/IP.  As a result, datagram sockets are often referred to as UDP
sockets.

UDP sockets use the same socket API as that described above for TCP
sockets; however, the communication behavior differs.  First, an application
using UDP sockets does not need to connect to a peer prior to sending
it a message.  The destination address is specified as part of the send
operation.  A second major difference is that the message is not guaranteed
to arrive at the peer.  Network congestion in switches, routers, or the
remote NIC can discard the message, and no attempt will be made to resend
the message.  The sender will not be notified that the message either arrived
or was dropped.  Another difference between TCP and UDP sockets is the
maximum size of the transfer that is allowed.  UDP sockets limit messages to
at most 64k, though in practice, applications use a much smaller size, usually
aligned to the network MTU size (for example, 1500 bytes).

Most use of UDP sockets replace the socket send() / recv() calls with sendto()
and recvfrom().

```
/* Example send to peer at given IP address and UDP port */
struct addrinfo *ai, hints;

memset(&hints, 0, sizeof hints);
hints.ai_socktype = SOCK_DGRAM;
getaddrinfo("10.31.20.04", "7471", &hints, &ai);

ret = sendto(client_fd, buf, size, 0, ai->ai_addr, ai->ai_addrlen);
```

In the above example, we use getadddrinfo() to convert the given IP address
and UDP port number into a sockaddr.  That is passed into the sendto() call
in order to specify the destination of the message.  Note the similarities
between this flow and the TCP socket flow.  The recvfrom() call allows us
to receive the address of the sender of a message.  Note that unlike streaming
sockets, the entire message is accepted by the network on success.  All
contents of the buf parameter, specified by the size parameter, have been
queued by the network layer.

Although not shown, the application could call poll() or an equivalent
prior to calling sendto() to ensure that the socket is ready to accept new
data.  Similarly, poll() may be used prior to calling recvfrom() to check
if there is data ready to be read from the socket.

```
/* Example receive a message from a peer */
struct sockaddr_in addr;
socklen_t addrlen;

addrlen = sizeof(addr);
ret = recvfrom(client_fd, buf, size, 0, &addr, &addrlen);
```

This example will receive any incoming message from any peer.  The address
of the peer will be provided in the addr parameter.  In this case, we only
provide enough space to record and IPv4 address (limited by our use of
struct sockaddr_in).  Supporting an IPv6 address would simply require passing
in a larger address buffer (mapped to struct sockaddr_in6 for example).

## Advantages

The socket API has two significant advantages.  First, it is available on a
wide variety of operating systems and platforms, and works over the vast
majority of available networking hardware.  It can even work for communication
between processes on the same system without any network hardware.  It is
easily the de-facto networking API.  This by itself makes it appealing to use.

The second key advantage is that it is relatively easy to program to.  The
importance of this should not be overlooked.  Networking APIs that offer
access to higher performing features, but are difficult to program to correctly
or well, often result in lower application performance.  This is not unlike
coding an application in a higher-level language such as C or C++, versus
assembly.  Although writing directly to assembly language offers the _promise_
of being better performing, for the vast majority of developers, their
applications will perform better if written in C or C++, and using an
optimized compiler.  Applications should have a clear need for high-performance
networking before selecting an alternative API to sockets.

## Disadvantages

When considering the problems with the socket API as it pertains to
high-performance networking, we limit our discussion to the two most common
sockets types: streaming (TCP) and datagram (UDP).

Most applications require that network data be sent reliably.  This invariably
means using a connection-oriented TCP socket.  TCP sockets transfer data as a
stream of bytes.  However, many applications operate on messages.  The result is
that applications often insert headers that are simply used to convert
application message to / from a byte stream.  These headers consume additional
network bandwidth and processing.  The streaming nature of the interface also
results in the application using loops as shown in the examples above to send
and receive larger messages.  The complexity of those loops can be significant
if the application is managing sockets to hundreds or thousands of peers.

Another issue highlighted by the above examples deals with the asynchronous
nature of network traffic.  When using a reliable transport, it is not enough
to place an application's data onto the network.  If the network is busy, it
could drop the packet, or the data could become corrupted during a transfer.
The data must be kept until it has been acknowledged by the peer, so that it
can be resent if needed.  The socket API is defined such that the application
owns the contents of its memory buffers after a socket call returns.

As an example, if we examine the socket send() call, once send() returns the
application is free to modify its buffer.  The network implementation has a
couple of options.  One option is for the send call to place the data directly
onto the network.  The call must then block before returning to the user until
the peer acknowledges that it received the data, at which point send() can
return.  The obvious problem with this approach is that the application is
blocked in the send() call until the network stack at the peer can process
the data and generate an acknowledgment.  This can be a significant amount
of time where the application is blocked and unable to process other work,
such as responding to messages from other clients.  Such an approach is not
feasible.

A better option is for the send() call to copy the application's data into an
internal buffer.  The data transfer is then issued out of that buffer, which
allows retrying the operation in case of a failure.  The send() call in this
case is not blocked, but all data that passes through the network will result
in a memory copy to a local buffer, even in the absence of any errors.

Allowing immediate re-use of a data buffer is a feature of the socket API that
keeps it simple and easy to program to.  However, such a feature can potentially
have a negative impact on network performance.  For network or memory limited
applications, or even applications concerned about power consumption, an
alternative API may be attractive.

A slightly more hidden problem occurs in the socket APIs designed for UDP
sockets.  This problem is an inefficiency in the implementation as a result
of the API design being designed for ease of use.  In order for the
application to send data to a peer, it needs to provide the IP address
and UDP port number of the peer.  That involves passing a sockaddr structure
to the sendto() and recvfrom() calls.  However, IP addresses are a higher-
level network layer address.  In order to transfer data between systems,
low-level link layer addresses are needed, for example Ethernet addresses.
The network layer must map IP addresses to Ethernet addresses on every send
operation.  When scaled to thousands of peers, that overhead on every send
call can be significant.

Finally, because the socket API is often considered in conjunction with TCP
and UDP protocols, it is intentionally detached from the underlying
network hardware implementation, including NICs, switches, and routers.
Access to available network features is therefore constrained by what the
API can support.

It is worth noting here, that some operating systems support enhanced
APIs that may be used to interact with TCP and UDP sockets.  For example,
Linux supports an interface known as io_uring, and Windows has an
asynchronous socket API.  Those APIs can help alleviate some of the
problems described above.  However, an application will still be restricted
by the features that the TCP an UDP protocols provide.

# High-Performance Networking

By analyzing the socket API in the context of high-performance networking,
we can start to see some features that are desirable for a network API.

## Avoiding Memory Copies

The socket API implementation usually results in data copies occurring at
both the sender and the receiver.  This is a trade-off between keeping the
interface easy to use, versus providing reliability.  Ideally, all memory
copies would be avoided when transferring data over the network.  There are
techniques and APIs that can be used to avoid memory copies, but in practice,
the cost of avoiding a copy can often be more than the copy itself, in
particular for small transfers (measured in bytes, versus kilobytes or more).

To avoid a memory copy at the sender, we need to place the application data
directly onto the network.  If we also want to avoid blocking the sending
application, we need some way for the network layer to communicate with the
application when the buffer is safe to re-use.  This would allow the original
buffer to be re-used in case the data needs to be re-transmitted.  This leads
us to crafting a network interface that behaves asynchronously.  The
application will need to issue a request, then receive some sort of
notification when the request has completed.

Avoiding a memory copy at the receiver is more challenging.  When data arrives
from the network, it needs to land into an available memory buffer, or it will
be dropped, resulting in the sender re-transmitting the data.  If we use socket
recv() semantics, the only way to avoid a copy at the receiver is for the recv()
to be called before the send().  Recv() would then need to block until the data
has arrived.  Not only does this block the receiver, it is impractical to use
outside of an application with a simple request-reply protocol.

Instead, what is needed is a way for the receiving application to provide one
or more buffers to the network for received data to land.  The network then
needs to notify the application when data is available.  This sort of mechanism
works well if the receiver does not care where in its memory space the data is
located; it only needs to be able to process the incoming message.

As an alternative, it is possible to reverse this flow, and have the network
layer hand its buffer to the application.  The application would then be
responsible for returning the buffer to the network layer when it is done
with its processing.  While this approach can avoid memory copies, it suffers
from a few drawbacks.  First, the network layer does not know what size of
messages to expect, which can lead to inefficient memory use.  Second, many
would consider this a more difficult programming model to use.  And finally,
the network buffers would need to be mapped into the application process'
memory space, which negatively impacts performance.

In addition to processing messages, some applications want to receive data and
store it in a specific location in memory.  For example, a database may want
to merge received data records into an existing table.  In such cases, even
if data arriving from the network goes directly into an application's receive
buffers, it may still need to be copied into its final location.  It would be
ideal if the network supported placing data that arrives from the network into
a specific memory buffer, with the buffer determined based on the contents
of the data.

### Network Buffers

Based on the problems described above, we can start to see that avoiding
memory copies depends upon the ownership of the memory buffers used for
network traffic.  With socket based transports, the network buffers are
owned and managed by the networking stack.  This is usually handled by
the operating system kernel.  However, this results in the data 'bouncing'
between the application buffers and the network buffers.  By putting the
application in control of managing the network buffers, we can avoid
this overhead.  The cost for doing so is additional complexity in
the application.

Note that even though we want the application to own the network buffers,
we would still like to avoid the situation where the application
implements a complex network protocol.  The trade-off is that the
app provides the data buffers to the network stack, but the network
stack continues to handle things like flow control, reliability,
and segmentation and reassembly.

### Resource Management

We define resource management to mean properly allocating network resources
in order to avoid overrunning data buffers or queues.  Flow control is a
common aspect of resource management.  Without proper flow control, a
sender can overrun a slow or busy receiver.  This can result in dropped
packets, re-transmissions, and increased network congestion.  Significant
research and development has gone into implementing flow control algorithms.
Because of its complexity, it is not something that an application developer
should need to deal with.  That said, there are some applications where
flow control simply falls out of the network protocol.  For example, a
request-reply protocol naturally has flow control built in.

For our purposes, we expand the definition of resource management beyond
flow control.  Flow control typically only deals with available network
buffering at a peer.  We also want to be concerned about having available
space in outbound data transfer queues.  That is, as we issue commands to
the local NIC to send data, that those commands can be queued at the NIC.
When we consider reliability, this means tracking outstanding requests
until they have been acknowledged.  Resource management will need to
ensure that we do not overflow that request queue.

Additionally, supporting asynchronous operations (described in detail
below) will introduce potential new queues.  Those queues also must not overflow.

## Asynchronous Operations

Arguably, the key feature of achieving high-performance is supporting
asynchronous operations, or the ability to overlap different communication and
communication with computation.  The socket API supports asynchronous
transfers with its non-blocking mode.  However, because the API itself
operates synchronously, the result is additional data copies.  For an
API to be asynchronous, an application needs to be able to submit work,
then later receive some sort of notification that the work is done.
In order to avoid extra memory copies, the application must agree not
to modify its data buffers until the operation completes.

There are two main ways to notify an application that it is safe to
re-use its data buffers.  One mechanism is for the network layer to
invoke some sort of callback or send a signal to the application that
the request is done.  Some asynchronous APIs use this mechanism.  The
drawback of this approach is that signals interrupt an application's
processing.  This can negatively impact the CPU caches, plus requires
interrupt processing.  Additionally, it is often difficult to develop
an application that can handle processing a signal that can occur at anytime.

An alternative mechanism for supporting asynchronous operations is to
write events into some sort of completion queue when an operation completes.
This provides a way to indicate to an application when a data transfer has
completed, plus gives the application control over when and how to process
completed requests.  For example, it can process requests in batches to
improve code locality and performance.

### Interrupts and Signals

Interrupts are a natural extension to supporting asynchronous operations.
However, when dealing with an asynchronous API, they can negatively impact
performance.  Interrupts, even when directed to a kernel agent, can
interfere with application processing.

If an application has an asynchronous interface with completed operations
written into a completion queue, it is often sufficient for the application
to simply check the queue for events.  As long as the application has other
work to perform, there is no need for it to block.  This alleviates the
need for interrupt generation.  A NIC merely needs to write an entry into
the completion queue and update a tail pointer to signal that a request is
done.

If we follow this argument, then it can be beneficial to give the application
control over when interrupts should occur and when to write events to some
sort of wait object.  By having the application notify the network layer
that it will wait until a completion occurs, we can better manage the
number and type of interrupts that are generated.

### Event Queues

As outlined above, there are performance advantages to having an API that
reports completions or provides other types of notification using an event
queue.  A very simple type of event queue merely tracks completed operations.
As data is received or a send completes, an entry is written into the event queue.

## Direct Hardware Access

When discussing the network layer, most software implementations refer to
kernel modules responsible for implementing the necessary transport and
network protocols.  However, if we want network latency to approach
sub-microsecond speeds, then we need to remove as much software between
the application and its access to the hardware as possible.  One way to
do this is for the application to have direct access to the network interface
controller's command queues.  Similarly, the NIC requires direct access to
the application's data buffers and control structures, such as the above
mentioned completion queues.

Note that when we speak about an application having direct access to network
hardware, we're referring to the application process.  Naturally, an application
developer is highly unlikely to code for a specific hardware NIC.  That work
would be left to some sort of network library specifically targeting the NIC.
The actual network layer, which implements the network transport, could be
part of the network library or offloaded onto the NIC's hardware or firmware.

### Kernel Bypass

Kernel bypass is a feature that allows the application to avoid calling into
the kernel for data transfer operations.  This is possible when it has direct
access to the NIC hardware.  Complete kernel bypass is impractical because of
security concerns and resource management constraints.  However, it is possible
to avoid kernel calls for what are called 'fast-path' operations, such as
send or receive.

For security and stability reasons, operating system kernels cannot rely on
data that comes from user space applications.  As a result, even a simple
kernel call often requires acquiring and releasing locks, coupled with
data verification checks.  If we can limit the effects of a poorly written
or malicious application to its own process space, we can avoid the overhead
that comes with kernel validation without impacting system stability.

### Direct Data Placement

Direct data placement means avoiding data copies when sending and receiving
data, plus placing received data into the correct memory buffer where needed.
On a broader scale, it is part of having direct hardware access, with the
application and NIC communicating directly with shared memory buffers and
queues.

Direct data placement is often thought of by those familiar with RDMA -
remote direct memory access.  RDMA is a technique that allows reading and
writing memory that belongs to a peer process that is running on a node
across the network.  Advanced RDMA hardware is capable of accessing the
target memory buffers without involving the execution of the peer process.
RDMA relies on offloading the network transport onto the NIC in order
to avoid interrupting the target process.

The main advantages of supporting direct data placement is avoiding
memory copies and minimizing processing overhead.

# Designing Interfaces for Performance

We want to design a network interface that can meet the requirements outlined
above.  Moreover, we also want to take into account the performance of the
interface itself.  It is often not obvious how an interface can adversely
affect performance, versus performance being a result of the underlying
implementation.  The following sections describe how interface choices
can impact performance.  Of course, when we begin defining the actual
APIs that an application will use, we will need to trade off raw performance
for ease of use where it makes sense.

When considering performance goals for an API, we need to take into account
the target application use cases.  For the purposes of this discussion, we
want to consider applications that communicate with thousands to millions of
peer processes.  Data transfers will include millions of small messages per
second per peer, and large transfers that may be up to gigabytes of data.
At such extreme scales, even small optimizations are measurable, in terms
of both performance and power.  If we have a million peers sending a millions
messages per second, eliminating even a single instruction from the code path
quickly multiplies to saving billions of instructions per second from the
overall execution, when viewing the operation of the entire application.

We once again refer to the socket API as part of this discussion in order
to illustrate how an API can affect performance.

```
/* Notable socket function prototypes */
/* "control" functions */
int socket(int domain, int type, int protocol);
int bind(int socket, const struct sockaddr *addr, socklen_t addrlen);
int listen(int socket, int backlog);
int accept(int socket, struct sockaddr *addr, socklen_t *addrlen);
int connect(int socket, const struct sockaddr *addr, socklen_t addrlen);
int shutdown(int socket, int how);
int close(int socket);

/* "fast path" data operations - send only (receive calls not shown) */
ssize_t send(int socket, const void *buf, size_t len, int flags);
ssize_t sendto(int socket, const void *buf, size_t len, int flags,
    const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t sendmsg(int socket, const struct msghdr *msg, int flags);
ssize_t write(int socket, const void *buf, size_t count);
ssize_t writev(int socket, const struct iovec *iov, int iovcnt);

/* "indirect" data operations */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int select(int nfds, fd_set *readfds, fd_set *writefds,
    fd_set *exceptfds, struct timeval *timeout);
```

Examining this list, there are a couple of features to note.  First, there
are multiple calls that can be used to send data, as well as multiple calls
that can be used to wait for a non-blocking socket to become ready.  This
will be discussed in more detail further on.  Second, the operations have
been split into different groups (terminology is ours).  Control operations
are those functions that an application seldom invokes during execution.
They often only occur as part of initialization.

Data operations, on the other hand, may be called hundreds to millions of
times during an application's lifetime.  They deal directly or indirectly
with transferring or receiving data over the network.  Data operations
can be split into two groups.  Fast path calls interact with the network
stack to immediately send or receive data.  In order to achieve high
bandwidth and low latency, those operations need to be as fast as possible.
Non-fast path operations that still deal with data transfers are those calls,
that while still frequently called by the application, are not as
performance critical.  For example, the select() and poll() calls are
used to block an application thread until a socket becomes ready.
Because those calls suspend the thread execution, performance is a
lesser concern.  (Performance of those operations is still of a concern,
but the cost of executing the operating system scheduler often swamps
any but the most substantial performance gains.)

## Call Setup Costs

The amount of work that an application needs to perform before issuing
a data transfer operation can affect performance, especially message rates.
Obviously, the more parameters an application must push on the stack
to call a function increases its instruction count.  However, replacing
stack variables with a single data structure does not help to
reduce the setup costs.

Suppose that an application wishes to send a single data buffer of a
given size to a peer.  If we examine the socket API, the best fit for
such an operation is the write() call.  That call takes only those
values which are necessary to perform the data transfer.  The send()
call is a close second, and send() is a more natural function name for
network communication, but send() requires one extra argument over write().
Other functions are even worse in terms of setup costs.  The sendmsg()
function, for example, requires that the application format a data structure,
the address of which is passed into the call.  This requires significantly
more instructions from the application if done for every data transfer.

Even though all other send functions can be replaced by sendmsg(), it
is useful to have multiple ways for the application to issue send requests.
Not only are the other calls easier to read and use (which lower software
maintenance costs), but they can also improve performance.

## Branches and Loops

When designing an API, developers rarely consider how the API impacts
the underlying implementation.  However, the selection of API parameters
can require that the underlying implementation add branches or use control
loops.  Consider the difference between the write() and writev() calls.
The latter passes in an array of I/O vectors, which may be processed
using a loop such as this:

```
/* Sample implementation for processing an array */
for (i = 0; i < iovcnt; i++) {
    ...
}
```

In order to process the iovec array, the natural software construct
would be to use a loop to iterate over the entries.  Loops result in
additional processing.  Typically, a loop requires initializing a
loop control variable (e.g. i = 0), adds ALU operations (e.g. i++),
and a comparison (e.g. i < iovcnt).  This overhead is necessary to
handle an arbitrary number of iovec entries.  If the common case is
that the application wants to send a single data buffer, write()
is a better option.

In addition to control loops, an API can result in the implementation
needing branches.  Branches can change the execution flow of a program,
impacting processor pipe-lining techniques.  Processor branch
prediction helps alleviate this issue.  However, while branch
prediction can be correct nearly 100% of the time while running a
micro-benchmark, such as a network bandwidth or latency test, with
more realistic network traffic, the impact can become measurable.

We can easily see how an API can introduce branches into the code flow
if we examine the send() call.  Send() takes an extra flags parameter
over the write() call.  This allows the application to modify the behavior
of send().  From the viewpoint of implementing send(), the flags parameter
must be checked.  In the best case, this adds one additional check
(flags are non-zero).  In the worst case, every valid flag may need
a separate check, resulting in potentially dozens of checks.

Overall, the sockets API is well designed considering these performance
implications.  It provides complex calls where they are needed, with
simpler functions available that can avoid some of the overhead inherent
in other calls.

## Command Formatting

The ultimate objective of invoking a network function is to transfer
or receive data from the network.  In this section, we're dropping to
the very bottom of the software stack to the component responsible for
directly accessing the hardware.  This is usually referred to as the
network driver, and its implementation is often tied to a specific
piece of hardware, or a series of NICs by a single hardware vendor.

In order to signal a NIC that it should read a memory buffer and copy
that data onto the network, the software driver usually needs to write
some sort of command to the NIC.  To limit hardware complexity and
cost, a NIC may only support a couple of command formats.  This differs
from the software interfaces that we've been discussing, where we
can have different APIs of varying complexity in order to reduce overhead.
There can be significant costs associated with formatting the command
and posting it to the hardware.

With a standard NIC, the command is formatted by a kernel driver.
That driver sits at the bottom of the network stack servicing
requests from multiple applications.  It must typically format
each command only after a request has passed through the network
stack.

With devices that are directly accessible by a single application,
there are opportunities to use pre-formatted command structures.
The more of the command that can be initialized prior to the application
submitting a network request, the more streamlined the process, and the
better the performance.

As an example, a NIC needs to have the destination address as part of
a send operation.  If an application is sending to a single peer, that
information can be cached and be part of a pre-formatted network header.
This is only possible if the NIC driver knows that the destination will not
change between sends.  The closer that the driver can be to the application,
the greater the chance for optimization.  An optimal approach is for the
driver to be part of a library that executes entirely within the application
 process space.

## Memory Footprint

Memory footprint concerns are most notable among high-performance
computing (HPC) applications that communicate with thousands of peers.
Excessive memory consumption impacts application scalability, limiting
the number of peers that can operate in parallel to solve problems.
There is often a trade-off between minimizing the memory footprint
needed for network communication, application performance, and ease
of use of the network interface.

As we discussed with the socket API semantics, part of the ease of using
sockets comes from the network layering copying the user's buffer into an
internal buffer belonging to the network stack.  The amount of internal
buffering that's made available to the application directly correlates
with the bandwidth that an application can achieve.  In general, larger
internal buffering increases network performance, with a cost of increasing
the memory footprint consumed by the application.  This memory footprint
exists independent of the amount of memory allocated directly by the
application.  Eliminating network buffering not only helps with performance,
but also scalability, by reducing the memory footprint needed to support
the application.

While network memory buffering increases as an application scales, it can
often be configured to a fixed size.  The amount of buffering needed is
dependent on the number of active communication streams being used at
any one time.  That number is often significantly lower than the total
number of peers that an application may need to communicate with.  The
amount of memory required to _address_ the peers, however, usually has
a linear relationship with the total number of peers.

With the socket API, each peer is identified using a struct sockaddr.
If we consider a UDP based socket application using IPv4 addresses, a
peer is identified by the following address.

```
/* IPv4 socket address - with typedefs removed */
struct sockaddr_in {
    uint16_t sin_family; /* AF_INET */
    uint16_t sin_port;
    struct {
        uint32_t sin_addr;
    } in_addr;
};
```

In total, the application requires 8-bytes of addressing for each peer.
If the app communicates with a million peers, that explodes to roughly
8 MB of memory space that is consumed just to maintain the address list.
If IPv6 addressing is needed, then the requirement increases by a factor of 4.

Luckily, there are some tricks that can be used to help reduce the
addressing memory footprint, though doing so will introduce more instructions
into code path to access the network stack.  For instance, we can notice that
all addresses in the above example have the same sin_family value (AF_INET).
There's no need to store that for each address.  This potentially shrinks each
address from 8 bytes to 6.  (We may be left with unaligned data, but that's a
trade-off to reducing the memory consumption).  Depending on how the addresses
are assigned, further reduction may be possible.  For example, if the
application uses the same set of port addresses at each node, then we
can eliminate storing the port, and instead calculate it from some base
value.  This type of trick can be applied to the IP portion of the address
if the app is lucky enough to run across sequential IP addresses.

The main issue with this sort of address reduction is that it is difficult
to achieve.  It requires that each application check for and handle address
compression, exposing the application to the addressing format used by the
networking stack.  It should be kept in mind that TCP/IP and UDP/IP addresses
are logical addresses, not physical.  When running over Ethernet, the
addresses that appear at the link layer are MAC addresses, not IP addresses.
The IP to MAC address association is managed by the network software.  We
would like to provide addressing that is simple for an application to use,
but at the same time can provide a minimal memory footprint.

## Communication Resources

We need to take a brief detour in the discussion in order to delve deeper
into the network problem and solution space.  Instead of continuing to
think of a socket as a single entity, with both send and receive capabilities,
we want to consider its components separately. A network socket can be viewed
as three basic constructs: a transport level address, a send or transmit
queue, and a receive queue.  Because our discussion will begin to pivot
away from pure socket semantics, we will refer to our network 'socket'
as an endpoint.

In order to reduce an application's memory footprint, we need to consider
features that fall outside of the socket API.  So far, much of the discussion
has been around sending data to a peer.  We now want to focus on the best
mechanisms for receiving data.

With sockets, when an app has data to receive (indicated, for example, by a
POLLIN event), we call recv().  The network stack copies the receive data
into its buffer and returns.  If we want to avoid the data copy on the receive
side, we need a way for the application to post its buffers to the network
stack _before_ data arrives.

Arguably, a natural way of extending the socket API to support this feature
is to have each call to recv() simply post the buffer to the network layer.
As data is received, the receive buffers are removed in the order that they
were posted.  Data is copied into the posted buffer and returned to the user.
It would be noted that the size of the posted receive buffer may be larger
(or smaller) than the amount of data received.  If the available buffer space
is larger, hypothetically, the network layer could wait a short amount of
time to see if more data arrives.  If nothing more arrives, the receive
completes with the buffer returned to the application.

This raises an issue regarding how to handle buffering on the receive side.
So far, with sockets we've mostly considered a streaming protocol.  However,
many applications deal with messages which end up being layered over the
data stream.  If they send an 8 KB message, they want the receiver to receive
an 8 KB message.  Message boundaries need to be maintained.

If an application sends and receives a fixed sized message, buffer allocation
becomes trivial.  The app can post X number of buffers each of an optimal size.
However, if there is a wide mix in message sizes, difficulties arise.  It is not
uncommon for an app to have 80% of its messages be a couple hundred of bytes or
less, but 80% of the total data that it sends to be in large transfers that are,
say, a megabyte or more.  Pre-posting receive buffers in such a situation
is challenging.

A commonly used technique used to handle this situation is to implement one
application level protocol for smaller messages, and use a separate protocol
for transfers that are larger than some given threshold.  This would allow an
application to post a bunch of smaller messages, say 4 KB, to receive data.
For transfers that are larger than 4 KB, a different communication protocol
is used, possibly over a different socket or endpoint.

### Shared Receive Queues

If an application pre-posts receive buffers to a network queue, it needs to
balance the size of each buffer posted, the number of buffers that are posted
to each queue, and the number of queues that are in use.  With a socket like
approach, each socket would maintain an independent receive queue where data
is placed.  If an application is using 1000 endpoints and posts 100 buffers,
each 4 KB, that results in 400 MB of memory space being consumed to receive
data.  (We can start to realize that by eliminating memory copies, one of
the trade offs is increased memory consumption.)  While 400 MB seems like a
lot of memory, there is less than half a megabyte allocated to a single
receive queue.  At today's networking speeds, that amount of space can be
consumed within milliseconds.  The result is that if only a few endpoints
are in use, the application will experience long delays where flow control
will kick in and back the transfers off.

There are a couple of observations that we can make here.  The first is that
in order to achieve high scalability, we need to move away from a
connection-oriented protocol, such as streaming sockets.  Secondly, we need
to reduce the number of receive queues that an application uses.

A shared receive queue is a network queue that can receive data for many
different endpoints at once.  With shared receive queues, we no longer
associate a receive queue with a specific transport address.  Instead
network data will target a specific endpoint address.  As data arrives,
the endpoint will remove an entry from the shared receive queue, place
the data into the application's posted buffer, and return it to the user.
Shared receive queues can greatly reduce the amount of buffer space needed
by an applications.  In the previous example, if a shared receive queue
were used, the app could post 10 times the number of buffers (1000 total),
yet still consume 100 times less memory (4 MB total).  This is far more
scalable.  The drawback is that the application must now be aware of
receive queues and shared receive queues, rather than considering the
network only at the level of a socket.

### Multi-Receive Buffers

Shared receive queues greatly improve application scalability; however,
it still results in some inefficiencies as defined so far.  We've only
considered the case of posting a series of fixed sized memory buffers
to the receive queue.  As mentioned, determining the size of each
buffer is challenging.  Transfers larger than the fixed size require
using some other protocol in order to complete.  If transfers are
typically much smaller than the fixed size, then the extra buffer
space goes unused.

Again referring to our example, if the application posts 1000 buffers,
then it can only receive 1000 messages before the queue is emptied.
At data rates measured in millions of messages per second, this will
introduce stalls in the data stream.  An obvious solution is to increase
the number of buffers posted.  The problem is dealing with variable sized
messages, including some which are only a couple hundred bytes in length.
For example, if the average message size in our case is 256 bytes or less,
then even though we've allocated 4 MB of buffer space, we only make use of
6% of that space.  The rest is wasted in order to handle messages which
may only occasionally be up to 4 KB.

A second optimization that we can make is to fill up each posted receive
buffer as messages arrive.  So, instead of a 4 KB buffer being removed
from use as soon as a single 256 byte message arrives, it can instead
receive up to 16, 256 byte, messages.  We refer to such a feature as
'multi-receive' buffers.

With multi-receive buffers, instead of posting a bunch of smaller buffers,
we instead post a single larger buffer, say the entire 4 MB, at once.
As data is received, it is placed into the posted buffer.  Unlike TCP
streams, we still maintain message boundaries.  The advantages here are
twofold.  Not only is memory used more efficiently, allowing us to receive
more smaller messages at once and larger messages overall, but we reduce
the number of function calls that the application must make to maintain
its supply of available receive buffers.

When combined with shared receive queues, multi-receive buffers
help support optimal receive side buffering and processing.  The
main drawback to supporting multi-receive buffers are that the
application will not necessarily know up front how many messages
may be associated with a single posted memory buffer.  This is
rarely a problem for applications.

## Optimal Hardware Allocation

As part of scalability considerations, we not only need to consider the
processing and memory resources of the host system, but also the allocation
and use of the NIC hardware.  We've referred to network endpoints as
combination of transport addressing, transmit queues, and receive queues.
The latter two queues are often implemented as hardware command queues.
Command queues are used to signal the NIC to perform some sort of work.
A transmit queue indicates that the NIC should transfer data.  A transmit
command often contains information such as the address of the buffer to
transmit, the length of the buffer, and destination addressing data.
The actual format and data contents vary based on the hardware implementation.

NICs have limited resources.  Only the most scalable, high-performance
applications likely need to be concerned with utilizing NIC hardware optimally.
 However, such applications are an important and specific focus of libfabric.
 Managing NIC resources is often handled by a resource manager application,
 which is responsible for allocating systems to competing applications,
 among other activities.

Supporting applications that wish to make optimal use of hardware requires
that hardware related abstractions be exposed to the application.  Such
abstractions cannot require a specific hardware implementation, and care
must be taken to ensure that the resulting API is still usable by developers
unfamiliar with dealing with such low level details.  Exposing concepts such
as shared receive queues is an example of giving an application more control
over how hardware resources are used.

### Sharing Command Queues

By exposing the transmit and receive queues to the application, we open the
possibility for the application that makes use of multiple endpoints to
determine how those queues might be shared.  We talked about the benefits
of sharing a receive queue among endpoints.  The benefits of sharing transmit
queues are not as obvious.

An application that uses more addressable endpoints than there are transmit
queues will need to share transmit queues among the endpoints.  By controlling
which endpoint uses which transmit queue, the application can prioritize traffic.
A transmit queue can also be configured to optimize for a specific type of
data transfer, such as large transfers only.

From the perspective of a software API, sharing transmit or receive queues
implies exposing those constructs to the application, and allowing them to be
associated with different endpoint addresses.

### Multiple Queues

The opposite of a shared command queue are endpoints that have multiple queues.
An application that can take advantage of multiple transmit or receive queues
can increase parallel handling of messages without synchronization constraints.
Being able to use multiple command queues through a single endpoint has
advantages  over using multiple endpoints.  Multiple endpoints require
separate addresses, which increases memory use.  A single endpoint with
multiple queues can continue to expose a single address, while taking
full advantage of available NIC resources.

## Progress Model Considerations

One aspect of the sockets programming interface that developers often
don't consider is the location of the protocol implementation.  This is
usually managed by the operating system kernel.  The network stack is
responsible for handling flow control messages, timing out transfers,
re-transmitting unacknowledged transfers, processing received data,
and sending acknowledgments.  This processing requires that the network
stack consume CPU cycles.  Portions of that processing can be done within
the context of the application thread, but much must be handled by kernel
threads dedicated to network processing.

By moving the network processing directly into the application process,
we need to be concerned with how network communication makes forward progress.
For example, how and when are acknowledgments sent?  How are timeouts and
message re-transmissions handled?  The progress model defines this behavior,
and it depends on how much of the network processing has been offloaded
onto the NIC.

More generally, progress is the ability of the underlying network
implementation to complete processing of an asynchronous request.  In
many cases, the processing of an asynchronous request requires the use
of the host processor.  For performance reasons, it may be undesirable
for the provider to allocate a thread for this purpose, which will compete
with the application thread(s).  We can avoid thread context switches if
the application thread can be used to make forward progress on requests --
check for acknowledgments, retry timed out operations, etc.  Doing so
requires that the application periodically call into the network stack.

## Ordering

Network ordering is a complex subject.  With TCP sockets, data is sent and
received in the same order.  Buffers are re-usable by the application
immediately upon returning from a function call.  As a result, ordering is
simple to understand and use.  UDP sockets complicate things slightly.  With
UDP sockets, messages may be received out of order from how they were sent.
In practice, this often doesn't occur, particularly, if the application only
communicates over a local area network, such as Ethernet.

With our evolving network API, there are situations where exposing different
order semantics can improve performance.  These details will be discussed
further below.

### Messages

UDP sockets allow messages to arrive out of order because each message is
routed from the sender to the receiver independently.  This allows packets
to take different network paths, to avoid congestion or take advantage of
multiple network links for improved bandwidth.  We would like to take advantage
of the same features in those cases where the application doesn't care in
which order messages arrive.

Unlike UDP sockets, however, our definition of message ordering is more subtle.
UDP messages are small, MTU sized packets.  In our case, messages may be
gigabytes in size.  We define message ordering to indicate whether the start
of each message is processed in order or out of order.  This is related to,
but separate from the order of how the message payload is received.

An example will help clarify this distinction.  Suppose that an application
has posted two messages to its receive queue.  The first receive points to a
4 KB buffer.  The second receive points to a 64 KB buffer.  The sender will
transmit a 4 KB message followed by a 64 KB message.  If messages are processed
in order, then the 4 KB send will match with the 4 KB received, and the 64 KB
send will match with the 64 KB receive.  However, if messages can be processed
out of order, then the sends and receives can mismatch, resulting in the 64 KB
send being truncated.

In this example, we're not concerned with what order the data is received in.
The 64 KB send could be broken in 64 1-KB transfers that take different
routes to the destination.  So, bytes 2k-3k could be received before bytes
1k-2k.  Message ordering is not concerned with ordering _within_ a message,
only _between_ messages.  With ordered messages, the messages themselves
need to be processed in order.

The more relaxed message ordering can be the more optimizations that the
network stack can use to transfer the data.  However, the application must
be aware of message ordering semantics, and be able to select the desired
semantic for its needs.  For the purposes of this section, messages refers
to transport level operations, which includes RDMA and similar operations
(some of which have not yet been discussed).

### Data

Data ordering refers to the receiving and placement of data both
_within and between_ messages.  Data ordering is most important
to messages that can update the same target memory buffer.  For
example, imagine an application that writes a series of database
records directly into a peer memory location.  Data ordering,
combined with message ordering, ensures that the data from the
second write updates memory after the first write completes.
The result is that the memory location will contain the records
carried in the second write.

Enforcing data ordering between messages requires that the messages
themselves be ordered.  Data ordering can also apply within a single
message, though this level of ordering is usually less important to
applications.  Intra-message data ordering indicates that the data for a
single message is received in order.  Some applications use this feature
to 'spin' reading the last byte of a receive buffer.  Once the byte changes,
the application knows that the operation has completed and all earlier
data has been received.  (Note that while such behavior is interesting
for benchmark purposes, using such a feature in this way is strongly
discouraged.  It is not portable between networks or platforms.)

### Completions

Completion ordering refers to the sequence that asynchronous operations
report their completion to the application.  Typically, unreliable data
transfer will naturally complete in the order that they are submitted
to a transmit queue.  Each operation is transmitted to the network,
with the completion occurring immediately after.  For reliable data
transfers, an operation cannot complete until it has been acknowledged
by the peer.  Since ack packets can be lost or possibly take different
paths through the network, operations can be marked as completed out of
order.  Out of order acks is more likely if messages can be processed
out of order.

Asynchronous interfaces require that the application track their
outstanding requests.  Handling out of order completions can increase
application complexity, but it does allow for optimizing network
utilization.

# lifabric Architecture

Libfabric is well architected to support the previously discussed features.
For further information on the libfabric architecture, see
the next programmer's guide section: [`fi_arch`(7)](fi_arch.7.html).
