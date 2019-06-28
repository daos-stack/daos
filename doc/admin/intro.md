# Introduction

The Distributed Asynchronous Object Storage (DAOS) is an open-source
object store designed from the ground up for massively distributed Non
Volatile Memory (NVM). DAOS takes advantage of next-generation NVM
technology, like Storage Class Memory (SCM) and NVM express (NVMe),
while presenting a key-value storage interface on top of commodity
hardware that provides features, such as, transactional non-blocking
I/O, advanced data protection with self-healing, end-to-end data
integrity, fine-grained data control, and elastic storage, to optimize
performance and cost.

This initial administration guide version is associated with DAOS v0.4.

## Terms used in this Document 

The following terms and abbreviations are used in this document.

|Term|Definition|
|----|----|
|ACLs|Access Control Lists|
|CaRT|Collective and RPC Transport (CaRT) library. A software library built on top of the Mercury Function Shipping library to support distributed communication functionality.|
|CGO|Go tools that enable creation of Go packages that call C code|
|CN|Compute Node|
|CPU|Central Processing Unit|
|COTS|Commercial off-the-shelf|
|Daemon|A process offering system-level resources.|
|DCPM|Intel Optane DC Persistent Memory|
|DPDK|Data Plane Development Kit|
|dRPC|DAOS Remote Procedure Call|
|BIO|Blob I/O|
|gRPC|gRPC[^1] Remote Procedure Calls|
|URT|A common library of Gurt Useful Routines and Types provided with CaRT.|
|HLD|High-Level Design|
|I/O|Input/Output|
|ISA-L|Intel® Intelligent Storage Acceleration Library|
|libfabric|A user-space library that exports the Open Fabrics Interface|
|Mercury|A user-space RPC library that can use libfabrics as a transport|
|NVM|Non-Volatile Memory|
|NVMe|Non-Volatile Memory express|
|OFI|OpenFabrics Interfaces|
|OS|Operating System|
|PMDK|Persistent Memory Development Kit|
|PMIx|Process Management Interface for Exascale|
|Raft|Raft is a consensus algorithm used to distribute state transitions among DAOS server nodes.|
|RDB|Replicated Database, containing pool metadata and maintained across DAOS servers using the Raft algorithm.|
|RPC|Remote Procedure Call.|
|SPDK|Storage Performance Development Kit|
|SWIM|Scalable Weakly-consistent Infection-style process group Membership protocol|
|UPI|Intel® Ultra Path Interconnect|
|UUID|Universal Unique Identifier|
|VOS|Versioned Object Store|
               
## Additional Documentation

Refer to the following documentation for architecture and description:

|Document|Location|
|----|----|
|DAOS Internals       |https://github.com/daos-stack/daos/blob/master/src/README.md
|DAOS Storage Model   |<https://github.com/daos-stack/daos/blob/master/doc/storage_model.md>
|Community Roadmap    |https://wiki.hpdd.intel.com/display/DC/Roadmap
                           

[^1]: <https://grpc.io/>
