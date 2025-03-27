/*
 * Copyright (c) 2019 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *          Swapnil Ingle <swapnil.ingle@nutanix.com>
 *          Felipe Franciosi <felipe@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

/*
 * Defines the libvfio-user server-side API.  The protocol definitions can be
 * found in vfio-user.h.
 *
 * This is not currently a stable API or ABI, and may change at any time.
 * Library calls are not guaranteed thread-safe: multi-threaded consumers need
 * to protect calls with their own exclusion methods.
 */

#ifndef LIB_VFIO_USER_H
#define LIB_VFIO_USER_H

#include <stdint.h>
#include <sys/uio.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/queue.h>

#include "pci_caps/dsn.h"
#include "pci_caps/msi.h"
#include "pci_caps/msix.h"
#include "pci_caps/pm.h"
#include "pci_caps/px.h"
#include "pci_defs.h"
#include "vfio-user.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIB_VFIO_USER_MAJOR 0
#define LIB_VFIO_USER_MINOR 1

/* DMA addresses cannot be directly de-referenced. */
typedef void *vfu_dma_addr_t;

struct dma_sg;
typedef struct dma_sg dma_sg_t;

typedef struct vfu_ctx vfu_ctx_t;

/*
 * Returns the size, in bytes, of dma_sg_t.
 */
size_t
dma_sg_size(void);

/*
 * Attaching to the transport is non-blocking.
 * The caller must then manually call vfu_attach_ctx(),
 * which is non-blocking, as many times as necessary.
 *
 * This also applies to vfu_run_ctx(). However, it's presumed that any actual
 * reads or writes of the socket connection will not need to block, since both
 * APIS are synchronous.
 */
#define LIBVFIO_USER_FLAG_ATTACH_NB  (1 << 0)

typedef enum {
    VFU_TRANS_SOCK,
    // For internal testing only
    VFU_TRANS_PIPE,
    VFU_TRANS_MAX
} vfu_trans_t;

typedef enum {
    VFU_DEV_TYPE_PCI
} vfu_dev_type_t;

/**
 * Creates libvfio-user context. By default one ERR and one REQ IRQs are
 * initialized, this can be overridden with vfu_setup_device_nr_irqs.
 *
 * @trans: transport type
 * @path: path to socket file.
 * @flags: context flags (LIBVFIO_USER_FLAG_*)
 * @pvt: private data
 * @dev_type: device type
 *
 * @returns the vfu_ctx to be used or NULL on error. Sets errno.
 */
vfu_ctx_t *
vfu_create_ctx(vfu_trans_t trans, const char *path,
               int flags, void *pvt, vfu_dev_type_t dev_type);

/*
 * Finalizes the device making it ready for vfu_attach_ctx(). This function is
 * mandatory to be called before vfu_attach_ctx().
 * @vfu_ctx: the libvfio-user context
 *
 * @returns: 0 on success, -1 on error. Sets errno.
 */
int
vfu_realize_ctx(vfu_ctx_t *vfu_ctx);

/*
 * Attempts to attach to the transport. Attach is mandatory before vfu_run_ctx()
 * and is non blocking if context is created with LIBVFIO_USER_FLAG_ATTACH_NB
 * flag.
 *
 * @returns: 0 on success, -1 on error. Sets errno.  If errno is set to EAGAIN
 * or EWOULDBLOCK then the transport is not ready to attach to and the operation
 * must be retried.
 *
 * @vfu_ctx: the libvfio-user context
 */
int
vfu_attach_ctx(vfu_ctx_t *vfu_ctx);

/**
 * Return a file descriptor suitable for waiting on via epoll() or similar. The
 * file descriptor may change after a successful vfu_attach_ctx(), or on
 * receiving ENOTCONN error message from vfu_run_ctx(); in those cases,
 * vfu_get_poll_fd() should be called again to get the current correct file
 * descriptor.
 */
int
vfu_get_poll_fd(vfu_ctx_t *vfu_ctx);

/**
 * Polls the vfu_ctx and processes the command received from client.
 * - Blocking vfu_ctx:
 *   Blocks until new request is received from client and continues processing
 *   the requests. Exits only in case of error or if the client disconnects.
 * - Non-blocking vfu_ctx(LIBVFIO_USER_FLAG_ATTACH_NB):
 *   Processes one request from client if it's available, otherwise it
 *   immediately returns and the caller is responsible for periodically
 *   calling again.
 *
 * @vfu_ctx: The libvfio-user context to poll
 *
 * @returns the number of requests processed (0 or more); or -1 on error,
 *          with errno set as follows:
 *
 * ENOTCONN: client closed connection, vfu_attach_ctx() should be called again
 * EBUSY: the device was asked to quiesce and is still quiescing
 * Other errno values are also possible.
 */
int
vfu_run_ctx(vfu_ctx_t *vfu_ctx);

/**
 * Destroys libvfio-user context. During this call the device must already be
 * in quiesced state; the quiesce callback is not called. Any other device
 * callback can be called.
 *
 * @vfu_ctx: the libvfio-user context to destroy
 */
