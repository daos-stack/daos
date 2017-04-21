# Introduction

The emergence of data-intensive applications in business, government, and academia is stretching existing I/O models beyond its capabilities. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable, large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared-nothing interface from the ground up and be able to support massively-distributed storage for which failure will be the norm, while preserving low latency and high-bandwidth access over the fabric. This document presents the design of a complete I/O architecture called Distributed Asynchronous Object Storage, which aggregates persistent memory, distributed across the fabric into globally-accessible object address spaces, providing consistency, availability, and resiliency guarantees without compromising performance.

The proposed architecture leverages and extends the concepts developed under the DOE Exascale Fast Forward Storage & I/O program, but does not actually reuse any of the layers designed as part of this project. In particular, the new DAOS design is not based on any existing parallel file systems and will be developed from the ground up.

This high-level design document is organized as follows:

- <a href="../src/client/model.md">DAOS Storage Model</a>: Describes the DAOS storage model and architecture. 
- <a href="../src/client/layering.md">DAOS Storage Stack</a>: Presents the storage stack organization and functionality of each layer. 
- <a href="use_cases.md">Use Cases</a>: Provides a list of use cases.
- The remaining sections (Versioning Object Store, Pool Service, Container Service, Key Array Object, POSIX Emulation) focus on the internal design of each stack layer, including data structure and algorithmic choices.

## Terminology

	
	
	
	
	
|Acronym|Expansion |
|---|---|
|DAOS| Distributed Asynchronous Object Storage |
|DAOS-M/DSM|DAOS Persistent Memory Storage layer  |
|DAOS-SR/DSR| DAOS Sharding and Resilience layer |
|DAOS-P/DSP|DAOS POSIX layer
|FWK|Full-weight kernel|	
|HCE|Highest committed epoch|
|HDD|Hard Disk Drive|
|KV|Key – Value|
|LHE|Lowest held epoch|
|LRE|Lowest referenced epoch|
|MTBF|Mean Time Between Failures|
|NVML|Non-Volatile Memory Libraries|
|OFI|Open Fabrics Interface|
|PM/PMEM|Persistent Memory|
|RAS|Reliability, Availability & Serviceability|
|RDMA/RMA	|Remote (Direct) Memory Access|
|SPDK|Storage Performance Development Kit|
|SSD|Solid State Drive|
|UUID|Universal Unique Identifier|
|OIT|Object Index Table|
|OSIT|Object Shard Index Table|
|RDG|Redundancy Group|
