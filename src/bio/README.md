# Blob I/O

The Blob I/O (BIO) module was implemented for issuing I/O over NVMe SSDs. The DAOS service has two tiers of storage: Storage Class Memory (SCM) for byte-granular application data and metadata, and NVMe for bulk application data. Similar to how PMDK is currently used to facilitate access to SCM, the Storage Performance Development Kit (SPDK) is used to provide seamless and efficient
access to NVMe SSDs. The current DAOS storage model involves three DAOS server xstreams per core, along with one main DAOS server xstream per core mapped to an NVMe SSD device. DAOS storage allocations can occur on either SCM by using a PMDK pmemobj pool, or on NVMe, using an SPDK blob. All local server metadata will be stored in a per-server pmemobj pool on SCM and will include all current and relevant NVMe device, pool, and xstream mapping information.

This document contains the following sections:

- <a href="#1">Storage Performance Development Kit (SPDK)</a>
    - <a href="#2">SPDK Integration</a>
    - <a href="#3">Per-Server Metadata Management (SMD)</a>
    - <a href="#4">DMA Buffer Management</a>
- <a href="#5">NVMe Threading Model</a>
- <a href="#6">Faulty Device Detection</a>
    - <a href="#7">Device Owner XStream</a>
    - <a href="#8">Blobstore Health Data</a>
- <a href="#9">Device Monitoring</a>
    - <a href="#10">Blobstore/Device State Transitions</a>
    - <a href="#11">NVMe Faulty Reaction Callback</a>
- <a href="#12">Useful Admin DMG Commands</a>


<a id="1"></a>
## Storage Performance Development Kit (SPDK)
SPDK is an open source C library that when used in a storage application, can provide a significant performance increase of more than 7X over the standard NVMe kernel driver. SPDK's high performance can mainly be attributed to the user space NVMe driver, eliminating all syscalls and enabling zero-copy access from the application. In SPDK, the hardware is polled for completions as opposed to relying on interrupts, lowering both total latency and latency variance. SPDK also offers a block device layer called bdev which sits immediately above the device drivers like in a traditional kernel storage stack. This module offers pluggable module APIs for implementing block devices that interface with different types of block storage devices. This includes driver modules for NVMe, Malloc (ramdisk), Linux AIO, Ceph RBD, and others.

![/doc/graph/Fig_065.png](/doc/graph/Fig_065.png "SPDK Software Stack")

#### SPDK NVMe Driver
The NVMe driver is a C library linked to a storage application providing direct, zero-copy data transfer to and from NVMe SSDs. Other benefits of the SPDK NVMe driver are that it is entirely in user space, operates in polled-mode vs. interrupt-dependent, is asynchronous and lock-less.
#### SPDK Block Device Layer (bdev)
The bdev directory contains a block device abstraction layer used to translate from a common block protocol to specific protocols of backend devices, such as NVMe. Additionally, this layer provides automatic queuing of I/O requests in response to certain conditions, lock-less sending of queues, device configuration and reset support, and I/O timeout trafficking.
#### SPDK Blobstore
The blobstore is a block allocator for a higher-level storage service. The allocated blocks are termed 'blobs' within SPDK. Blobs are designed to be large (at least hundreds of KB), and therefore another allocator is needed in addition to the blobstore to provide efficient small block allocation for the DAOS service. The blobstore provides asynchronous, un-cached, and parallel blob read and write interfaces

<a id="2"></a>
## SPDK Integration
The BIO module relies on the SPDK API to initialize/finalize the SPDK environment on the DAOS server start/shutdown. The DAOS storage model is integrated with SPDK by the following:

* Management of SPDK blobstores and blobs:
NVMe SSDs are assigned to each DAOS server xstream. SPDK blobstores are created on each NVMe SSD. SPDK blobs are created and attached to each per-xstream VOS pool.
* Association of SPDK I/O channels with DAOS server xstreams:
Once SPDK I/O channels are properly associated to the corresponding device, NVMe hardware completion pollers are integrated into server polling ULTs.

<a id="3"></a>
## Per-Server Metadata Management (SMD)
   One of the major subcomponenets of the BIO module is per-server metadata management. The SMD submodule consists of a PMDK pmemobj pool stored on SCM used to track each DAOS server's local metadata.

   Currently, the persistent metadata tables tracked are :
  - **NVMe Device Table**: NVMe SSD to DAOS server xstream mapping (local PCIe attached NVMe SSDs are assigned to different server xstreams to avoid hardware contention)
  - **NVMe Pool Table**: NVMe SSD, DAOS server xstream, and SPDK blob ID mapping (SPDK blob to VOS pool:xstream mapping)
  - **NVMe Device State Table**: NVMe SSD to device state mapping (supported states are NORMAL or FAULTY)
   On DAOS server start, these tables are loaded from persistent memory and used to initialize new, and load any previous blobstores and blobs. Also, there is potential to expand this module to support other non-NVMe related metadata in the future.

   Useful Admin Commands to Query SMD:
   <a href="#31">dmg storage query smd</a> [used to query both SMD device table and pool table]

<a id="4"></a>
## DMA Buffer Management
  BIO internally manages a per-xstream DMA safe buffer for SPDK DMA transfer over NVMe SSDs. The buffer is allocated using the SPDK memory allocation API and can dynamically grow on demand. This buffer also acts as an intermediate buffer for RDMA over NVMe SSDs, meaning on DAOS bulk update, client data will be RDMA transferred to this buffer first, then the SPDK blob I/O interface will be called to start local DMA transfer from the buffer directly to NVMe SSD. On DAOS bulk fetch, data present on the NVMe SSD will be DMA transferred to this buffer first, and then RDMA transferred to the client.