void
vfu_destroy_ctx(vfu_ctx_t *vfu_ctx);

/**
 * Return the private pointer given to vfu_create_ctx().
 */
void *
vfu_get_private(vfu_ctx_t *vfu_ctx);

/**
 * Callback function signature for log function
 * @vfu_ctx: the libvfio-user context
 * @level: log level as defined in syslog(3)
 * @vfu_log_fn_t: typedef for log function.
 * @msg: message
 */
typedef void (vfu_log_fn_t)(vfu_ctx_t *vfu_ctx, int level, const char *msg);

/**
 * Log to the logging function configured for this context. The format should
 * not include a new line.
 */
void
vfu_log(vfu_ctx_t *vfu_ctx, int level, const char *fmt, ...) \
    __attribute__((format(printf, 3, 4)));

/**
 * Set up logging information.
 * @vfu_ctx: the libvfio-user context
 * @log: logging function
 * @level: logging level as defined in syslog(3)
 *
 * The log handler is expected to add a newline (that is, log messages do not
 * include a newline).
 */
int
vfu_setup_log(vfu_ctx_t *vfu_ctx, vfu_log_fn_t *log, int level);

/**
 * Prototype for region access callback. When a region is accessed, libvfio-user
 * calls the previously registered callback with the following arguments:
 *
 * @vfu_ctx: the libvfio-user context
 * @buf: buffer containing the data to be written or data to be read into
 * @count: number of bytes being read or written
 * @offset: byte offset within the region
 * @is_write: whether or not this is a write
 *
 * @returns the number of bytes read or written, or -1 on error, setting errno.
 */
typedef ssize_t (vfu_region_access_cb_t)(vfu_ctx_t *vfu_ctx, char *buf,
                                         size_t count, loff_t offset,
                                         bool is_write);

#define VFU_REGION_FLAG_READ      (1 << 0)
#define VFU_REGION_FLAG_WRITE     (1 << 1)
#define VFU_REGION_FLAG_RW        (VFU_REGION_FLAG_READ | VFU_REGION_FLAG_WRITE)
/* If unset, this is an IO region. */
#define VFU_REGION_FLAG_MEM       (1 << 2)
#define VFU_REGION_FLAG_ALWAYS_CB (1 << 3)
#define VFU_REGION_FLAG_MASK      (VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM | \
                                   VFU_REGION_FLAG_ALWAYS_CB)

/**
 * Set up a device region.
 *
 * A region is an area of device memory that can be accessed by the client,
 * either via VFIO_USER_REGION_READ/WRITE, or directly by mapping the region
 * into the client's address space if an fd is given.
 *
 * A mappable region can be split into mappable sub-areas according to the
 * @mmap_areas array. Note that the client can memory map any part of the
 * file descriptor, even if not supposed to do so according to @mmap_areas.
 * There is no way in Linux to avoid this.
 *
 * TODO maybe we should introduce per-sparse region file descriptors so that
 * the client cannot possibly memory map areas it's not supposed to. Even if
 * the client needs to have region under the same backing file, it is possible
 * to create linear device-mapper targets, one for each area, and provide file
 * descriptors of these DM targets. This is something we can document and
 * demonstrate in a sample.
 *
 * Areas that are accessed via such a mapping by definition do not invoke any
 * given callback.  However, the callback can still be invoked, even on a
 * mappable area, if the client chooses to call VFIO_USER_REGION_READ/WRITE.
 *
 * The following regions are special and are explained below:
 *  - VFU_PCI_DEV_CFG_REGION_IDX,
 *  - VFU_PCI_DEV_MIGR_REGION_IDX, and
 *  - VFU_GENERIC_DEV_MIGR_REG_IDX.
 *
 * Region VFU_PCI_DEV_CFG_REGION_IDX, corresponding to PCI config space, has
 * special handling:
 *
 *  - the @size argument is ignored: the region size is always the size defined
 *    by the relevant PCI specification
 *  - all accesses to the standard PCI header (i.e. the first 64 bytes of the
 *    region) are handled by the library
 *  - all accesses to known PCI capabilities (see vfu_pci_add_capability())
 *    are handled by the library
 *  - if no callback is provided, reads to other areas are a simple memcpy(),
 *    and writes are an error
 *  - otherwise, the callback is expected to handle the access
 *  - if VFU_REGION_FLAG_ALWAYS_CB flag is set, all accesses to the config
 *    space are forwarded to the callback
 *
 * Regions VFU_PCI_DEV_MIGR_REGION_IDX and VFU_GENERIC_DEV_MIGR_REG_IDX,
 * corresponding to the migration region, enable live migration support for
 * the device. The migration region must contain at the beginning the migration
 * registers (struct vfio_user_migration_info) and the remaining part of the
 * region can be arbitrarily used by the device implementation. The region
 * provided must have at least vfu_get_migr_register_area_size() bytes available
 * at the start of the region (this size is guaranteed to be page-aligned). If
 * mmap_areas is given, it must _not_ include this part of the region.
 *
 * libvfio-user offers two ways for the migration region to be used:
 *  1. natively: the device implementation must handle accesses to the
 *      migration registers and migration data via the region callbacks. The
 *      semantics of these registers are explained in <linux/vfio.h>.
 *  2. via the vfu_migration_callbacks_t callbacks: the device implementation
 *      registers a set of callbacks by calling vfu_setup_device_migration.
 *      The region's read/write callbacks are never called.
 *
 * @vfu_ctx: the libvfio-user context
 * @region_idx: region index
 * @size: size of the region
 * @region_access: callback function to access region
 * @flags: region flags (VFU_REGION_FLAG_*)
 * @mmap_areas: array of memory mappable areas; if an fd is provided, but this
 * is NULL, then the entire region is mappable.
 * @nr_mmap_areas: number of sparse areas in @mmap_areas; must be provided if
 *  the @mmap_areas is non-NULL, or 0 otherwise.
 * @fd: file descriptor of the file backing the region if the region is
 *  mappable; it is the server's responsibility to create a file suitable for
 *  memory mapping by the client.
 * @offset: offset of the region within the fd, or zero.
 *
 * @returns 0 on success, -1 on error, Sets errno.
 */
