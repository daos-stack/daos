# DAOS BIO (Blob I/O) Threading Model - Detailed Explanation

## Overview

The BIO module implements a sophisticated threading model built on top of SPDK (Storage Performance Development Kit) to manage NVMe SSD access efficiently. The threading model revolves around three key concepts: **xstreams**, **SPDK threads**, and specialized roles assigned to specific xstreams.

## Core Components

### 1. **bio_xs_context (Xstream Context)**

The `bio_xs_context` structure represents a per-xstream (execution stream) context for NVMe operations:

```c
struct bio_xs_context {
    int                  bxc_tgt_id;         // Target ID for this xstream
    uint64_t             bxc_io_monitor_ts;  // IO monitoring timestamp
    struct spdk_thread  *bxc_thread;         // Associated SPDK thread
    struct bio_xs_blobstore *bxc_xs_blobstores[SMD_DEV_TYPE_MAX];
    struct bio_dma_buffer *bxc_dma_buf;      // DMA buffer for this xstream
    unsigned int         bxc_self_polling:1;  // Standalone VOS mode
    unsigned int         bxc_skip_draining:1; // Skip event draining on exit
};
```

**Key Points:**
- Each DAOS target (VOS engine) has its own `bio_xs_context`
- One-to-one mapping with SPDK threads for lock-free operation
- Contains per-xstream resources (DMA buffers, blobstore references)
- Manages I/O channels to SPDK blobstores

### 2. **SPDK Thread Integration**

SPDK provides a lightweight, user-space threading framework:

```c
struct spdk_thread *bxc_thread;  // Created with spdk_thread_create()
```

**SPDK Thread Characteristics:**
- **User-space threads**: Not OS threads, lightweight message-passing framework
- **Event-driven**: Based on polling and message queues
- **Lock-free**: Each thread has its own message ring
- **Polled execution**: Must be actively polled to process events via `spdk_thread_poll()`

**Thread Creation:**
```c
// In bio_xsctxt_alloc()
snprintf(th_name, sizeof(th_name), "daos_spdk_%d", tgt_id);
ctxt->bxc_thread = spdk_thread_create((const char *)th_name, NULL);
spdk_set_thread(ctxt->bxc_thread);  // Set as current thread context
```

### 3. **bio_blobstore (Shared Blobstore Context)**

Represents a SPDK blobstore (NVMe SSD) that can be shared across multiple xstreams:

```c
struct bio_blobstore {
    ABT_mutex            bb_mutex;
    ABT_cond             bb_barrier;
    struct spdk_blob_store *bb_bs;
    struct bio_xs_context  *bb_owner_xs;      // Device owner xstream
    struct bio_xs_context **bb_xs_ctxts;      // All xstreams using this
    int                     bb_ref;            // Reference count
    enum bio_bs_state       bb_state;         // Device state
    // ... health monitoring, device info, etc.
};
```

**Key Points:**
- One blobstore per NVMe SSD
- Can be accessed by multiple xstreams concurrently
- Protected by Argobots (ABT) mutex for shared data
- Has a designated "owner" xstream for management

## Special Xstream Roles

### 1. **Init Xstream** (First Started Xstream)

**Identification:**
```c
inline bool is_init_xstream(struct bio_xs_context *ctxt) {
    return ctxt->bxc_thread == nvme_glb.bd_init_thread;
}
```

**Responsibilities:**
- **SPDK Subsystem Initialization**: First xstream initializes all SPDK subsystems
- **Blobstore Discovery**: Scans and creates all blobstores on server startup
- **Device Hotplug Management**: Registers and handles hotplug poller
- **LED Management**: All VMD LED operations execute on init xstream
- **Device List Maintenance**: Tracks all available devices (bd_bdevs list)

**Code Flow:**
```c
// In bio_xsctxt_alloc()
if (nvme_glb.bd_init_thread == NULL) {
    // This is the init xstream
    nvme_glb.bd_init_thread = ctxt->bxc_thread;
    nvme_glb.bd_init_xs = ctxt;
    
    // Initialize SPDK subsystems
    rc = init_bio_bdevs(ctxt);
    
    // Register hotplug poller
    // ...
}
```

**Why Init Xstream is Special:**
- SPDK requires a single thread to initialize subsystems
- Hotplug detection needs centralized management
- VMD operations must execute in a consistent context

### 2. **Device Owner Xstream** (Per-Blobstore)