<a id="5"></a>
## NVMe Threading Model
![/doc/graph/NVME_Threading_Model_with_FDD](/doc/graph/NVME_Threading_Model_with_FDD.PNG "NVMe Threading Model")
The items in orange highlighted in the threading model image above represent all updates made for the Faulty Device Detection feature.

<a id="6"></a>
## Faulty Device Detection
<a id="7"></a>
#### Device Owner XStream
In the case there is no direct 1:1 mapping of VOS XSream to NVMe SSD, the VOS xstream that first opens the SPDK blobstore will be named the 'Device Owner'. The Device Owner Xstream is responsible for maintaining and updating the blobstore health data (see below), handling device state transitions, and also media error events. All non-owner xstreams will forward events to the device owner.
<a id="8"></a>
#### Blobstore Health Data
In-memory (DRAM) data maintained by the Device Owner Xstream consisting of the following:
  - Subset of raw NVMe SSD health stats queried via SPDK Admin APIs
  - I/O error counters (Read, Write, Unmap)
  - Checksum Error Counter (currently not implemented)

 Useful Admin Commands to Query Device Health Data:
  - <a href="#81">dmg storage query blobstore-health</a> [used to query in-memory blobstore health data]
  - <a href="#82">dmg storage scan --nvme-health</a> [used to query raw SPDK SSD health stats]

<a id="9"></a>
## Device Monitoring
The current NVMe monitoring period for event polling is set at 60 seconds.
  - Blobstore State Transitions
  - Collect/Update Blobstore Health Data
  - Faulty Device Auto Detetection (currently not implemented)

<a id="10"></a>
## Blobstore/Device State Transitions
![/doc/graph/Blobstore_State_Transitions.PNG](/doc/graph/Blobstore_State_Transitions.PNG "Device State Transitions")

**Blobstore:**
  - NORMAL: healthy & functional
  - FAULTY: meets certain faulty criteria
  - TEARDOWN: affected targets are marked as DOWN
  - OUT: blobstore is torn down
  - REPLACED: new device is hot-plugged, initialize new blobstore and blobs (not implemented)
  - REINT: affected targets are in REINT state (not implemented)

**Per-Server Metadata (SMD):**
  - NORMAL
  - FAULTY

 Useful Admin Commands to Query & Set Device State:
   - <a href="#111">dmg storage query device-state</a> [used to query SMD persistent device state]
   - <a href="#112">dmg storage set nvme-faulty</a> [used to manually set the device state to FAULTY, triggers faulty reaction callback]


<a id="11"></a>
## NVMe Faulty Reaction Callback
1. Blobstore state transition: NORMAL->FAULTY
2. SMD device state updated to FAULTY (persistent state)
3. Exclude RPC sent to all targets in UP/UPINT
4. Rebuild triggered
5. Targets in DOWN or DOWNOUT (not necessary for rebuild to be complete before blobstore transition)
6. Blobstore state transition: FAULTY->TEARDOWN
7. Blobstore torn down
8. Blobstore state transition: TEARDOWN->OUT

<a id="12"></a>
## Useful Admin DMG Commands:
<a id="82"></a>
- Query NVMe SSD Health Stats: **$dmg storage scan --nvme-health**
```
$dmg storage scan --nvme-health -l=boro-11:10001
boro-11:10001: connected
boro-11:10001
        NVMe controllers and namespaces detail with health statistics:
                PCI:0000:81:00.0 Model:INTEL SSDPEDKE020T7  FW:QDV10130 Socket:1 Capacity:1.95TB
                Health Stats:
                        Temperature:288K(15C)
                        Controller Busy Time:5h26m0s
                        Power Cycles:4
                        Power On Duration:16488h0m0s
                        Unsafe Shutdowns:2
                        Media Errors:0
                        Error Log Entries:0
                        Critical Warnings:
                                Temperature: OK
                                Available Spare: OK
                                Device Reliability: OK
                                Read Only: OK
                                Volatile Memory Backup: OK
```
<a id="31"></a>
- Query Per-Server Metadata (SMD): **$dmg storage query smd**
```
$dmg storage query smd --devices --pools -l=boro-11:10001
boro-11:10001: connected
SMD Device List:
boro-11:10001:
        Device:
                UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
                VOS Target IDs: 0 1 2 3
SMD Pool List:
boro-11:10001:
        Pool:
                UUID: 01b41f76-a783-462f-bbd2-eb27c2f7e326
                VOS Target IDs: 0 1 3 2
                SPDK Blobs: 4294967404 4294967405 4294967407 4294967406
```

<a id="81"></a>
- Query Blobstore Health Data: **$dmg storage query blobstore-health**
```
$dmg storage query blobstore-health --devuuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19 -l=boro-11:10001
boro-11:10001: connected
Blobstore Health Data:
boro-11:10001:
        Device UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
        Read errors: 0
        Write errors: 0
        Unmap errors: 0
        Checksum errors: 0
        Device Health:
                Error log entries: 0
                Media errors: 0
                Temperature: 289
                Temperature: OK
                Available Spare: OK
                Device Reliability: OK
                Read Only: OK
                Volatile Memory Backup: OK
```
<a id="111"></a>
- Query Persistent Device State: **$dmg storage query device-state**
```
$dmg storage query device-state --devuuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19 -l=boro-11:10001
boro-11:10001: connected
Device State Info:
boro-11:10001:
        Device UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
        State: NORMAL
```
<a id="112"></a>
- Manually Set Device State to FAULTY: **$dmg storage set nvme-faulty**
```
$dmg storage set nvme-faulty --devuuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19 -l=boro-11:10001
boro-11:10001: connected
Device State Info:
boro-11:10001:
        Device UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
        State: FAULTY
```