int
vfu_setup_region(vfu_ctx_t *vfu_ctx, int region_idx, size_t size,
                 vfu_region_access_cb_t *region_access, int flags,
                 struct iovec *mmap_areas, uint32_t nr_mmap_areas,
                 int fd, uint64_t offset);

typedef enum vfu_reset_type {
    /*
     * Client requested a device reset (for example, as part of a guest VM
     * reboot). The vfio-user context remains valid, but it's expected that all
     * ongoing operations are completed or cancelled, and any device state is
     * reset to a known-good initial state (including any PCI register state).
     */
    VFU_RESET_DEVICE,

    /*
     * The vfio-user socket client connection was closed or reset. The attached
     * context is cleaned up after returning from the reset callback, and
     * vfu_attach_ctx() must be called to establish a new client.
     */
    VFU_RESET_LOST_CONN,

    /*
     * Client requested to initiate PCI function level reset.
     */
    VFU_RESET_PCI_FLR
} vfu_reset_type_t;

/*
 * Device callback for quiescing the device.
 *
 * vfu_run_ctx uses this callback to request from the device to quiesce its
 * operation. A quiesced device must not call the following functions:
 *  - vfu_dma_read and vfu_dma_write,
 *  - vfu_addr_to_sg, vfu_map_sg, and vfu_unmap_sg, unless it does so from a
 *    device callback.
 *
 * The callback can return two values:
 * 1) 0: this indicates that the device was quiesced. vfu_run_ctx then continues
 *      to execute and when vfu_run_ctx returns to the caller the device is
 *      unquiesced.
 * 2) -1 with errno set to EBUSY: this indicates that the device cannot
 *      immediately quiesce. In this case, vfu_run_ctx returns -1 with errno
 *      set to EBUSY and future calls to vfu_run_ctx return the same. Until the
 *      device quiesces it can continue operate as normal. The device indicates
 *      that it quiesced by calling vfu_device_quiesced. When
 *      vfu_device_quiesced returns the device is no longer quiesced.
 *
 * A quiesced device should expect for any of the following callbacks to be
 * executed: vfu_dma_register_cb_t, vfu_unregister_cb_t, vfu_reset_cb_t, and
 * the migration transition callback. These callbacks are only called after the
 * device has been quiesced.
 *
 * The following example demonstrates how a device can use vfu_map_sg and
 * friends while quiesced:
 *
 * A DMA region is mapped, libvfio-user calls the quiesce callback but the
 * device cannot immediately quiesce:
 *
 *     int quiesce_cb(vfu_ctx_t *vfu_ctx) {
 *         errno = EBUSY;
 *         return -1;
 *     }
 *
 * While quiescing, the device can continue to operate as normal, including
 * calling functions such as vfu_map_sg. Then, the device finishes quiescing:
 *
 *  vfu_quiesce_done(vfu_ctx, 0);
 *
 * At this point, the device must have stopped using functions like
 * vfu_map_sg(), for example by pausing any I/O threads.  libvfio-user
 * eventually calls the dma_register device callback before vfu_quiesce_done
 * returns. In this callback the device is allowed to call functions such as
 * vfu_map_sg:
 *
 *     void (dma_register_cb(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info) {
 *         vfu_map_sg(ctx, ...);
 *     }
 *
 * Once vfu_quiesce_done returns, the device is unquiesced.
 *
 *
 * @vfu_ctx: the libvfio-user context
 *
 * @returns: 0 on success, -1 on failure with errno set.
 */
typedef int (vfu_device_quiesce_cb_t)(vfu_ctx_t *vfu_ctx);

/**
 * Sets up the device quiesce callback.
 *
 * @vfu_ctx: the libvfio-user context
 * @quiesce_cb: device quiesce callback
 */
void
vfu_setup_device_quiesce_cb(vfu_ctx_t *vfu_ctx,
                            vfu_device_quiesce_cb_t *quiesce_cb);

