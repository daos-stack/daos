Design
======

The UCX framework consists of the three main components: UC-Services (UCS),
UC-Transports (UCT), and UC-Protocols (UCP). Each one of these components
exports a public API, and can be used as a stand-alone library.

\image latex Architecture.pdf "UCX Framework Architecture"
\image html  Architecture.png "UCX Framework Architecture"

\section UCS
UCS is a service layer that provides the necessary functionality for
implementing portable and efficient utilities. This layer includes the
following services:
+ an abstraction for accessing platform specific functionality (atomic operations, thread safety, etc.),
+ tools for efficient memory management (memory pools, memory allocators, and memory allocators hooks),
+ commonly used data structures (hashes, trees, lists).

\section UCT
UCT is a transport layer that abstracts
the differences across various hardware architectures and provides a
low-level API that enables the implementation of communication protocols.
The primary goal of the layer is to provide direct and efficient access to
hardware network functionality. For this purpose,
UCT relies on vendor provided low-level drivers such as uGNI, Verbs,
shared memory, ROCM, CUDA. In addition, the layer provides
constructs for communication context management (thread-based and application level), and
allocation and management of device-specific memories including those found
in accelerators. In terms of communication APIs, UCT defines interfaces for
immediate (short), buffered copy-and-send (bcopy), and zero-copy (zcopy)
communication operations.

\b Short: This type of operation is optimized for small messages that can be posted and completed
in place @anchor uct_short_protocol_desc.

\b Bcopy: This type of operation is optimized for medium size messages that are typically sent through a
so-called bouncing-buffer. This auxiliary buffer is typically allocated given network constraints and ready for
immediate utilization by the hardware. Since a custom data packing routine could  be provided, this method
can be used for non-contiguous i/o @anchor uct_bcopy_protocol_desc.

\b Zcopy: This type of operation exposes zero-copy memory-to-memory communication semantics, which means that
message is sent directly from user buffer, or received directly to user buffer, without being copied between
the network layers @anchor uct_zcopy_protocol_desc.

\section UCP
UCP implements higher-level protocols that are typically used by message passing (MPI)
and PGAS programming models by using lower-level capabilities exposed
through the UCT layer. UCP is provides the following functionality: ability to select different transports for
communication, message fragmentation, multi-rail communication, and initializing and finalizing
the library.
Currently, the API has the following classes of interfaces:
Initialization, Remote Memory Access (RMA) communication, Atomic Memory
Operations (AMO), Active Message, Tag-Matching, and Collectives.

\b Initialization: This subset of interfaces defines the communication
context setup, queries the network capabilities, and initializes the local
communication endpoints. The context represented by the UCX context is an
abstraction of the network transport resources. The communication endpoint
setup interfaces initialize the UCP endpoint, which is an abstraction of all
the necessary resources associated with a particular connection. The
communication endpoints are used as input to all communication operations to
describe the source and destination of the communication.

\b RMA: This subset of interfaces defines one-sided communication operations such as PUT and
GET, required for implementing low overhead, direct memory access communications
constructs needed by both distributed and shared memory
programming models. UCP includes a separate set of interfaces for
communicating non-contiguous data. This functionality was included to
support various programming models' communication requirements and leverage
the scatter/gather capabilities of modern network hardware.

\b AMO: This subset of interfaces provides support for atomically performing operations
on the remote memory, an important class of operations for PGAS
programming models, particularly OpenSHMEM.

\b Tag \b Matching: This interface supports tag-matching for send-receive semantics which is a key
communication semantic defined by the MPI specification.

\b Stream : The API provides order and reliable communication semantics.
Data is treated as an ordered sequence of bytes pushed through the connection.
In contrast of tag-matching interface, the size of each individual send does
not necessarily have to match the size of each individual receive, as long as
the total number of bytes is the same. This API is designed to match widely
used BSD-socket based programming models.

\b Active \b Message: A subset of functionality where the incoming packet invokes a
sender-specified callback in order to be processed by the receiving process.
As an example, the two-sided MPI interface can easily be implemented on top
of such a concept (TBD: cite openmpi ). However, these interfaces are more general and
suited for other programming paradigms where the receiver process does not
prepost receives, but expects to react to incoming packets directly. Like
RMA and tag-matching interfaces, the active message interface provides
separate APIs for different message types and non-contiguous data.

\b Collectives: This subset of interfaces defines group communication and
synchronization operations. The collective operations include barrier,
all-to-one, all-to-all, and reduction operations. When possible, we will
take advantage of hardware acceleration for collectives
(e.g., InfiniBand Switch collective acceleration).

