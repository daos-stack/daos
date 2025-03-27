Introduction
============

\section Motivation
A communication middleware abstracts the vendor-specific software and hardware
interfaces.
They bridge the semantic and functionality gap between the programming models
and the software and hardware network interfaces by providing
data transfer interfaces and implementation, optimized protocols for data
transfer between various memories, and managing network resources. There are many
communication middleware APIs and libraries to support parallel programming
models such as MPI, OpenSHMEM, and task-based models.

Current communication middleware designs typically take two approaches. First,
communication middleware such as Intel's PSM (previously Qlogic), Mellanox's
MXM, and IBM's PAMI provide high-performance implementations for specific
network hardware. Second, communication middleware such as VMI, Cactus, ARMCI,
GASNet, and Open MPI are tightly coupled to a specific programming model.
Communication middleware designed with either of this design approach
requires significant porting effort to move a new network
interface or programming model.

To achieve functional and performance portability across
architectures and programming models, we introduce Unified Communication X
(UCX).

\section UCX
Unified Communication X (UCX) is a set of network APIs and their
implementations for high throughput computing. UCX is a combined
effort of national laboratories, industry, and academia to design and
implement a high-performing and highly-scalable network stack for next
generation applications and systems. UCX design provides the ability to
tailor its APIs and network functionality to suit a wide variety of
application domains.
We envision that these APIs will satisfy the networking needs of many
programming models such as the Message Passing Interface (MPI), OpenSHMEM,
Partitioned Global Address Space (PGAS) languages, task-based paradigms, and
I/O bound applications.

The initial focus is on supporting semantics such as point-to-point
communications (one-sided and two-sided), collective communication,
and remote atomic operations required for popular parallel programming models.
Also, the initial UCX reference implementation
is targeted to support current network technologies such as:
+ Open Fabrics - InfiniBand (Mellanox, Qlogic, IBM), iWARP, RoCE
+ Cray uGNI - GEMINI and ARIES interconnects
+ Shared memory (MMAP, Posix, CMA, KNEM, XPMEM, etc.)
+ Ethernet (TCP/UDP)


UCX design goals are focused on performance and scalability, while efficiently supporting
popular and emerging programming models.

UCX's API and design do not impose architectural constraints on the network hardware
nor require any specific capabilities to the support the programming model functionality.
This is achieved by keeping the API flexible and ability to support the missing
functionality efficiently in the software.


Extreme scalability is an important design goal for UCX.
To achieve this, UCX follows these design principles:
+ Minimal memory consumption : Design avoids data-structures that scale with the number of
processing   elements (i.e., order N data structures), and share resources among multiple
programming models.
+ Low-latency Interfaces: Design provides at least two sets of APIs with one set focused on the performance,
and the other focused on functionality.
+ High bandwidth - With minimal software overhead combined and support for multi-rail and multi-device
 capabilities, the design provides all the hooks that are necessary for exploiting hardware bandwidth
capabilities.
+ Asynchronous Progress: API provides non-blocking communication interfaces and design supports asynchronous progress
required for communication and computation overlap
+ Resilience - the API exposes communication control hooks required for fault
tolerant communication library implementation.

UCX design provides native support for hybrid programming models. The
design enables resource sharing, optimal memory usage, and progress engine
coordination to efficiently implement hybrid programming models. For example,
hybrid applications that use both OpenSHMEM and MPI programming models will be
able to select between a single-shared UCX network context or a stand
alone UCX network context for each one of them. Such flexibility,
optimized resource sharing, and reduced memory consumption, improve network
and application performance.