/*
 * Called by the device to complete a pending quiesce operation. After the
 * function returns the device is unquiesced.
 *
 * @vfu_ctx: the libvfio-user context
 * @quiesce_errno: 0 for success or errno in case the device fails to quiesce,
 *                 in which case the operation requiring the quiesce is failed
 *                 and the device is reset.
 *
 * @returns 0 on success, or -1 on failure. Sets errno.
 */
int
vfu_device_quiesced(vfu_ctx_t *vfu_ctx, int quiesce_errno);

/*
 * Callback function that is called when the device must be reset.
 */
typedef int (vfu_reset_cb_t)(vfu_ctx_t *vfu_ctx, vfu_reset_type_t type);

/**
 * Set up device reset callback.
 *
 * A reset should ensure that all on-going use of device IRQs or guest memory is
 * completed or cancelled before returning from the callback.
 *
 * @vfu_ctx: the libvfio-user context
 * @reset: device reset callback
 */
int
vfu_setup_device_reset_cb(vfu_ctx_t *vfu_ctx, vfu_reset_cb_t *reset);

/*
 * Info for a guest DMA region.  @iova is always valid; the other parameters
 * will only be set if the guest DMA region is mappable.
 *
 * @iova: guest DMA range. This is the guest physical range (as we don't
 *   support vIOMMU) that the guest registers for DMA, via a VFIO_USER_DMA_MAP
 *   message, and is the address space used as input to vfu_addr_to_sg().
 * @vaddr: if the range is mapped into this process, this is the virtual address
 *   of the start of the region.
 * @mapping: if @vaddr is non-NULL, this range represents the actual range
 *   mmap()ed into the process. This might be (large) page aligned, and
 *   therefore be different from @vaddr + @iova.iov_len.
 * @page_size: if @vaddr is non-NULL, page size of the mapping (e.g. 2MB)
 * @prot: if @vaddr is non-NULL, protection settings of the mapping as per
 *   mmap(2)
 *
 * For a real example, using the gpio sample server, and a qemu configured to
 * use huge pages and share its memory:
 *
 * gpio: mapped DMA region iova=[0xf0000-0x10000000) vaddr=0x2aaaab0f0000
 * page_size=0x200000 mapping=[0x2aaaab000000-0x2aaabb000000)
 *
 *     0xf0000                    0x10000000
 *     |                                   |
 *     v                                   v
 *     +-----------------------------------+
 *     | Guest IOVA (DMA) space            |
 *  +--+-----------------------------------+--+
 *  |  |                                   |  |
 *  |  +-----------------------------------+  |
 *  |  ^ libvfio-user server address space    |
 *  +--|--------------------------------------+
 *  ^ vaddr=0x2aaaab0f0000                    ^
 *  |                                         |
 *  0x2aaaab000000               0x2aaabb000000
 *
 * This region can be directly accessed at 0x2aaaab0f0000, but the underlying
 * large page mapping is in the range [0x2aaaab000000-0x2aaabb000000).
 */
typedef struct vfu_dma_info {
    struct iovec iova;
    void *vaddr;
    struct iovec mapping;
    size_t page_size;
    uint32_t prot;
} vfu_dma_info_t;

/*
 * Called when a guest registers one of its DMA regions via a VFIO_USER_DMA_MAP
 * message.
 *
 * @vfu_ctx: the libvfio-user context
 * @info: the DMA info
 */
typedef void (vfu_dma_register_cb_t)(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info);

/*
 * Function that is called when the guest unregisters a DMA region.  This
 * callback is required if you want to be able to access guest memory directly
 * via a mapping. The device must release all references to that region before
 * the callback returns.
 *
 * @vfu_ctx: the libvfio-user context
 * @info: the DMA info
 */
typedef void (vfu_dma_unregister_cb_t)(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info);

/**
 * Set up device DMA registration callbacks. When libvfio-user is notified of a
 * DMA range addition or removal, these callbacks will be invoked.
 *
 * If this function is not called, guest DMA regions are not accessible via
 * vfu_addr_to_sg().
 *
 * To directly access this DMA memory via a local mapping with vfu_map_sg(), at
 * least @dma_unregister must be provided.
 *
 * @vfu_ctx: the libvfio-user context
 * @dma_register: DMA region registration callback (optional)
 * @dma_unregister: DMA region unregistration callback (optional)
 */

int
vfu_setup_device_dma(vfu_ctx_t *vfu_ctx, vfu_dma_register_cb_t *dma_register,
                     vfu_dma_unregister_cb_t *dma_unregister);

enum vfu_dev_irq_type {
    VFU_DEV_INTX_IRQ,
    VFU_DEV_MSI_IRQ,
    VFU_DEV_MSIX_IRQ,
    VFU_DEV_ERR_IRQ,
    VFU_DEV_REQ_IRQ,
    VFU_DEV_NUM_IRQS
};

