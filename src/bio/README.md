# Blob I/O

The Blob I/O (BIO) module was implemented for issuing I/O over NVMe SSDs. The DAOS service has two tiers of storage: Storage Class Memory (SCM) for byte-granular application data and metadata, and NVMe for bulk application data. Similar to how PMDK is currently used to faciliate access to SCM, the Storage Performance Development Kit (SPDK) is used to provide seamless and efficient
access to NVMe SSDs. The current DAOS storage model involves three DAOS server xstreams per core, along with one main DAOS server xstream per core mapped to an NVMe SSD device. DAOS storage allocations can occur on either SCM by using a PMDK pmemobj pool, or on NVMe, using an SPDK blob. All local server metadata will be stored in a per-server pmemobj pool on SCM and will include all current and relevant NVMe device, pool, and xstream mapping information.

This document contains the following sections:

## Storage Performance Development Kit (SPDK)
SPDK is an open source C library that when used in a storage application, can provide a significant performance increase of more than 7X over the standard NVMe kernel driver. SPDK's high performance can mainly be attributed to the user space NVMe driver, eliminating all syscalls and enabling zero-copy access from the application. In SPDK, the hardware is polled for completions as opposed to relying on interrupts, lowering both total latency and latency variance. SPDK also offers a block device layer called bdev which sits immediately above the device drivers like in a traditional kernel storage stack. This module offers pluggable module APIs for implementing block devices that interface with different types of block storage devices. This includes driver modules for NVMe, Malloc (ramdisk), Linux AIO, Ceph RBD, and others.

![/doc/graph/Fig_065.png](/doc/graph/Fig_065.png "SPDK Software Stack")

#### SPDK NVMe Driver
The NVMe driver is a C library linked to a storage application providing direct, zero-copy data transfer to and from NVMe SSDs. Other benefits of the SPDK NVMe driver are that it is entirely in user space, operates in polled-mode vs. interrupt-dependent, is asynchronous and lock-less.
#### SPDK Block Device Layer (bdev)
The bdev directory contains a block device abstraction layer used to translate from a common block protocol to specific protocols of backend devices, such as NVMe. Additionally, this layer provides automatic queuing of I/O requests in response to certain conditions, lock-less sending of queues, device configuration and reset support, and I/O timeout trafficking.
#### SPDK Blobstore
The blobstore is a block allocator for a higher-level storage service. The allocated blocks are termed 'blobs' within SPDK. Blobs are designed to be large (at least hundreds of KB), and therefore another allocator is needed in addition to the blobstore to provide efficient small block allocation for the DAOS service. The blobstore provides asynchronous, un-cached, and parallel blob read and write interfaces

## SPDK Integration
The BIO module relies on the SPDK API to initialize/finalize the SPDK environment on the DAOS server start/shutdown. The DAOS storage model is integrated with SPDK by the following:
<ol>
<li>Management of SPDK blobstores and blobs:
NVMe SSDs are assigned to each DAOS server xstream. SPDK blobstores are created on each NVMe SSD. SPDK blobs are created and attached to each per-xstream VOS pool.</li>
<li>Association of SPDK I/O channels with DAOS server xstreams:
Once SPDK I/O channels are properly associated to the corresponding device, NVMe hardware completion pollers are integrated into server polling ULTs.</li>

## Per-Server Metadata Management (SMD)
One of the major subcomponenets of the BIO module is per-server metadata management. The SMD submodule consists of a PMDK pmemobj pool stored on SCM used to track each DAOS server's local metadata.

Currently, the persistent metadata tables tracked are:
- **NVMe Stream Table**: NVMe SSD to DAOS server xstream mapping (local PCIe attached NVMe SSDs are assigned to different server xstreams to avoid hardware contention)
- **NVMe Pool Table**: NVMe SSD, DAOS server xstream, and SPDK blob ID mapping (SPDK blob to VOS pool:xstream mapping)
- **NVMe Device Table**: (in progress) NVMe SSD to device status mapping
On DAOS server start, these tables are loaded from persistent memory and used to initialize new, and load any previous blobstores and blobs. Alos, there is potential to expand this module to support other non-NVMe related metadata in the future.

## DMA Buffer Management
BIO internally manages a per-xstream DMA safe buffer for SPDK DMA transfer over NVMe SSDs. The buffer is allocated using the SPDK memory allocation API and can dynamically grow on demand. This buffer also acts as an intermediate buffer for RDMA over NVMe SSDs, meaning on DAOS bulk update, client data will be RDMA transferred to this buffer first, then the SPDK blob I/O interface will be called to start local DMA transfer from the buffer directly to NVMe SSD. On DAOS bulk fetch, data present on the NVMe SSD will be DMA transferred to this buffer first, and then RDMA transferred to the client.