**Identification:**
```c
inline bool is_bbs_owner(struct bio_xs_context *ctxt, struct bio_blobstore *bbs) {
    return bbs->bb_owner_xs == ctxt;
}
```

**Responsibilities:**
- **Health Monitoring**: Queries and updates device health statistics
- **State Transitions**: Manages blobstore state machine (NORMAL → FAULTY → OUT)
- **Media Error Handling**: Processes all I/O errors for the device
- **Auto-Faulty Detection**: Monitors error thresholds and triggers eviction
- **Blob Operations**: Performs blobstore load/unload operations

**Assignment:**
```c
// In alloc_bio_blobstore()
bb->bb_owner_xs = ctxt;  // First xstream to open the blobstore
```

**Why Device Owner is Special:**
- Avoids race conditions in health data updates
- Centralizes state machine logic
- Provides consistent monitoring intervals
- Single point for faulty device reactions

### 3. **Regular Xstreams** (All Others)

**Responsibilities:**
- **I/O Operations**: Issue read/write/unmap operations to blobs
- **DMA Buffer Management**: Manage per-xstream DMA safe buffers
- **I/O Channel Operations**: Use SPDK I/O channels for zero-copy access
- **Event Forwarding**: Forward device events to owner xstream

## Threading Model Flow

### Initialization Sequence

```
1. First DAOS engine starts
   ├─> bio_xsctxt_alloc(tgt_id=0)
   ├─> Creates SPDK thread: "daos_spdk_0"
   ├─> Becomes INIT XSTREAM
   ├─> Initializes SPDK subsystems (spdk_subsystem_init)
   ├─> Scans all NVMe devices
   ├─> Creates bio_bdev for each device
   └─> Loads blobstores (becomes DEVICE OWNER for first blobstore)

2. Second DAOS engine starts
   ├─> bio_xsctxt_alloc(tgt_id=1)
   ├─> Creates SPDK thread: "daos_spdk_1"
   ├─> Opens existing blobstores
   └─> Becomes DEVICE OWNER for second blobstore (if available)

3. Subsequent engines follow same pattern...
```

### Message Passing Between Xstreams

SPDK provides lock-free inter-thread communication:

```c
// Send message to init xstream for LED operation
spdk_thread_send_msg(init_thread(), set_led, led_msg);

// Send message to device owner for media error
spdk_thread_send_msg(owner_thread(bbs), bio_media_error, mem);

// Send message to all xstreams for teardown
for (i = 0; i < bbs->bb_ref; i++) {
    spdk_thread_send_msg(xs_ctxt->bxc_thread, teardown_xs_bs, bxb);
}
```

**Message Flow:**
```
Regular Xstream (tgt=2)
    │
    │ Media Error Detected
    │
    ├──> spdk_thread_send_msg()
    │
    ▼
Device Owner Xstream (tgt=0)
    │
    │ bio_media_error() callback executes
    │
    ├──> Increments error counter
    ├──> Updates health stats
    └──> Triggers state transition if threshold reached
```

### Polling Model

Each xstream must actively poll its SPDK thread:

```c
// Main NVMe poll function called by DAOS engine ULT
int bio_nvme_poll(struct bio_xs_context *ctxt) {
    // Poll SPDK thread for completions and messages
    rc = spdk_thread_poll(ctxt->bxc_thread, 0, 0);
    
    // Init xstream: scan for hotplug events
    if (is_init_xstream(ctxt)) {
        scan_bio_bdevs(ctxt, now);
        bio_led_reset_on_timeout(ctxt, now);
    }
    
    // Device owner: monitor health and state
    for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
        bxb = ctxt->bxc_xs_blobstores[st];
        if (bxb && is_bbs_owner(ctxt, bxb->bxb_blobstore)) {
            bio_bs_monitor(ctxt, st, now);
        }
    }
    
    return rc;
}
```

**Polling Responsibilities by Xstream Type:**

| Xstream Type | Polling Actions |
|--------------|----------------|
| **Init** | • SPDK event processing<br>• Hotplug device scanning<br>• LED timeout checks<br>• Regular I/O operations |
| **Device Owner** | • SPDK event processing<br>• Device health monitoring<br>• State transition checks<br>• Auto-faulty detection<br>• Regular I/O operations |
| **Regular** | • SPDK event processing<br>• Regular I/O operations |

## Advanced Concepts