/**
 * Set up device IRQ counts.
 * @vfu_ctx: the libvfio-user context
 * @type: IRQ type (VFU_DEV_INTX_IRQ ... VFU_DEV_REQ_IRQ)
 * @count: number of irqs
 */
int
vfu_setup_device_nr_irqs(vfu_ctx_t *vfu_ctx, enum vfu_dev_irq_type type,
                         uint32_t count);


typedef enum {
    VFU_MIGR_STATE_STOP,
    VFU_MIGR_STATE_RUNNING,
    VFU_MIGR_STATE_STOP_AND_COPY,
    VFU_MIGR_STATE_PRE_COPY,
    VFU_MIGR_STATE_RESUME
} vfu_migr_state_t;

#define VFU_MIGR_CALLBACKS_VERS 1

/*
 * Callbacks during the pre-copy and stop-and-copy phases.
 *
 * The client executes the following steps to copy migration data:
 *
 * 1. get_pending_bytes: device must return amount of migration data
 * 2. prepare_data: device must prepare migration data
 * 3. read_data: device must provide migration data
 *
 * The client repeats the above steps until there is no more migration data to
 * return (the device must return 0 from get_pending_bytes to indicate that
 * there are no more migration data to be consumed in this iteration).
 */
typedef struct {

    /*
     * Set it to VFU_MIGR_CALLBACKS_VERS.
     */
    int version;

    /*
     * Migration state transition callback.
     *
     * The callback should return -1 on error, setting errno.
     *
     *
     * TODO rename to vfu_migration_state_transition_callback
     * FIXME maybe we should create a single callback and pass the state?
     */
    int (*transition)(vfu_ctx_t *vfu_ctx, vfu_migr_state_t state);

    /* Callbacks for saving device state */

    /*
     * Function that is called to retrieve the amount of pending migration
     * data. If migration data were previously made available (function
     * prepare_data has been called) then calling this function signifies that
     * they have been read (e.g. migration data can be discarded). If the
     * function returns 0 then migration has finished and this function won't
     * be called again.
     *
     * The amount of pending migration data returned by the device does not
     * necessarily have to monotonically decrease over time and does not need
     * to match the amount of migration data returned via the @size argument in
     * prepare_data. It can completely fluctuate according to the needs of the
     * device. These semantics are derived from the pending_bytes register in
     * VFIO. Therefore the value returned by get_pending_bytes must be
     * primarily regarded as boolean, either 0 or non-zero, as far as migration
     * completion is concerned. More advanced vfio-user clients can make
     * assumptions on how migration is progressing on devices that guarantee
     * that the amount of pending migration data decreases over time.
     */
    uint64_t (*get_pending_bytes)(vfu_ctx_t *vfu_ctx);

    /*
     * Function that is called to instruct the device to prepare migration data
     * to be read when in pre-copy or stop-and-copy state, and to prepare for
     * receiving migration data when in resuming state.
     *
     * When in pre-copy and stop-and-copy state, the function must return only
     * after migration data are available at the specified offset. This
     * callback is called once per iteration. The amount of data available
     * pointed to by @size can be different that the amount of data returned by
     * get_pending_bytes in the beginning of the iteration.
     *
     * In VFIO, the data_offset and data_size registers can be read multiple
     * times during an iteration and are invariant, libvfio-user simplifies
     * this by caching the values and returning them when read, guaranteeing
     * that prepare_data() is called only once per migration iteration.
     *
     * When in resuming state, @offset must be set to where migration data must
     * written. @size points to NULL.
     *
     * The callback should return -1 on error, setting errno.
     */
    int (*prepare_data)(vfu_ctx_t *vfu_ctx, uint64_t *offset, uint64_t *size);

    /*
     * Function that is called to read migration data. offset and size can be
     * any subrange on the offset and size previously returned by prepare_data.
     * The function must return the amount of data read or -1 on error, setting
     * errno.
     *
     * This function can be called even if the migration data can be memory
     * mapped.
     */
    ssize_t (*read_data)(vfu_ctx_t *vfu_ctx, void *buf,
                         uint64_t count, uint64_t offset);

    /* Callbacks for restoring device state */

    /*
     * Fuction that is called for writing previously stored device state. The
     * function must return the amount of data written or -1 on error, setting
     * errno.
     */
    ssize_t (*write_data)(vfu_ctx_t *vfu_ctx, void *buf, uint64_t count,
                          uint64_t offset);

    /*
     * Function that is called when client has written some previously stored
     * device state.
     *
     * The callback should return -1 on error, setting errno.
     */
    int (*data_written)(vfu_ctx_t *vfu_ctx, uint64_t count);

} vfu_migration_callbacks_t;

/**
 * The definition for VFIO_DEVICE_STATE_XXX differs with the version of vfio
 * header file used. Some old systems wouldn't have these definitions. Some
 * other newer systems would be using region based migration, and not
 * have VFIO_DEVICE_STATE_V1_XXXX defined. The latest ones have
 * VFIO_DEVICE_STATE_V1_XXXX defined. The following addresses all
 * these scenarios.
 */
#if defined(VFIO_DEVICE_STATE_STOP)

