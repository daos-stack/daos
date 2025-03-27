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

#ifndef LIB_VFIO_USER_PRIVATE_H
#define LIB_VFIO_USER_PRIVATE_H

#include <errno.h>
#include <sys/queue.h>

#include "common.h"
#include "pci_caps.h"
#include "vfio-user.h"

/*
 * The main reason we limit the size of an individual DMA region from the client
 * is to limit the size of the dirty bitmaps: this corresponds to 256MB at a 4K
 * page size.
 */
#define MAX_DMA_SIZE (8 * ONE_TB)
#define MAX_DMA_REGIONS 16

#define SERVER_MAX_DATA_XFER_SIZE (VFIO_USER_DEFAULT_MAX_DATA_XFER_SIZE)

/*
 * Enough to receive a VFIO_USER_REGION_WRITE of SERVER_MAX_DATA_XFER_SIZE.
 */
#define SERVER_MAX_MSG_SIZE (SERVER_MAX_DATA_XFER_SIZE + \
                             sizeof(struct vfio_user_header) + \
                             sizeof(struct vfio_user_region_access))

/*
 * Structure used to hold an in-flight request+reply.
 *
 * Incoming request body and fds are stored in in.*.
 *
 * Outgoing requests are either stored in out.iov.iov_base, or out_iovecs. In
 * the latter case, the iovecs refer to data that should not be freed.
 */
typedef struct vfu_msg {
    /* in/out */
    struct vfio_user_header hdr;

    bool processed_cmd;

    struct {
        int *fds;
        size_t nr_fds;
        struct iovec iov;
    } in, out;

    struct iovec *out_iovecs;
    size_t nr_out_iovecs;
} vfu_msg_t;

typedef struct {
    int         err_efd;    /* eventfd for irq err */
    int         req_efd;    /* eventfd for irq req */
    uint32_t    max_ivs;    /* maximum number of ivs supported */
    int         efds[0];    /* must be last */
} vfu_irqs_t;

struct migration;

typedef struct  {
    /* Region flags, see VFU_REGION_FLAG_READ and friends. */
    uint32_t            flags;
    /* Size of the region. */
    uint32_t            size;
    /* Callback that is called when the region is read or written. */
    vfu_region_access_cb_t  *cb;
    /* Sparse mmap areas if set. */
    struct iovec *mmap_areas;
    int nr_mmap_areas;
    /* fd for a mappable region, or -1. */
    int fd;
    /* offset of region within fd. */
    uint64_t offset;
    /* The subregions for ioregionfds and ioeventfds */
    LIST_HEAD(, ioeventfd) subregions;
} vfu_reg_info_t;

struct pci_dev {
    vfu_pci_type_t          type;
    vfu_pci_config_space_t  *config_space;
    struct pci_cap          caps[VFU_MAX_CAPS];
    size_t                  nr_caps;
    struct pci_cap          ext_caps[VFU_MAX_CAPS];
    size_t                  nr_ext_caps;
};

struct dma_controller;

enum vfu_ctx_pending_state {
    VFU_CTX_PENDING_NONE,
    VFU_CTX_PENDING_MSG,
    VFU_CTX_PENDING_DEVICE_RESET,
    VFU_CTX_PENDING_CTX_RESET
};

struct vfu_ctx_pending_info {
    enum vfu_ctx_pending_state  state;
    vfu_msg_t                   *msg;

    /* when pending == VFU_CTX_PENDING_XXX_RESET */
    uint32_t                migr_dev_state;
};

enum cb_type {
    CB_NONE,
    CB_DMA_REGISTER,
    CB_DMA_UNREGISTER,
    CB_RESET,
    CB_QUIESCE,
    CB_MIGR_STATE
};

struct vfu_ctx {
    void                    *pvt;
    struct dma_controller   *dma;
    int                     log_level;
    vfu_log_fn_t            *log;
    size_t                  nr_regions;
    vfu_reg_info_t          *reg_info;
    struct pci_dev          pci;
    struct transport_ops    *tran;
    void                    *tran_data;
    uint64_t                flags;
    char                    *uuid;

    /* device callbacks */
    vfu_device_quiesce_cb_t *quiesce;
    vfu_reset_cb_t          *reset;
    vfu_dma_register_cb_t   *dma_register;
    vfu_dma_unregister_cb_t *dma_unregister;

    int                     client_max_fds;
    size_t                  client_max_data_xfer_size;

    struct vfu_ctx_pending_info pending;
    bool                    quiesced;
    enum cb_type            in_cb;

    struct migration        *migration;

    uint32_t                irq_count[VFU_DEV_NUM_IRQS];
    vfu_irqs_t              *irqs;
    bool                    realized;
    vfu_dev_type_t          dev_type;

    ssize_t                 pci_cap_exp_off;
};

typedef struct ioeventfd {
    uint64_t offset;
    uint64_t size;
    int32_t fd;
    uint32_t flags;
    uint64_t datamatch;
    LIST_ENTRY(ioeventfd) entry;
} ioeventfd_t;

static inline int
ERROR_INT(int err)
{
    errno = err;
    return -1;
}

static inline void *
ERROR_PTR(int err)
{
    errno = err;
    return NULL;
}

int
consume_fd(int *fds, size_t nr_fds, size_t index);

int
handle_dma_map(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg,
               struct vfio_user_dma_map *dma_map);

int
handle_dma_unmap(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg,
                 struct vfio_user_dma_unmap *dma_unmap);

int
handle_device_get_region_info(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg);

int
handle_device_reset(vfu_ctx_t *vfu_ctx, vfu_reset_type_t reason);

MOCK_DECLARE(bool, cmd_allowed_when_stopped_and_copying, uint16_t cmd);

MOCK_DECLARE(bool, should_exec_command, vfu_ctx_t *vfu_ctx, uint16_t cmd);

MOCK_DECLARE(ssize_t, migr_trans_to_valid_state, vfu_ctx_t *vfu_ctx,
             struct migration *migr, uint32_t device_state, bool notify);

#endif /* LIB_VFIO_USER_PRIVATE_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