### 1. **SPDK I/O Channels**

Each xstream gets its own I/O channel to each blobstore:

```c
struct bio_xs_blobstore {
    struct bio_blobstore    *bxb_blobstore;  // Shared blobstore
    struct spdk_io_channel  *bxb_io_channel; // Per-xstream channel
    // ...
};
```

**Benefits:**
- Lock-free I/O submission
- Per-thread completion queues
- Zero-copy data transfer
- Hardware queue affinity

### 2. **DMA Buffer Management**

Each xstream has its own DMA-safe buffer:

```c
struct bio_xs_context {
    struct bio_dma_buffer *bxc_dma_buf;  // SPDK allocated memory
};
```

**Purpose:**
- Intermediate buffer for RDMA transfers
- Ensures memory is DMA-capable
- Per-xstream allocation avoids contention
- Can grow dynamically on demand

### 3. **Self-Polling Mode**

For standalone VOS (testing), xstreams can self-poll:

```c
if (ctxt->bxc_self_polling) {
    spdk_thread_poll(ctxt->bxc_thread, 0, 0);
} else {
    bio_yield(NULL);  // Let other ULTs run
}
```

### 4. **Blobstore State Machine**

Device owner xstream manages state transitions:

```
NORMAL ──┐
    ↓    │
FAULTY   │ (device reintegration)
    ↓    │
TEARDOWN │
    ↓    │
OUT ─────┘
```

## Why This Model?

### Design Rationale

1. **Lock-Free Performance**
   - One SPDK thread per DAOS target eliminates locking
   - Each thread has dedicated hardware queues
   - Message passing via lock-free rings

2. **Clear Ownership**
   - Device owner centralizes management
   - Init xstream handles system-wide operations
   - Regular xstreams focus on I/O

3. **Fault Isolation**
   - Device failures handled by owner xstream
   - Other xstreams forward errors
   - Clean separation of concerns

4. **Hardware Affinity**
   - SPDK threads can pin to NUMA nodes
   - I/O channels map to hardware queues
   - Maximizes throughput

5. **Scalability**
   - Adding engines = adding xstreams
   - No global locks in data path
   - Linear scaling with device count

## Common Patterns

### Pattern 1: Owner-Only Operations

```c
// Only device owner should do this
if (is_bbs_owner(ctxt, bbs)) {
    bio_bs_state_set(bbs, BIO_BS_STATE_FAULTY);
}
```

### Pattern 2: Forward to Owner

```c
// Any xstream can detect error, forward to owner
struct media_error_msg *mem = D_ALLOC_PTR();
// ... fill in error info ...
spdk_thread_send_msg(owner_thread(bbs), bio_media_error, mem);
```

### Pattern 3: Init Xstream Only

```c
// LED operations must run on init xstream
if (init_xs_context() != NULL) {
    spdk_thread_send_msg(init_thread(), set_led, led_msg);
}
```

### Pattern 4: Broadcast to All

```c
// Send to all xstreams using the blobstore
for (i = 0; i < bbs->bb_ref; i++) {
    struct bio_xs_context *xs_ctxt = bbs->bb_xs_ctxts[i];
    spdk_thread_send_msg(xs_ctxt->bxc_thread, callback, msg);
}
```

## Debugging Tips

1. **Check xstream identity:**
   ```c
   D_DEBUG(DB_MGMT, "Init xstream: %d, Owner: %d",
           is_init_xstream(ctxt),
           is_bbs_owner(ctxt, bbs));
   ```

2. **Trace message passing:**
   ```c
   D_DEBUG(DB_IO, "Sending message from tgt %d to tgt %d",
           current_ctxt->bxc_tgt_id,
           target_ctxt->bxc_tgt_id);
   ```

3. **Monitor polling:**
   ```c
   rc = spdk_thread_poll(ctxt->bxc_thread, 0, 0);
   D_DEBUG(DB_TRACE, "Poll returned: %d events", rc);
   ```

## Summary

The BIO threading model achieves high performance and scalability through:
- **Per-target SPDK threads** for lock-free I/O
- **Specialized roles** (init, device owner) for management
- **Message passing** for inter-xstream communication
- **Polling model** integrated with DAOS ULTs
- **Clear ownership** to avoid race conditions

This design allows DAOS to fully exploit NVMe SSD performance while maintaining clean separation of concerns for device management, health monitoring, and fault handling.