_Static_assert(VFIO_DEVICE_STATE_STOP == 0,
               "incompatible VFIO_DEVICE_STATE_STOP definition");

#define VFIO_DEVICE_STATE_V1_STOP         VFIO_DEVICE_STATE_STOP
#define VFIO_DEVICE_STATE_V1_RUNNING      VFIO_DEVICE_STATE_RUNNING
#define VFIO_DEVICE_STATE_V1_SAVING       VFIO_DEVICE_STATE_SAVING
#define VFIO_DEVICE_STATE_V1_RESUMING     VFIO_DEVICE_STATE_RESUMING

#elif !defined(VFIO_REGION_TYPE_MIGRATION_DEPRECATED) /* VFIO_DEVICE_STATE_STOP */

#define VFIO_DEVICE_STATE_V1_STOP         (0)
#define VFIO_DEVICE_STATE_V1_RUNNING      (1 << 0)
#define VFIO_DEVICE_STATE_V1_SAVING       (1 << 1)
#define VFIO_DEVICE_STATE_V1_RESUMING     (1 << 2)
#define VFIO_DEVICE_STATE_MASK            ((1 << 3) - 1)

#endif /* VFIO_REGION_TYPE_MIGRATION_DEPRECATED */

/*
 * The currently defined migration registers; if using migration callbacks,
 * these are handled internally by the library.
 *
 * This is analogous to struct vfio_device_migration_info.
 */
struct vfio_user_migration_info {
    /* VFIO_DEVICE_STATE_* */
    uint32_t device_state;
    uint32_t reserved;
    uint64_t pending_bytes;
    uint64_t data_offset;
    uint64_t data_size;
};

/*
 * Returns the size of the area needed to hold the migration registers at the
 * beginning of the migration region; guaranteed to be page aligned.
 */
size_t
vfu_get_migr_register_area_size(void);

/**
 * vfu_setup_device_migration provides an abstraction over the migration
 * protocol: the user specifies a set of callbacks which are called in response
 * to client accesses of the migration region; the migration region read/write
 * callbacks are not called after this function call. Offsets in callbacks are
 * relative to @data_offset.
 *
 * @vfu_ctx: the libvfio-user context
 * @callbacks: migration callbacks
 * @data_offset: offset in the migration region where data begins.
 *
 * @returns 0 on success, -1 on error, sets errno.
 */
int
vfu_setup_device_migration_callbacks(vfu_ctx_t *vfu_ctx,
                                     const vfu_migration_callbacks_t *callbacks,
                                     uint64_t data_offset);

/**
 * Triggers an interrupt.
 *
 * libvfio-user takes care of using the correct IRQ type (IRQ index: INTx or
 * MSI/X), the caller only needs to specify the sub-index.
 *
 * @vfu_ctx: the libvfio-user context to trigger interrupt
 * @subindex: vector subindex to trigger interrupt on
 *
 * @returns 0 on success, or -1 on failure. Sets errno.
 */
int
vfu_irq_trigger(vfu_ctx_t *vfu_ctx, uint32_t subindex);

/**
 * Takes a guest physical address range and populates an array of scatter/gather
 * entries than can be individually mapped in the program's virtual memory.  A
 * single linear guest physical address span may need to be split into multiple
 * scatter/gather regions due to limitations of how memory can be mapped.
 *
 * vfu_setup_device_dma() must have been called prior to using this function.
 *
 * @vfu_ctx: the libvfio-user context
 * @dma_addr: the guest physical address
 * @len: size of memory to be mapped
 * @sg: array that receives the scatter/gather entries to be mapped
 * @max_sg: maximum number of elements in above array
 * @prot: protection as defined in <sys/mman.h>
 *
 * @returns the number of scatter/gather entries created on success, and on
 * failure:
 *  -1:         if the GPA address span is invalid (errno=ENOENT) or
 *              protection violation (errno=EACCES)
 *  (-x - 1):   if @max_sg is too small, where x is the number of scatter/gather
 *              entries necessary to complete this request (errno=0).
 */
int
vfu_addr_to_sg(vfu_ctx_t *vfu_ctx, vfu_dma_addr_t dma_addr, size_t len,
               dma_sg_t *sg, int max_sg, int prot);

/**
 * Maps scatter/gather entries from the guest's physical address space to the
 * process's virtual memory. It is the caller's responsibility to remove the
 * mappings by calling vfu_unmap_sg().
 *
 * This is only supported when a @dma_unregister callback is provided to
 * vfu_setup_device_dma().
 *
 * @vfu_ctx: the libvfio-user context
 * @sg: array of scatter/gather entries returned by vfu_addr_to_sg. These
 *      entries must not be modified and the array must not be deallocated
 *      until vfu_unmap_sg() has been called.
 * @iov: array of iovec structures (defined in <sys/uio.h>) to receive each
 *       mapping
 * @cnt: number of scatter/gather entries to map
 * @flags: must be 0
 *
 * @returns 0 on success, -1 on failure. Sets errno.
 */
int
vfu_map_sg(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, struct iovec *iov, int cnt,
           int flags);

/**
 * Unmaps scatter/gather entries (previously mapped by vfu_map_sg()) from
 * the process's virtual memory.
 *
 * @vfu_ctx: the libvfio-user context
 * @sg: array of scatter/gather entries to unmap
 * @iov: array of iovec structures for each scatter/gather entry
 * @cnt: number of scatter/gather entries to unmap
 */
void
vfu_unmap_sg(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, struct iovec *iov, int cnt);

/**
 * Read from the dma region exposed by the client. This can be used as an
 * alternative to vfu_map_sg(), if the region is not directly mappable, or DMA
 * notification callbacks have not been provided.
 *
 * @vfu_ctx: the libvfio-user context
 * @sg: a DMA segment obtained from dma_addr_to_sg
 * @data: data buffer to read into
 *
 * @returns 0 on success, -1 on failure. Sets errno.
 */
int
vfu_dma_read(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, void *data);

/**
 * Write to the dma region exposed by the client.  This can be used as an
 * alternative to vfu_map_sg(), if the region is not directly mappable, or DMA
 * notification callbacks have not been provided.
 *
 * @vfu_ctx: the libvfio-user context
 * @sg: a DMA segment obtained from dma_addr_to_sg
 * @data: data buffer to write
 *
 * @returns 0 on success, -1 on failure. Sets errno.
 */
int
vfu_dma_write(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, void *data);

/*
 * Supported PCI regions.
 *
 * Note: in VFIO, each region starts at a terabyte offset
 * (VFIO_PCI_INDEX_TO_OFFSET) and because Linux supports up to 128 TB of user
 * space virtual memory, there can be up to 128 device regions. PCI regions are
 * fixed and in retrospect this choice has proven to be problematic because
 * devices might contain potentially unused regions. New regions can now be
 * positioned anywhere by using the VFIO_REGION_INFO_CAP_TYPE capability.  In
 * vfio-user we don't have this problem because the region index is just an
 * identifier: the VMM memory maps a file descriptor that is passed to it and
 * the mapping offset is derived from the mmap_areas offset value, rather than a
 * static mapping from region index to offset. Thus, additional regions can
 * have static indexes in vfio-user.
 */
enum {
    VFU_PCI_DEV_BAR0_REGION_IDX,
    VFU_PCI_DEV_BAR1_REGION_IDX,
    VFU_PCI_DEV_BAR2_REGION_IDX,
    VFU_PCI_DEV_BAR3_REGION_IDX,
    VFU_PCI_DEV_BAR4_REGION_IDX,
    VFU_PCI_DEV_BAR5_REGION_IDX,
    VFU_PCI_DEV_ROM_REGION_IDX,
    VFU_PCI_DEV_CFG_REGION_IDX,
    VFU_PCI_DEV_VGA_REGION_IDX,
    VFU_PCI_DEV_MIGR_REGION_IDX,
    VFU_PCI_DEV_NUM_REGIONS,
};

typedef enum {
    VFU_PCI_TYPE_CONVENTIONAL,
    VFU_PCI_TYPE_PCI_X_1,
    VFU_PCI_TYPE_PCI_X_2,
    VFU_PCI_TYPE_EXPRESS
} vfu_pci_type_t;

enum {
    VFU_GENERIC_DEV_MIGR_REGION_IDX,
    VFU_GENERIC_DEV_NUM_REGIONS
};

/**
 * Initialize the context for a PCI device. This function must be called only
 * once per libvfio-user context.
 *
 * This function initializes a buffer for the PCI config space, accessible via
 * vfu_pci_get_config_space().
 *
 * Returns 0 on success, or -1 on error, setting errno.
 *
 * @vfu_ctx: the libvfio-user context
 * @pci_type: PCI type (convention PCI, PCI-X mode 1, PCI-X mode2, PCI-Express)
 * @hdr_type: PCI header type. Only PCI_HEADER_TYPE_NORMAL is supported.
 * @revision: PCI/PCI-X/PCIe revision
 */
int
vfu_pci_init(vfu_ctx_t *vfu_ctx, vfu_pci_type_t pci_type,
             int hdr_type, int revision);

/*
 * Set the Vendor ID, Device ID, Subsystem Vendor ID, and Subsystem ID fields of
 * the PCI config header (PCI3 6.2.1, 6.2.4).
 *
 * This must always be called for PCI devices, after vfu_pci_init().
 */
void
vfu_pci_set_id(vfu_ctx_t *vfu_ctx, uint16_t vid, uint16_t did,
               uint16_t ssvid, uint16_t ssid);

/*
 * Set the class code fields (base, sub-class, and programming interface) of the
 * PCI config header (PCI3 6.2.1).
 *
 * If this function is not called, the fields are initialized to zero.
 */
void
vfu_pci_set_class(vfu_ctx_t *vfu_ctx, uint8_t base, uint8_t sub, uint8_t pi);


/*
 * Returns a pointer to the PCI configuration space.
 *
 * PCI config space consists of an initial 64-byte vfu_pci_hdr_t, plus
 * additional space, containing capabilities and/or device-specific
 * configuration.  Standard config space is 256 bytes (PCI_CFG_SPACE_SIZE);
 * extended config space is 4096 bytes (PCI_CFG_SPACE_EXP_SIZE).
 */
vfu_pci_config_space_t *
vfu_pci_get_config_space(vfu_ctx_t *vfu_ctx);

#define VFU_CAP_FLAG_EXTENDED (1 << 0)
#define VFU_CAP_FLAG_CALLBACK (1 << 1)
#define VFU_CAP_FLAG_READONLY (1 << 2)

/**
 * Add a PCI capability to PCI config space.
 *
 * Certain standard capabilities are handled entirely within the library:
 *
 * PCI_CAP_ID_EXP (pxcap)
 * PCI_CAP_ID_MSIX (msixcap)
 * PCI_CAP_ID_PM (pmcap)
 *
 * However, they must still be explicitly initialized and added here.
 *
 * The contents of @data are copied in. It must start with either a struct
 * cap_hdr or a struct ext_cap_hdr, with the ID field set; the 'next' field is
 * ignored.  For PCI_CAP_ID_VNDR or PCI_EXT_CAP_ID_VNDR, the embedded size field
 * must also be set; in general, any non-fixed-size capability must be
 * initialized such that the size can be derived at this point.
 *
 * If @pos is non-zero, the capability will be placed at the given offset within
 * configuration space. It must not overlap the PCI standard header, or any
 * existing capability. Note that if a capability is added "out of order" in
 * terms of the offset, there is no re-ordering of the capability list written
 * in configuration space.
 *
 * If @pos is zero, the capability will be placed at a suitable offset
 * automatically.
 *
 * The @flags field can be set as follows:
 *
 * VFU_CAP_FLAG_EXTENDED: this is an extended capability; supported if device is
 * of PCI type VFU_PCI_TYPE_{PCI_X_2,EXPRESS}.
 *
 * VFU_CAP_FLAG_CALLBACK: all accesses to the capability are delegated to the
 * callback for the region VFU_PCI_DEV_CFG_REGION_IDX. The callback should copy
 * data into and out of the capability as needed (this could be directly on the
 * config space area from vfu_pci_get_config_space()). It is not supported to
 * allow writes to the initial capability header (ID/next fields).
 *
 * VFU_CAP_FLAG_READONLY: this prevents clients from writing to the capability.
 * By default, clients are allowed to write to any part of the capability,
 * excluding the initial header.
 *
 * Returns the offset of the capability in config space, or -1 on error, with
 * errno set.
 *
 * @vfu_ctx: the libvfio-user context
 * @pos: specific offset for the capability, or 0.
 * @flags: VFU_CAP_FLAG_*
 * @data: capability data, including the header
 */
ssize_t
vfu_pci_add_capability(vfu_ctx_t *vfu_ctx, size_t pos, int flags, void *data);

/**
 * Find the offset within config space of a given capability (if there are
 * multiple possible matches, use vfu_pci_find_next_capability()).
 *
 * Returns 0 if no such capability was found, with errno set.
 *
 * @vfu_ctx: the libvfio-user context
 * @extended whether capability is an extended one or not
 * @id: capability id (PCI_CAP_ID_* or PCI_EXT_CAP_ID *)
 */
size_t
vfu_pci_find_capability(vfu_ctx_t *vfu_ctx, bool extended, int cap_id);

/**
 * Find the offset within config space of the given capability, starting from
 * @pos, which must be the valid offset of an existing capability. This can be
 * used to iterate through multiple capabilities with the same ID.
 *
 * Returns 0 if no more matching capabilities were found, with errno set.
 *
 * @vfu_ctx: the libvfio-user context
 * @extended whether capability is an extended one or not
 * @pos: offset within config space to start looking
 * @id: capability id (PCI_CAP_ID_*)
 */
size_t
vfu_pci_find_next_capability(vfu_ctx_t *vfu_ctx, bool extended,
                             size_t pos, int cap_id);

bool
vfu_sg_is_mappable(vfu_ctx_t *vfu_ctx, dma_sg_t *sg);

/*
 * Creates a new ioeventfd at the given setup memory region with @offset, @size,
 * @fd, @flags and @datamatch.
 *
 * Returns 0 on success and -1 on failure with errno set.
 *
 * @vfu_ctx: the libvfio-user context
 * @region_idx: The index of the memory region to set up the ioeventfd
 * @fd: the value of the file descriptor
 * @offset: The offset into the memory region
 * @size: size of the ioeventfd
 * @flags: Any flags to set up the ioeventfd
 * @datamatch: sets the datamatch value
 */
int
vfu_create_ioeventfd(vfu_ctx_t *vfu_ctx, uint32_t region_idx, int fd,
                     size_t offset, uint32_t size, uint32_t flags,
                     uint64_t datamatch);

#ifdef __cplusplus
}
#endif

#endif /* LIB_VFIO_USER_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
