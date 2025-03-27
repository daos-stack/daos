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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <linux/vfio.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <sys/eventfd.h>

#include "dma.h"
#include "irq.h"
#include "libvfio-user.h"
#include "migration.h"
#include "pci.h"
#include "private.h"
#include "tran_pipe.h"
#include "tran_sock.h"

static int
vfu_reset_ctx(vfu_ctx_t *vfu_ctx, int reason);

EXPORT void
vfu_log(vfu_ctx_t *vfu_ctx, int level, const char *fmt, ...)
{
    va_list ap;
    char buf[BUFSIZ];
    int _errno = errno;

    assert(vfu_ctx != NULL);

    if (vfu_ctx->log == NULL || level > vfu_ctx->log_level || fmt == NULL) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    vfu_ctx->log(vfu_ctx, level, buf);
    errno = _errno;
}

static size_t
get_vfio_caps_size(bool is_migr_reg, vfu_reg_info_t *reg)
{
    size_t type_size = 0;
    size_t sparse_size = 0;

    if (is_migr_reg) {
        type_size = sizeof(struct vfio_region_info_cap_type);
    }

    if (reg->nr_mmap_areas != 0) {
        sparse_size = sizeof(struct vfio_region_info_cap_sparse_mmap)
                      + (reg->nr_mmap_areas * sizeof(struct vfio_region_sparse_mmap_area));
    }

    return type_size + sparse_size;
}

/*
 * Populate the sparse mmap capability information to vfio-client.
 * Sparse mmap information stays after struct vfio_region_info and cap_offset
 * points accordingly.
 */
static int
dev_get_caps(vfu_ctx_t *vfu_ctx, vfu_reg_info_t *vfu_reg, bool is_migr_reg,
             struct vfio_region_info *vfio_reg, int **fds, size_t *nr_fds)
{
    struct vfio_info_cap_header *header;
    struct vfio_region_info_cap_type *type = NULL;
    struct vfio_region_info_cap_sparse_mmap *sparse = NULL;

    assert(vfu_ctx != NULL);
    assert(vfio_reg != NULL);
    assert(fds != NULL);
    assert(nr_fds != NULL);

    header = (struct vfio_info_cap_header*)(vfio_reg + 1);

    if (is_migr_reg) {
        type = (struct vfio_region_info_cap_type *)header;
        type->header.id = VFIO_REGION_INFO_CAP_TYPE;
        type->header.version = 1;
        type->header.next = 0;
        type->type = VFIO_REGION_TYPE_MIGRATION;
        type->subtype = VFIO_REGION_SUBTYPE_MIGRATION;
        vfio_reg->cap_offset = sizeof(struct vfio_region_info);
    }

    if (vfu_reg->mmap_areas != NULL) {
        int i, nr_mmap_areas = vfu_reg->nr_mmap_areas;
        if (type != NULL) {
            type->header.next = vfio_reg->cap_offset + sizeof(struct vfio_region_info_cap_type);
            sparse = (struct vfio_region_info_cap_sparse_mmap*)(type + 1);
        } else {
            vfio_reg->cap_offset = sizeof(struct vfio_region_info);
            sparse = (struct vfio_region_info_cap_sparse_mmap*)header;
        }

        if (nr_mmap_areas > vfu_ctx->client_max_fds) {
            vfu_log(vfu_ctx, LOG_DEBUG, "%s: region has nr_mmap_areas=%d, "
                    "but client only supports %d fds", __func__,
                    nr_mmap_areas, vfu_ctx->client_max_fds);
            return ERROR_INT(ENOSPC);
        }

        *fds = malloc(nr_mmap_areas * sizeof(int));
        if (*fds == NULL) {
            return ERROR_INT(ENOMEM);
        }
        sparse->header.id = VFIO_REGION_INFO_CAP_SPARSE_MMAP;
        sparse->header.version = 1;
        sparse->header.next = 0;
        sparse->nr_areas = *nr_fds = nr_mmap_areas;

        for (i = 0; i < nr_mmap_areas; i++) {
            struct iovec *iov = &vfu_reg->mmap_areas[i];

            vfu_log(vfu_ctx, LOG_DEBUG, "%s: area %d [%p, %p)", __func__,
                    i, iov->iov_base, iov_end(iov));

            (*fds)[i] = vfu_reg->fd;
            sparse->areas[i].offset = (uintptr_t)iov->iov_base;
            sparse->areas[i].size = iov->iov_len;
        }
    }
    return 0;
}

static void
debug_region_access(vfu_ctx_t *vfu_ctx, size_t region, char *buf,
                    size_t count, uint64_t offset, bool is_write)
{
    const char *verb = is_write ? "wrote" : "read";
    uint64_t val;

    switch (count) {
    case 8: val = *((uint64_t *)buf); break;
    case 4: val = *((uint32_t *)buf); break;
    case 2: val = *((uint16_t *)buf); break;
    case 1: val = *((uint8_t *)buf); break;
    default:
            vfu_log(vfu_ctx, LOG_DEBUG, "region%zu: %s %zu bytes at %#lx",
                    region, verb, count, offset);
            return;
    }

    if (is_write) {
        vfu_log(vfu_ctx, LOG_DEBUG, "region%zu: wrote %#zx to (%#lx:%zu)",
                region, val, offset, count);
    } else {
        vfu_log(vfu_ctx, LOG_DEBUG, "region%zu: read %#zx from (%#lx:%zu)",
                region, val, offset, count);
    }
}

static ssize_t
region_access(vfu_ctx_t *vfu_ctx, size_t region, char *buf,
              size_t count, uint64_t offset, bool is_write)
{
    const char *verb = is_write ? "write to" : "read from";
    ssize_t ret;

    assert(vfu_ctx != NULL);
    assert(buf != NULL);

    if ((region == VFU_PCI_DEV_CFG_REGION_IDX) &&
        !(vfu_ctx->reg_info[region].flags & VFU_REGION_FLAG_ALWAYS_CB)) {
        ret = pci_config_space_access(vfu_ctx, buf, count, offset, is_write);
        if (ret == -1) {
            goto out;
        }
    } else if (region == VFU_PCI_DEV_MIGR_REGION_IDX) {
        if (vfu_ctx->migration == NULL) {
            vfu_log(vfu_ctx, LOG_ERR, "migration not enabled");
            ret = ERROR_INT(EINVAL);
            goto out;
        }

        ret = migration_region_access(vfu_ctx, buf, count, offset, is_write);
    } else {
        vfu_region_access_cb_t *cb = vfu_ctx->reg_info[region].cb;

        if (cb == NULL) {
            vfu_log(vfu_ctx, LOG_ERR, "no callback for region %zu", region);
            ret = ERROR_INT(EINVAL);
            goto out;
        }

        ret = cb(vfu_ctx, buf, count, offset, is_write);
    }

out:
    if (ret != (ssize_t)count) {
        vfu_log(vfu_ctx, LOG_DEBUG, "region%zu: %s (%#lx:%zu) failed: %m",
                region, verb, offset, count);
    } else {
        debug_region_access(vfu_ctx, region, buf, count, offset, is_write);
    }

    return ret;
}

static bool
is_valid_region_access(vfu_ctx_t *vfu_ctx, size_t size, uint16_t cmd,
                       struct vfio_user_region_access *ra)
{
    size_t index;

    assert(vfu_ctx != NULL);
    assert(ra != NULL);

    if (size < sizeof(*ra)) {
        vfu_log(vfu_ctx, LOG_ERR, "message size too small (%zu)", size);
        return false;
    }

    if (ra->count > SERVER_MAX_DATA_XFER_SIZE) {
        vfu_log(vfu_ctx, LOG_ERR, "region access count too large (%u)",
                ra->count);
        return false;
    }

    if (cmd == VFIO_USER_REGION_WRITE && size - sizeof(*ra) != ra->count) {
        vfu_log(vfu_ctx, LOG_ERR, "region write count too small: "
                "expected %lu, got %u", size - sizeof(*ra), ra->count);
        return false;
    }

    index = ra->region;

    if (index >= vfu_ctx->nr_regions) {
        vfu_log(vfu_ctx, LOG_ERR, "bad region index %zu", index);
        return false;
    }

    if (satadd_u64(ra->offset, ra->count) > vfu_ctx->reg_info[index].size) {
        vfu_log(vfu_ctx, LOG_ERR, "out of bounds region access %#lx-%#lx "
                "(size %u)", ra->offset, ra->offset + ra->count,
                vfu_ctx->reg_info[index].size);

        return false;
    }

    if (device_is_stopped_and_copying(vfu_ctx->migration) &&
        index != VFU_PCI_DEV_MIGR_REGION_IDX) {
        vfu_log(vfu_ctx, LOG_ERR,
                "cannot access region %zu while device in stop-and-copy state",
                index);
        return false;
    }

    return true;
}

static int
handle_region_access(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_user_region_access *in_ra = msg->in.iov.iov_base;
    struct vfio_user_region_access *out_ra;
    ssize_t ret;
    char *buf;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    if (!is_valid_region_access(vfu_ctx, msg->in.iov.iov_len, msg->hdr.cmd, in_ra)) {
        return ERROR_INT(EINVAL);
    }

    if (in_ra->count == 0) {
        return 0;
    }

    msg->out.iov.iov_len = sizeof(*in_ra);
    if (msg->hdr.cmd == VFIO_USER_REGION_READ) {
        msg->out.iov.iov_len += in_ra->count;
    }
    msg->out.iov.iov_base = calloc(1, msg->out.iov.iov_len);
    if (msg->out.iov.iov_base == NULL) {
        return -1;
    }

    out_ra = msg->out.iov.iov_base;
    out_ra->region = in_ra->region;
    out_ra->offset = in_ra->offset;
    out_ra->count = in_ra->count;

    if (msg->hdr.cmd == VFIO_USER_REGION_READ) {
        buf = (char *)(&out_ra->data);
    } else {
        buf = (char *)(&in_ra->data);
    }

    ret = region_access(vfu_ctx, in_ra->region, buf, in_ra->count,
                        in_ra->offset, msg->hdr.cmd == VFIO_USER_REGION_WRITE);
    if (ret != in_ra->count) {
        /* FIXME we should return whatever has been accessed, not an error */
        if (ret >= 0) {
            ret = ERROR_INT(EINVAL);
        }
        return ret;
    }

    out_ra->count = ret;

    return 0;
}

static int
handle_device_get_info(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_user_device_info *in_info;
    struct vfio_user_device_info *out_info;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    in_info = msg->in.iov.iov_base;

    if (msg->in.iov.iov_len < sizeof(*in_info) || in_info->argsz < sizeof(*out_info)) {
        return ERROR_INT(EINVAL);
    }

    msg->out.iov.iov_len = sizeof (*out_info);
    msg->out.iov.iov_base = calloc(1, sizeof(*out_info));

    if (msg->out.iov.iov_base == NULL) {
        return -1;
    }

    out_info = msg->out.iov.iov_base;
    out_info->argsz = sizeof(*out_info);
    out_info->flags = VFIO_DEVICE_FLAGS_PCI | VFIO_DEVICE_FLAGS_RESET;
    out_info->num_regions = vfu_ctx->nr_regions;
    out_info->num_irqs = VFU_DEV_NUM_IRQS;

    vfu_log(vfu_ctx, LOG_DEBUG, "devinfo flags %#x, num_regions %d, "
            "num_irqs %d", out_info->flags, out_info->num_regions,
            out_info->num_irqs);

    return 0;
}

int
handle_device_get_region_info(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_region_info *in_info;
    struct vfio_region_info *out_info;
    vfu_reg_info_t *vfu_reg;
    size_t caps_size = 0;
    int ret;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    in_info = msg->in.iov.iov_base;

    if (msg->in.iov.iov_len < sizeof(*in_info) || in_info->argsz < sizeof(*out_info)) {
        return ERROR_INT(EINVAL);
    }

    if (in_info->index >= vfu_ctx->nr_regions) {
        vfu_log(vfu_ctx, LOG_DEBUG, "bad region index %d in get region info",
                in_info->index);
        return ERROR_INT(EINVAL);
    }

    vfu_reg = &vfu_ctx->reg_info[in_info->index];

    if (vfu_reg->size > 0) {
        caps_size = get_vfio_caps_size(in_info->index == VFU_PCI_DEV_MIGR_REGION_IDX,
                                       vfu_reg);
    }

    msg->out.iov.iov_len = MIN(sizeof(*out_info) + caps_size, in_info->argsz);
    msg->out.iov.iov_base = calloc(1, msg->out.iov.iov_len);

    if (msg->out.iov.iov_base == NULL) {
        return -1;
    }

    out_info = msg->out.iov.iov_base;

    /* This might be more than the buffer we actually return. */
    out_info->argsz = sizeof(*out_info) + caps_size;
    out_info->index = in_info->index;
    out_info->offset = vfu_reg->offset;
    out_info->size = vfu_reg->size;

    out_info->flags = 0;

    if (vfu_reg->flags & VFU_REGION_FLAG_READ) {
        out_info->flags |= VFIO_REGION_INFO_FLAG_READ;
    }
    if (vfu_reg->flags & VFU_REGION_FLAG_WRITE) {
        out_info->flags |= VFIO_REGION_INFO_FLAG_WRITE;
    }

    if (vfu_reg->fd != -1) {
        out_info->flags |= VFIO_REGION_INFO_FLAG_MMAP;
    }

    if (caps_size > 0) {
        /* Only actually provide the caps if they fit. */
        if (in_info->argsz >= out_info->argsz) {
            out_info->flags |= VFIO_REGION_INFO_FLAG_CAPS;
            ret = dev_get_caps(vfu_ctx, vfu_reg,
                               in_info->index == VFU_PCI_DEV_MIGR_REGION_IDX,
                               out_info, &msg->out.fds, &msg->out.nr_fds);
            if (ret < 0) {
                return ret;
            }
        }
    }

    vfu_log(vfu_ctx, LOG_DEBUG, "region_info[%d] offset %#llx flags %#x "
            "size %llu " "argsz %u", out_info->index, out_info->offset,
            out_info->flags, out_info->size, out_info->argsz);

    return 0;
}

EXPORT int
vfu_create_ioeventfd(vfu_ctx_t *vfu_ctx, uint32_t region_idx, int fd,
                     size_t offset, uint32_t size, uint32_t flags,
                     uint64_t datamatch)
{
    vfu_reg_info_t *vfu_reg;

    assert(vfu_ctx != NULL);
    assert(fd >= 0);

    if (region_idx >= VFU_PCI_DEV_NUM_REGIONS) {
        return ERROR_INT(EINVAL);
    }

    vfu_reg = &vfu_ctx->reg_info[region_idx];

    if (offset + size > vfu_reg->size) {
        return ERROR_INT(EINVAL);
    }

    ioeventfd_t *elem = malloc(sizeof(ioeventfd_t));
    if (elem == NULL) {
        return -1;
    }

    elem->fd = fd;
    elem->offset = offset;
    elem->size = size;
    elem->flags = flags;
    elem->datamatch = datamatch;
    LIST_INSERT_HEAD(&vfu_reg->subregions, elem, entry);

    return 0;
}

static void
free_regions(vfu_ctx_t *vfu_ctx)
{
    size_t index = 0;

    assert(vfu_ctx != NULL);

    for (index = 0; index < VFU_PCI_DEV_NUM_REGIONS; index++) {
        vfu_reg_info_t *vfu_reg = &vfu_ctx->reg_info[index];

        while (!LIST_EMPTY(&vfu_reg->subregions)) {
            ioeventfd_t *n = LIST_FIRST(&vfu_reg->subregions);
            LIST_REMOVE(n, entry);
            free(n);
        }
    }
    free(vfu_ctx->reg_info);
}

/*
 * This function is used to add fd's to the fd return array and gives you back
 * the index of the fd that has been added. If the fd is already present it will
 * return the index to that duplicate fd to reduce the number of fd's sent.
 */
static int
add_fd_index(int *out_fds, size_t *nr_out_fds, int fd_search)
{
    size_t i = 0;

    assert(out_fds != NULL);
    assert(nr_out_fds != NULL);

    for (i = 0; i < *nr_out_fds; i++) {
        if (out_fds[i] == fd_search) {
            return i;
        }
    }

    out_fds[*nr_out_fds] = fd_search;
    (*nr_out_fds)++;

    return *nr_out_fds - 1;
}

static int
handle_device_get_region_io_fds(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    size_t max_sent_sub_regions = 0;
    uint subregion_array_size = 0;
    vfu_reg_info_t *vfu_reg = NULL;
    vfio_user_region_io_fds_reply_t *reply = NULL;
    vfio_user_sub_region_ioeventfd_t *ioefd = NULL;
    vfio_user_region_io_fds_request_t *req = NULL;
    ioeventfd_t *sub_reg = NULL;
    size_t nr_sub_reg = 0;
    size_t i = 0;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);
    assert(msg->out.fds == NULL);

    if (msg->in.iov.iov_len < sizeof(vfio_user_region_io_fds_request_t)) {
        return ERROR_INT(EINVAL);
    }

    req = msg->in.iov.iov_base;

    if (req->flags != 0 || req->count != 0) {
        return ERROR_INT(EINVAL);
    }

    if (req->index >= vfu_ctx->nr_regions) {
        vfu_log(vfu_ctx, LOG_DEBUG, "bad region index %d in get region io fds "
                "info", req->index);
        return ERROR_INT(EINVAL);
    }

    vfu_reg = &vfu_ctx->reg_info[req->index];

    // At least one flag must be set for a valid region.
    if (!(vfu_reg->flags & VFU_REGION_FLAG_MASK)) {
        return ERROR_INT(EINVAL);
    }

    LIST_FOREACH(sub_reg, &vfu_reg->subregions, entry) {
        nr_sub_reg++;
    }

    if (req->argsz < sizeof(vfio_user_region_io_fds_reply_t) ||
        req->argsz > SERVER_MAX_DATA_XFER_SIZE) {
        return ERROR_INT(EINVAL);
    }

    max_sent_sub_regions = MIN((req->argsz -
                                sizeof(vfio_user_region_io_fds_reply_t)) /
                                sizeof(vfio_user_sub_region_ioeventfd_t),
                                nr_sub_reg);
    subregion_array_size = ((max_sent_sub_regions >= nr_sub_reg) ? nr_sub_reg :
                             0) * sizeof(vfio_user_sub_region_ioeventfd_t);
    msg->out.iov.iov_len = sizeof(vfio_user_region_io_fds_reply_t)
                           + subregion_array_size;
    msg->out.iov.iov_base = calloc(1, msg->out.iov.iov_len);
    if (msg->out.iov.iov_base == NULL) {
        return -1;
    }
    reply = msg->out.iov.iov_base;
    reply->index = req->index;
    reply->count = nr_sub_reg;
    reply->flags = 0;
    reply->argsz = sizeof(vfio_user_region_io_fds_reply_t) +
                          nr_sub_reg *
                          sizeof(vfio_user_sub_region_ioeventfd_t);

    msg->out.nr_fds = 0;
    if (req->argsz >= reply->argsz) {
        msg->out.fds = calloc(sizeof(int), max_sent_sub_regions);
        if (msg->out.fds == NULL) {
            return -1;
        }

        sub_reg = LIST_FIRST(&vfu_reg->subregions);
        for (i = 0; i < max_sent_sub_regions; i++) {

            ioefd = &reply->sub_regions[i].ioeventfd;
            ioefd->offset = sub_reg->offset;
            ioefd->size = sub_reg->size;
            ioefd->fd_index = add_fd_index(msg->out.fds, &msg->out.nr_fds,
                                        sub_reg->fd);
            ioefd->type = VFIO_USER_IO_FD_TYPE_IOEVENTFD;
            ioefd->flags = sub_reg->flags;
            ioefd->datamatch = sub_reg->datamatch;

            sub_reg = LIST_NEXT(sub_reg, entry);
        }
    }

    return 0;
}

int
consume_fd(int *fds, size_t nr_fds, size_t index)
{
   int fd;

   if (index >= nr_fds) {
       return ERROR_INT(EINVAL);
   }

   fd = fds[index];
   fds[index] = -1;
   return fd;
}

int
handle_dma_map(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg,
               struct vfio_user_dma_map *dma_map)
{
    char rstr[1024];
    int fd = -1;
    int ret;
    uint32_t prot = 0;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);
    assert(dma_map != NULL);

    if (msg->in.iov.iov_len < sizeof(*dma_map) || dma_map->argsz < sizeof(*dma_map)) {
        vfu_log(vfu_ctx, LOG_ERR, "bad DMA map region size=%zu argsz=%u",
                msg->in.iov.iov_len, dma_map->argsz);
        return ERROR_INT(EINVAL);
    }

    snprintf(rstr, sizeof(rstr), "[%#lx, %#lx) offset=%#lx flags=%#x",
             dma_map->addr, dma_map->addr + dma_map->size, dma_map->offset,
             dma_map->flags);

    vfu_log(vfu_ctx, LOG_DEBUG, "adding DMA region %s", rstr);

    if (dma_map->flags & VFIO_USER_F_DMA_REGION_READ) {
        prot |= PROT_READ;
        dma_map->flags &= ~VFIO_USER_F_DMA_REGION_READ;
    }

    if (dma_map->flags & VFIO_USER_F_DMA_REGION_WRITE) {
        prot |= PROT_WRITE;
        dma_map->flags &= ~VFIO_USER_F_DMA_REGION_WRITE;
    }

    if (dma_map->flags != 0) {
        vfu_log(vfu_ctx, LOG_ERR, "bad flags=%#x", dma_map->flags);
        return ERROR_INT(EINVAL);
    }

    if (msg->in.nr_fds > 0) {
        fd = consume_fd(msg->in.fds, msg->in.nr_fds, 0);
        if (fd < 0) {
            vfu_log(vfu_ctx, LOG_ERR, "failed to add DMA region %s: %m", rstr);
            return -1;
        }
    }

    ret = dma_controller_add_region(vfu_ctx->dma, (void *)dma_map->addr,
                                    dma_map->size, fd, dma_map->offset,
                                    prot);
    if (ret < 0) {
        ret = errno;
        vfu_log(vfu_ctx, LOG_ERR, "failed to add DMA region %s: %m", rstr);
        if (fd != -1) {
            close(fd);
        }
        return ERROR_INT(ret);
    }

    if (vfu_ctx->dma_register != NULL) {
        vfu_ctx->in_cb = CB_DMA_REGISTER;
        vfu_ctx->dma_register(vfu_ctx, &vfu_ctx->dma->regions[ret].info);
        vfu_ctx->in_cb = CB_NONE;

    }
    return 0;
}

/*
* Ideally, if argsz is too small for the bitmap, we should set argsz in the
* reply and fail the request with a struct vfio_user_dma_unmap payload.
* Instead, we simply fail the request - that's what VFIO does anyway.
*/
static bool
is_valid_unmap(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg,
               struct vfio_user_dma_unmap *dma_unmap)
{
    size_t struct_size = sizeof(*dma_unmap);
    size_t min_argsz = sizeof(*dma_unmap);

    switch (dma_unmap->flags) {
    case VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP:
        struct_size += sizeof(*dma_unmap->bitmap);
        /*
         * Because the saturating add will ensure that any overflow will be
         * larger than the maximum allowed ->argsz, this is sufficient to check
         * for that (which we need, because we are about to allocate based upon
         * this value).
         */
        min_argsz = satadd_u64(struct_size, dma_unmap->bitmap->size);
        break;

    case VFIO_DMA_UNMAP_FLAG_ALL:
        if (dma_unmap->addr || dma_unmap->size) {
            vfu_log(vfu_ctx, LOG_ERR, "bad addr=%#lx or size=%#lx, expected "
                    "both to be zero", dma_unmap->addr, dma_unmap->size);
            errno = EINVAL;
            return false;
        }
        break;

    case 0:
        break;

    default:
        vfu_log(vfu_ctx, LOG_ERR, "invalid DMA flags=%#x", dma_unmap->flags);
        errno = EINVAL;
        return false;
    }

    if (msg->in.iov.iov_len < struct_size ||
        dma_unmap->argsz < min_argsz ||
        dma_unmap->argsz > SERVER_MAX_DATA_XFER_SIZE) {
        vfu_log(vfu_ctx, LOG_ERR, "bad DMA unmap region size=%zu argsz=%u",
                msg->in.iov.iov_len, dma_unmap->argsz);
        errno = EINVAL;
        return false;
    }

    return true;
}

int
handle_dma_unmap(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg,
                 struct vfio_user_dma_unmap *dma_unmap)
{
    size_t out_size;
    int ret = 0;
    char rstr[1024];

    assert(vfu_ctx != NULL);
    assert(msg != NULL);
    assert(dma_unmap != NULL);

    if (!is_valid_unmap(vfu_ctx, msg, dma_unmap)) {
        return -1;
    }

    snprintf(rstr, sizeof(rstr), "[%#lx, %#lx) flags=%#x",
             dma_unmap->addr, dma_unmap->addr + dma_unmap->size, dma_unmap->flags);

    vfu_log(vfu_ctx, LOG_DEBUG, "removing DMA region %s", rstr);

    out_size = sizeof(*dma_unmap);

    if (dma_unmap->flags == VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP) {
        out_size += sizeof(*dma_unmap->bitmap) + dma_unmap->bitmap->size;
    }

    msg->out.iov.iov_base = malloc(out_size);
    if (msg->out.iov.iov_base == NULL) {
        return ERROR_INT(ENOMEM);
    }
    memcpy(msg->out.iov.iov_base, dma_unmap, sizeof(*dma_unmap));

    if (dma_unmap->flags == VFIO_DMA_UNMAP_FLAG_ALL) {
        dma_controller_remove_all_regions(vfu_ctx->dma,
                                          vfu_ctx->dma_unregister, vfu_ctx);
        goto out;
    }

    if (dma_unmap->flags & VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP) {
        memcpy(msg->out.iov.iov_base + sizeof(*dma_unmap), dma_unmap->bitmap, sizeof(*dma_unmap->bitmap));
        ret = dma_controller_dirty_page_get(vfu_ctx->dma,
                                            (vfu_dma_addr_t)dma_unmap->addr,
                                            dma_unmap->size,
                                            dma_unmap->bitmap->pgsize,
                                            dma_unmap->bitmap->size,
                                            msg->out.iov.iov_base + sizeof(*dma_unmap) + sizeof(*dma_unmap->bitmap));
        if (ret < 0) {
            vfu_log(vfu_ctx, LOG_ERR, "failed to get dirty page bitmap: %m");
            return -1;
        }
    }

    ret = dma_controller_remove_region(vfu_ctx->dma,
                                       (void *)dma_unmap->addr,
                                       dma_unmap->size,
                                       vfu_ctx->dma_unregister,
                                       vfu_ctx);
    if (ret < 0) {
        ret = errno;
        vfu_log(vfu_ctx, LOG_WARNING,
                "failed to remove DMA region %s: %m", rstr);
        return ERROR_INT(ret);
    }

out:
    msg->out.iov.iov_len = out_size;

    return ret;
}

static int
do_device_reset(vfu_ctx_t *vfu_ctx, vfu_reset_type_t reason)
{
    int ret;

    if (vfu_ctx->reset != NULL) {
        vfu_ctx->in_cb = CB_RESET;
        ret = vfu_ctx->reset(vfu_ctx, reason);
        vfu_ctx->in_cb = CB_NONE;
        if (ret < 0) {
            return ret;
        }
    }
    if (vfu_ctx->migration != NULL) {
        return handle_device_state(vfu_ctx, vfu_ctx->migration,
                                   VFIO_DEVICE_STATE_V1_RUNNING, false);
    }
    return 0;
}

int
handle_device_reset(vfu_ctx_t *vfu_ctx, vfu_reset_type_t reason)
{
    return do_device_reset(vfu_ctx, reason);
}

static int
handle_dirty_pages_get(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_user_dirty_pages *dirty_pages_in;
    struct vfio_user_dirty_pages *dirty_pages_out;
    struct vfio_user_bitmap_range *range_in;
    struct vfio_user_bitmap_range *range_out;
    size_t argsz;
    int ret;


    dirty_pages_in = msg->in.iov.iov_base;

    if (msg->in.iov.iov_len < sizeof(*dirty_pages_in) + sizeof(*range_in) ||
        dirty_pages_in->argsz > SERVER_MAX_DATA_XFER_SIZE ||
        dirty_pages_in->argsz < sizeof(*dirty_pages_out)) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid message size=%zu argsz=%u",
                msg->in.iov.iov_len, dirty_pages_in->argsz);
        return ERROR_INT(EINVAL);
    }

    range_in = msg->in.iov.iov_base + sizeof(*dirty_pages_in);

    /*
     * range_in is client-controlled, but we only need to protect against
     * overflow here: we'll take MIN() against a validated value next, and
     * dma_controller_dirty_page_get() will validate the actual ->bitmap.size
     * value later, anyway.
     */
    argsz = satadd_u64(sizeof(*dirty_pages_out) + sizeof(*range_out),
                       range_in->bitmap.size);

    msg->out.iov.iov_len = MIN(dirty_pages_in->argsz, argsz);
    msg->out.iov.iov_base = malloc(msg->out.iov.iov_len);
    if (msg->out.iov.iov_base == NULL) {
        return -1;
    }
    dirty_pages_out = msg->out.iov.iov_base;
    memcpy(dirty_pages_out, dirty_pages_in, sizeof(*dirty_pages_out));
    dirty_pages_out->argsz = argsz;

    /*
     * If the reply doesn't fit, reply with just the dirty pages header, giving
     * the needed argsz. Typically this shouldn't happen, as the client knows
     * the needed reply size and has already provided the correct bitmap size.
     */
    if (dirty_pages_in->argsz >= argsz) {
        void *bitmap_out = msg->out.iov.iov_base + sizeof(*dirty_pages_out)
                           + sizeof(*range_out);
        range_out = msg->out.iov.iov_base + sizeof(*dirty_pages_out);
        memcpy(range_out, range_in, sizeof(*range_out));
        ret = dma_controller_dirty_page_get(vfu_ctx->dma,
                                            (vfu_dma_addr_t)range_in->iova,
                                            range_in->size,
                                            range_in->bitmap.pgsize,
                                            range_in->bitmap.size, bitmap_out);
        if (ret != 0) {
            ret = errno;
            vfu_log(vfu_ctx, LOG_WARNING,
                    "failed to get dirty bitmap from DMA controller: %m");
            free(msg->out.iov.iov_base);
            msg->out.iov.iov_base = NULL;
            msg->out.iov.iov_len = 0;
            return ERROR_INT(ret);
        }
    } else {
        vfu_log(vfu_ctx, LOG_ERR,
                "dirty pages: get [%#lx, %#lx): buffer too small (%u < %lu)",
                range_in->iova, range_in->iova + range_in->size,
                dirty_pages_in->argsz, argsz);
    }

    return 0;
}

static int
handle_dirty_pages(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_user_dirty_pages *dirty_pages = msg->in.iov.iov_base;
    int ret;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    if (msg->in.iov.iov_len < sizeof(*dirty_pages) ||
        dirty_pages->argsz < sizeof(*dirty_pages)) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid message size %zu", msg->in.iov.iov_len);
        return ERROR_INT(EINVAL);
    }

    if (vfu_ctx->migration == NULL) {
        vfu_log(vfu_ctx, LOG_ERR, "migration not configured");
        return ERROR_INT(ENOTSUP);
    }

    switch (dirty_pages->flags) {
    case VFIO_IOMMU_DIRTY_PAGES_FLAG_START:
        ret = dma_controller_dirty_page_logging_start(vfu_ctx->dma,
                  migration_get_pgsize(vfu_ctx->migration));
        break;

    case VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP:
        dma_controller_dirty_page_logging_stop(vfu_ctx->dma);
        ret = 0;
        break;

    case VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP:
        ret = handle_dirty_pages_get(vfu_ctx, msg);
        break;

    default:
        vfu_log(vfu_ctx, LOG_ERR, "bad flags %#x", dirty_pages->flags);
        ret = ERROR_INT(EINVAL);
        break;
    }

    return ret;
}

static vfu_msg_t *
alloc_msg(struct vfio_user_header *hdr, int *fds, size_t nr_fds)
{
    vfu_msg_t *msg;
    size_t i;

    msg = calloc(1, sizeof(*msg));

    if (msg == NULL) {
        return NULL;
    }

    msg->hdr = *hdr;
    msg->in.nr_fds = nr_fds;

    if (nr_fds > 0) {
        msg->in.fds = calloc(msg->in.nr_fds, sizeof(int));

        if (msg->in.fds == NULL) {
            free(msg);
            return NULL;
        }

        for (i = 0; i < msg->in.nr_fds; i++) {
            msg->in.fds[i] = fds[i];
        }
    }

    return msg;
}

static void
free_msg(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    int saved_errno = errno;
    size_t i;

    if (msg == NULL) {
        return;
    }

    free(msg->in.iov.iov_base);

    for (i = 0; i < msg->in.nr_fds; i++) {
        if (msg->in.fds[i] != -1) {
            if (msg->processed_cmd) {
                vfu_log(vfu_ctx, LOG_DEBUG,
                        "closing unexpected fd %d (index %zu) from cmd %u",
                        msg->in.fds[i], i, msg->hdr.cmd);
            }
            close(msg->in.fds[i]);
        }
    }

    free(msg->in.fds);
    free(msg->out.fds);

    assert(msg->out.iov.iov_base == NULL || msg->out_iovecs == NULL);

    free(msg->out.iov.iov_base);

    /*
     * Each iov_base refers to data we don't want to free, but we *do* want to
     * free the allocated array of iovecs if there is one.
     */
    free(msg->out_iovecs);

    free(msg);

    errno = saved_errno;
}

static int
do_reply(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg, int reply_errno)
{
    int ret;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    if (msg->hdr.flags.no_reply) {
        /*
         * A failed client request is not a failure of handle_request() itself.
         */
        return 0;
    }

    ret = vfu_ctx->tran->reply(vfu_ctx, msg, reply_errno);

    if (ret < 0) {
        vfu_log(vfu_ctx, LOG_ERR, "failed to reply: %m");

        if (errno == ECONNRESET || errno == ENOMSG) {
            ret = vfu_reset_ctx(vfu_ctx, errno);
            if (ret < 0) {
                if (errno != EBUSY) {
                    vfu_log(vfu_ctx, LOG_WARNING, "failed to reset context: %m");
                }
                return ret;
            }
            errno = ENOTCONN;
        }
    }

    return ret;
}

static int
handle_request(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    int ret = 0;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    msg->processed_cmd = true;

    switch (msg->hdr.cmd) {
    case VFIO_USER_DMA_MAP:
        if (vfu_ctx->dma != NULL) {
            ret = handle_dma_map(vfu_ctx, msg, msg->in.iov.iov_base);
        }
        break;

    case VFIO_USER_DMA_UNMAP:
        if (vfu_ctx->dma != NULL) {
            ret = handle_dma_unmap(vfu_ctx, msg, msg->in.iov.iov_base);
        }
        break;

    case VFIO_USER_DEVICE_GET_INFO:
        ret = handle_device_get_info(vfu_ctx, msg);
        break;

    case VFIO_USER_DEVICE_GET_REGION_INFO:
        ret = handle_device_get_region_info(vfu_ctx, msg);
        break;

    case VFIO_USER_DEVICE_GET_REGION_IO_FDS:
        ret = handle_device_get_region_io_fds(vfu_ctx, msg);
        break;

    case VFIO_USER_DEVICE_GET_IRQ_INFO:
        ret = handle_device_get_irq_info(vfu_ctx, msg);
        break;

    case VFIO_USER_DEVICE_SET_IRQS:
        ret = handle_device_set_irqs(vfu_ctx, msg);
        break;

    case VFIO_USER_REGION_READ:
    case VFIO_USER_REGION_WRITE:
        ret = handle_region_access(vfu_ctx, msg);
        break;

    case VFIO_USER_DEVICE_RESET:
        vfu_log(vfu_ctx, LOG_INFO, "device reset by client");
        ret = handle_device_reset(vfu_ctx, VFU_RESET_DEVICE);
        break;

    case VFIO_USER_DIRTY_PAGES:
        // FIXME: don't allow migration calls if migration == NULL
        if (vfu_ctx->dma != NULL) {
            ret = handle_dirty_pages(vfu_ctx, msg);
        } else {
            ret = 0;
        }
        break;

    default:
        msg->processed_cmd = false;
        vfu_log(vfu_ctx, LOG_ERR, "bad command %d", msg->hdr.cmd);
        ret = ERROR_INT(EINVAL);
        break;
    }

    if (ret < 0) {
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: cmd %d failed: %m",
                msg->hdr.msg_id, msg->hdr.cmd);
    }

    return do_reply(vfu_ctx, msg, ret == 0 ? 0 : errno);
}

/*
 * Note that we avoid any malloc() before we see data, as this is used for
 * polling by SPDK.
 */
static int
get_request_header(vfu_ctx_t *vfu_ctx, vfu_msg_t **msgp)
{
    int fds[VFIO_USER_CLIENT_MAX_MSG_FDS_LIMIT] = { 0 };
    struct vfio_user_header hdr = { 0, };
    size_t nr_fds = VFIO_USER_CLIENT_MAX_MSG_FDS_LIMIT;
    size_t i;
    int ret;

    ret = vfu_ctx->tran->get_request_header(vfu_ctx, &hdr, fds, &nr_fds);

    if (unlikely(ret < 0)) {
        switch (errno) {
        case EAGAIN:
            return -1;

        case ENOMSG:
        case ECONNRESET:
            vfu_log(vfu_ctx, LOG_DEBUG, "failed to receive request header: %m");
            ret = vfu_reset_ctx(vfu_ctx, errno);
            if (ret < 0) {
                if (errno != EBUSY) {
                    vfu_log(vfu_ctx, LOG_WARNING, "failed to reset context: %m");
                }
                return ret;
            }
            return ERROR_INT(ENOTCONN);
        default:
            vfu_log(vfu_ctx, LOG_ERR, "failed to receive request: %m");
            return -1;
        }
    }

    *msgp = alloc_msg(&hdr, fds, nr_fds);

    if (*msgp == NULL) {
        int saved_errno = errno;
        for (i = 0; i < nr_fds; i++) {
            close(fds[i]);
        }
        errno = saved_errno;
        return -1;
    }

    return 0;
}

static bool
is_valid_header(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    if (msg->hdr.flags.type != VFIO_USER_F_TYPE_COMMAND) {
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: not a command req",
                msg->hdr.msg_id);
        return false;
    }

    if (msg->hdr.msg_size < sizeof(msg->hdr)) {
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: bad size %u in header",
                msg->hdr.msg_id, msg->hdr.msg_size);
        return false;
    } else if (msg->hdr.msg_size == sizeof(msg->hdr) &&
               msg->hdr.cmd != VFIO_USER_DEVICE_RESET) {
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: no payload for cmd%u",
                msg->hdr.msg_id, msg->hdr.cmd);
        return false;
    } else if (msg->hdr.msg_size > SERVER_MAX_MSG_SIZE) {
        /*
         * We know we can reject this: all normal requests shouldn't need this
         * amount of space, including VFIO_USER_REGION_WRITE, which should be
         * bound by max_data_xfer_size.
         */
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: size of %u is too large",
                msg->hdr.msg_id, msg->hdr.msg_size);
        return false;
    }

    return true;
}

bool
MOCK_DEFINE(cmd_allowed_when_stopped_and_copying)(uint16_t cmd)
{
    return cmd == VFIO_USER_REGION_READ ||
           cmd == VFIO_USER_REGION_WRITE ||
           cmd == VFIO_USER_DIRTY_PAGES;
}

bool
MOCK_DEFINE(should_exec_command)(vfu_ctx_t *vfu_ctx, uint16_t cmd)
{
    if (device_is_stopped_and_copying(vfu_ctx->migration)) {
        if (!cmd_allowed_when_stopped_and_copying(cmd)) {
            vfu_log(vfu_ctx, LOG_ERR,
                    "bad command %d while device in stop-and-copy state", cmd);
            return false;
        }
    } else if (device_is_stopped(vfu_ctx->migration)) {
        if (!cmd_allowed_when_stopped_and_copying(cmd)) {
            vfu_log(vfu_ctx, LOG_ERR,
                   "bad command %d while device in stopped state", cmd);
            return false;
        }
    }
    return true;
}

static bool
access_needs_quiesce(const vfu_ctx_t *vfu_ctx, size_t region_index,
                     uint64_t offset)
{
    return access_migration_needs_quiesce(vfu_ctx, region_index, offset)
           || access_is_pci_cap_exp(vfu_ctx, region_index, offset);
}

static bool
command_needs_quiesce(vfu_ctx_t *vfu_ctx, const vfu_msg_t *msg)
{
    struct vfio_user_region_access *reg;

    if (vfu_ctx->quiesce == NULL) {
        return false;
    }

    switch (msg->hdr.cmd) {
    case VFIO_USER_DMA_MAP:
    case VFIO_USER_DMA_UNMAP:
        return vfu_ctx->dma != NULL;

    case VFIO_USER_DEVICE_RESET:
        return true;

    case VFIO_USER_REGION_WRITE:
        if (msg->in.iov.iov_len < sizeof(*reg)) {
            /*
             * bad request, it will be eventually failed by
             * handle_region_access
             *
             */
            return false;
        }
        reg = msg->in.iov.iov_base;
        if (access_needs_quiesce(vfu_ctx, reg->region, reg->offset)) {
            return true;
        }
        break;
    }

    return false;
}

/*
 * Acquire a request from the vfio-user socket. Returns 0 on success, or -1 with
 * errno set as follows:
 *
 * EAGAIN/EWOULDBLOCK: no request was ready to read from the socket
 * ENOMSG: a message was read and replied to, no further handling is needed.
 * E*: other errors that should be returned to the caller
 */
static int
get_request(vfu_ctx_t *vfu_ctx, vfu_msg_t **msgp)
{
    vfu_msg_t *msg = NULL;
    int ret;

    assert(vfu_ctx != NULL);

    *msgp = NULL;

    ret = get_request_header(vfu_ctx, &msg);

    if (ret < 0) {
        return ret;
    }

    if (!is_valid_header(vfu_ctx, msg)) {
        ret = ERROR_INT(EINVAL);
        goto err;
    }

    msg->in.iov.iov_len = msg->hdr.msg_size - sizeof(msg->hdr);

    if (msg->in.iov.iov_len > 0) {
        ret = vfu_ctx->tran->recv_body(vfu_ctx, msg);

        if (ret < 0) {
            goto err;
        }
    }

    if (!should_exec_command(vfu_ctx, msg->hdr.cmd)) {
        ret = ERROR_INT(EINVAL);
        goto err;
    }

    if (command_needs_quiesce(vfu_ctx, msg)) {
        vfu_log(vfu_ctx, LOG_DEBUG, "quiescing device");
        vfu_ctx->in_cb = CB_QUIESCE;
        ret = vfu_ctx->quiesce(vfu_ctx);
        vfu_ctx->in_cb = CB_NONE;
        if (ret < 0) {
            if (errno != EBUSY) {
                vfu_log(vfu_ctx, LOG_DEBUG, "device failed to quiesce: %m");
                goto err;
            }

            vfu_log(vfu_ctx, LOG_DEBUG, "device will quiesce asynchronously");
            vfu_ctx->pending.state = VFU_CTX_PENDING_MSG;
            vfu_ctx->pending.msg = msg;
            /* NB the message is freed in vfu_device_quiesced */
            return ret;
        }

        vfu_log(vfu_ctx, LOG_DEBUG, "device quiesced immediately");
        vfu_ctx->quiesced = true;
    }

    *msgp = msg;
    return 0;

err:
    ret = do_reply(vfu_ctx, msg, ret == 0 ? 0 : errno);
    free_msg(vfu_ctx, msg);
    if (ret != 0) {
        return ret;
    }

    /* We handled the message already. */
    return ERROR_INT(ENOMSG);
}

EXPORT int
vfu_run_ctx(vfu_ctx_t *vfu_ctx)
{
    int reqs_processed = 0;
    bool blocking;
    int err;

    assert(vfu_ctx != NULL);

    if (!vfu_ctx->realized) {
        vfu_log(vfu_ctx, LOG_DEBUG, "device not realized");
        return ERROR_INT(EINVAL);
    }

    blocking = !(vfu_ctx->flags & LIBVFIO_USER_FLAG_ATTACH_NB);

    do {
        vfu_msg_t *msg;

        if (vfu_ctx->pending.state != VFU_CTX_PENDING_NONE) {
            return ERROR_INT(EBUSY);
        }

        err = get_request(vfu_ctx, &msg);

        if (err == 0) {
            err = handle_request(vfu_ctx, msg);
            free_msg(vfu_ctx, msg);
            reqs_processed++;
            /*
             * get_request might call the quiesce callback which might
             * immediately quiesce the device, vfu_device_quiesced won't
             * be called at all.
             */
            if (vfu_ctx->quiesced) {
                vfu_log(vfu_ctx, LOG_DEBUG, "device unquiesced");
                vfu_ctx->quiesced = false;
            }
        } else {
            /*
             * If there was no request to read, or we already handled the
             * (error) reply, that's not a failure of vfu_run_ctx() itself.
             */
            switch (errno) {
            case ENOMSG:
            case EAGAIN:
                err = 0;
                break;
            }
        }
    } while (err == 0 && blocking);

    return err == 0 ? reqs_processed : err;
}

EXPORT int
vfu_realize_ctx(vfu_ctx_t *vfu_ctx)
{
    vfu_reg_info_t *cfg_reg;
    uint32_t max_ivs = 0, i;
    size_t size;

    if (vfu_ctx->realized) {
        return 0;
    }

    cfg_reg = &vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX];

    // Set a default config region if none provided.
    if (cfg_reg->size == 0) {
        cfg_reg->flags = VFU_REGION_FLAG_RW;
        cfg_reg->size = PCI_CFG_SPACE_SIZE;
    }

    // This may have been allocated by vfu_setup_pci_config_hdr().
    if (vfu_ctx->pci.config_space == NULL) {
        vfu_ctx->pci.config_space = calloc(1, cfg_reg->size);
        if (vfu_ctx->pci.config_space == NULL) {
            return ERROR_INT(ENOMEM);
        }
    }

    // Set type for region registers.
    for (i = 0; i < PCI_BARS_NR; i++) {
        if (!(vfu_ctx->reg_info[i].flags & VFU_REGION_FLAG_MEM)) {
            vfu_ctx->pci.config_space->hdr.bars[i].io.region_type |= 0x1;
        }
    }

    if (vfu_ctx->irqs == NULL) {
        /*
         * FIXME need to check that the number of MSI and MSI-X IRQs are valid
         * (1, 2, 4, 8, 16 or 32 for MSI and up to 2048 for MSI-X).
         */

        // Work out highest count of irq vectors.
        for (i = 0; i < VFU_DEV_NUM_IRQS; i++) {
            if (max_ivs < vfu_ctx->irq_count[i]) {
                max_ivs = vfu_ctx->irq_count[i];
            }
        }

        // FIXME: assert(max_ivs > 0)?
        size = sizeof(int) * max_ivs;
        vfu_ctx->irqs = calloc(1, sizeof(vfu_irqs_t) + size);
        if (vfu_ctx->irqs == NULL) {
            // vfu_ctx->pci.config_space should be free'ed by vfu_destroy_ctx().
            return -1;
        }

        // Set context irq information.
        for (i = 0; i < max_ivs; i++) {
            vfu_ctx->irqs->efds[i] = -1;
        }
        vfu_ctx->irqs->err_efd = -1;
        vfu_ctx->irqs->req_efd = -1;
        vfu_ctx->irqs->max_ivs = max_ivs;

        // Reflect on the config space whether INTX is available.
        if (vfu_ctx->irq_count[VFU_DEV_INTX_IRQ] != 0) {
            vfu_ctx->pci.config_space->hdr.intr.ipin = 1; // INTA#
        }
    }

    if (vfu_ctx->pci.nr_caps != 0) {
        vfu_ctx->pci.config_space->hdr.sts.cl = 0x1;
    }

    vfu_ctx->realized = true;

    return 0;
}

static void
free_sparse_mmap_areas(vfu_ctx_t *vfu_ctx)
{
    int i;

    assert(vfu_ctx != NULL);

    for (i = 0; i < (int)vfu_ctx->nr_regions; i++) {
        free(vfu_ctx->reg_info[i].mmap_areas);
    }
}

static void
vfu_reset_ctx_quiesced(vfu_ctx_t *vfu_ctx)
{
    if (vfu_ctx->dma != NULL) {
        dma_controller_remove_all_regions(vfu_ctx->dma, vfu_ctx->dma_unregister,
                                          vfu_ctx);
    }

    /* FIXME what happens if the device reset callback fails? */
    do_device_reset(vfu_ctx, VFU_RESET_LOST_CONN);

    if (vfu_ctx->irqs != NULL) {
        irqs_reset(vfu_ctx);
    }

    if (vfu_ctx->tran->detach != NULL) {
        vfu_ctx->tran->detach(vfu_ctx);
    }
}

static int
vfu_reset_ctx(vfu_ctx_t *vfu_ctx, int reason)
{
    vfu_log(vfu_ctx, LOG_INFO, "%s: %s", __func__,  strerror(reason));

    if (vfu_ctx->quiesce != NULL
        && vfu_ctx->pending.state == VFU_CTX_PENDING_NONE) {
        vfu_ctx->in_cb = CB_QUIESCE;
        int ret = vfu_ctx->quiesce(vfu_ctx);
        vfu_ctx->in_cb = CB_NONE;
        if (ret < 0) {
            if (errno == EBUSY) {
                vfu_ctx->pending.state = VFU_CTX_PENDING_CTX_RESET;
                return ret;
            }
            vfu_log(vfu_ctx, LOG_ERR, "failed to quiesce device: %m");
            return ret;
        }
    }
    vfu_reset_ctx_quiesced(vfu_ctx);
    return 0;
}

EXPORT void
vfu_destroy_ctx(vfu_ctx_t *vfu_ctx)
{
    if (vfu_ctx == NULL) {
        return;
    }

    vfu_ctx->quiesce = NULL;
    if (vfu_reset_ctx(vfu_ctx, ESHUTDOWN) < 0) {
        vfu_log(vfu_ctx, LOG_WARNING, "failed to reset context: %m");
    }

    free(vfu_ctx->uuid);
    free(vfu_ctx->pci.config_space);

    if (vfu_ctx->tran->fini != NULL) {
        vfu_ctx->tran->fini(vfu_ctx);
    }

    if (vfu_ctx->dma != NULL) {
        dma_controller_destroy(vfu_ctx->dma);
    }
    free_sparse_mmap_areas(vfu_ctx);
    free_regions(vfu_ctx);
    free(vfu_ctx->migration);
    free(vfu_ctx->irqs);
    free(vfu_ctx);
}

EXPORT void *
vfu_get_private(vfu_ctx_t *vfu_ctx)
{
    assert(vfu_ctx != NULL);

    return vfu_ctx->pvt;
}

EXPORT vfu_ctx_t *
vfu_create_ctx(vfu_trans_t trans, const char *path, int flags, void *pvt,
               vfu_dev_type_t dev_type)
{
    vfu_ctx_t *vfu_ctx = NULL;
    int err = 0;
    size_t i;

    if ((flags & ~(LIBVFIO_USER_FLAG_ATTACH_NB)) != 0) {
        return ERROR_PTR(EINVAL);
    }

#ifdef WITH_TRAN_PIPE
    if (trans != VFU_TRANS_SOCK && trans != VFU_TRANS_PIPE) {
        return ERROR_PTR(ENOTSUP);
    }
#else
    if (trans != VFU_TRANS_SOCK) {
        return ERROR_PTR(ENOTSUP);
    }
#endif

    if (dev_type != VFU_DEV_TYPE_PCI) {
        return ERROR_PTR(ENOTSUP);
    }

    vfu_ctx = calloc(1, sizeof(vfu_ctx_t));
    if (vfu_ctx == NULL) {
        return NULL;
    }

    vfu_ctx->dev_type = dev_type;
    if (trans == VFU_TRANS_SOCK) {
        vfu_ctx->tran = &tran_sock_ops;
    } else {
#ifdef WITH_TRAN_PIPE
        vfu_ctx->tran = &tran_pipe_ops;
#endif
    }
    vfu_ctx->tran_data = NULL;
    vfu_ctx->pvt = pvt;
    vfu_ctx->flags = flags;
    vfu_ctx->log_level = LOG_ERR;
    vfu_ctx->pci_cap_exp_off = -1;

    vfu_ctx->uuid = strdup(path);
    if (vfu_ctx->uuid == NULL) {
        goto err_out;
    }

    /*
     * FIXME: Now we always allocate for migration region. Check if its better
     * to seperate migration region from standard regions in vfu_ctx.reg_info
     * and move it into vfu_ctx.migration.
     */
    vfu_ctx->nr_regions = VFU_PCI_DEV_NUM_REGIONS;
    vfu_ctx->reg_info = calloc(vfu_ctx->nr_regions, sizeof(*vfu_ctx->reg_info));
    if (vfu_ctx->reg_info == NULL) {
        goto err_out;
    }

    for (i = 0; i < vfu_ctx->nr_regions; i++) {
        vfu_ctx->reg_info[i].fd = -1;
        LIST_INIT(&vfu_ctx->reg_info[i].subregions);
    }

    if (vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_ERR_IRQ, 1) == -1) {
        goto err_out;
    }
    if (vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_REQ_IRQ, 1) == -1) {
        goto err_out;
    }

    if (vfu_ctx->tran->init != NULL) {
        err = vfu_ctx->tran->init(vfu_ctx);
        if (err < 0) {
            goto err_out;
        }
    }

    return vfu_ctx;

err_out:
    err = errno;

    vfu_destroy_ctx(vfu_ctx);

    return ERROR_PTR(err);
}

EXPORT int
vfu_attach_ctx(vfu_ctx_t *vfu_ctx)
{

    assert(vfu_ctx != NULL);

    return vfu_ctx->tran->attach(vfu_ctx);
}

EXPORT int
vfu_get_poll_fd(vfu_ctx_t *vfu_ctx)
{

    assert(vfu_ctx != NULL);

    return vfu_ctx->tran->get_poll_fd(vfu_ctx);
}

EXPORT int
vfu_setup_log(vfu_ctx_t *vfu_ctx, vfu_log_fn_t *log, int log_level)
{

    if (log_level != LOG_ERR && log_level != LOG_INFO && log_level != LOG_DEBUG) {
        return ERROR_INT(EINVAL);
    }

    vfu_ctx->log = log;
    vfu_ctx->log_level = log_level;

    return 0;
}

static int
copyin_mmap_areas(vfu_reg_info_t *reg_info,
                  struct iovec *mmap_areas, uint32_t nr_mmap_areas)
{
    size_t size = nr_mmap_areas * sizeof(*mmap_areas);

    if (mmap_areas == NULL || nr_mmap_areas ==  0) {
        return 0;
    }

    reg_info->mmap_areas = malloc(size);

    if (reg_info->mmap_areas == NULL) {
        return -1;
    }

    memcpy(reg_info->mmap_areas, mmap_areas, size);
    reg_info->nr_mmap_areas = nr_mmap_areas;

    return 0;
}

static bool
ranges_intersect(size_t off1, size_t size1, size_t off2, size_t size2)
{
    /*
     * For two ranges to intersect, the start of each range must be before the
     * end of the other range.
     * TODO already defined in lib/pci_caps.c, maybe introduce a file for misc
     * utility functions?
     */
    return (off1 < (off2 + size2) && off2 < (off1 + size1));
}

static bool
maps_over_migr_regs(struct iovec *iov)
{
    return ranges_intersect(0, vfu_get_migr_register_area_size(),
                            (size_t)iov->iov_base, iov->iov_len);
}

static bool
validate_sparse_mmaps_for_migr_reg(vfu_reg_info_t *reg)
{
    int i;

    for (i = 0; i < reg->nr_mmap_areas; i++) {
        if (maps_over_migr_regs(&reg->mmap_areas[i])) {
            return false;
        }
    }
    return true;
}

EXPORT int
vfu_setup_region(vfu_ctx_t *vfu_ctx, int region_idx, size_t size,
                 vfu_region_access_cb_t *cb, int flags,
                 struct iovec *mmap_areas, uint32_t nr_mmap_areas,
                 int fd, uint64_t offset)
{
    struct iovec whole_region = { .iov_base = 0, .iov_len = size };
    vfu_reg_info_t *reg;
    size_t i;
    int ret = 0;

    assert(vfu_ctx != NULL);

    if ((flags & ~(VFU_REGION_FLAG_MASK)) ||
        (!(flags & VFU_REGION_FLAG_RW))) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid region flags");
        return ERROR_INT(EINVAL);
    }

    if ((flags & VFU_REGION_FLAG_ALWAYS_CB) && (cb == NULL)) {
        vfu_log(vfu_ctx, LOG_ERR, "VFU_REGION_FLAG_ALWAYS_CB needs callback");
        return ERROR_INT(EINVAL);
    }

    if ((mmap_areas == NULL) != (nr_mmap_areas == 0) ||
        (mmap_areas != NULL && fd == -1)) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid mappable region arguments");
        return ERROR_INT(EINVAL);
    }

    if (region_idx < VFU_PCI_DEV_BAR0_REGION_IDX ||
        region_idx >= VFU_PCI_DEV_NUM_REGIONS) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid region index %d", region_idx);
        return ERROR_INT(EINVAL);
    }

    /*
     * PCI config space is never mappable or of type mem.
     */
    if (region_idx == VFU_PCI_DEV_CFG_REGION_IDX &&
        (((flags & VFU_REGION_FLAG_RW) != VFU_REGION_FLAG_RW) ||
        (flags & VFU_REGION_FLAG_MEM))) {
        return ERROR_INT(EINVAL);
    }

    if (region_idx == VFU_PCI_DEV_MIGR_REGION_IDX &&
        size < vfu_get_migr_register_area_size()) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid migration region size %zu", size);
        return ERROR_INT(EINVAL);
    }

    for (i = 0; i < nr_mmap_areas; i++) {
        struct iovec *iov = &mmap_areas[i];
        if ((size_t)iov_end(iov) > size) {
            return ERROR_INT(EINVAL);
        }
    }

    reg = &vfu_ctx->reg_info[region_idx];

    reg->flags = flags;
    reg->size = size;
    reg->cb = cb;
    reg->fd = fd;
    reg->offset = offset;

    if (mmap_areas == NULL && reg->fd != -1) {
        mmap_areas = &whole_region;
        nr_mmap_areas = 1;
    }

    if (nr_mmap_areas > 0) {
        ret = copyin_mmap_areas(reg, mmap_areas, nr_mmap_areas);
        if (ret < 0) {
            goto err;
        }
    }

    if (region_idx == VFU_PCI_DEV_MIGR_REGION_IDX) {
        if (!validate_sparse_mmaps_for_migr_reg(reg)) {
            vfu_log(vfu_ctx, LOG_ERR,
                    "migration registers cannot be memory mapped");
            errno = EINVAL;
            goto err;
        }
    }

    return 0;

err:
    ret = errno;
    free(reg->mmap_areas);
    memset(reg, 0, sizeof(*reg));
    return ERROR_INT(ret);
}

EXPORT int
vfu_setup_device_reset_cb(vfu_ctx_t *vfu_ctx, vfu_reset_cb_t *reset)
{
    assert(vfu_ctx != NULL);
    vfu_ctx->reset = reset;
    return 0;
}

EXPORT void
vfu_setup_device_quiesce_cb(vfu_ctx_t *vfu_ctx, vfu_device_quiesce_cb_t *quiesce)
{
    assert(vfu_ctx != NULL);
    vfu_ctx->quiesce = quiesce;
}

EXPORT int
vfu_setup_device_dma(vfu_ctx_t *vfu_ctx, vfu_dma_register_cb_t *dma_register,
                     vfu_dma_unregister_cb_t *dma_unregister)
{

    assert(vfu_ctx != NULL);

    // Create the internal DMA controller.
    vfu_ctx->dma = dma_controller_create(vfu_ctx, MAX_DMA_REGIONS,
                                         MAX_DMA_SIZE);
    if (vfu_ctx->dma == NULL) {
        return ERROR_INT(errno);
    }

    vfu_ctx->dma_register = dma_register;
    vfu_ctx->dma_unregister = dma_unregister;

    return 0;
}

EXPORT int
vfu_setup_device_nr_irqs(vfu_ctx_t *vfu_ctx, enum vfu_dev_irq_type type,
                         uint32_t count)
{

    assert(vfu_ctx != NULL);

    if (type >= VFU_DEV_NUM_IRQS) {
        vfu_log(vfu_ctx, LOG_ERR, "Invalid IRQ type index %u", type);
        return ERROR_INT(EINVAL);
    }

    vfu_ctx->irq_count[type] = count;

    return 0;
}

EXPORT int
vfu_setup_device_migration_callbacks(vfu_ctx_t *vfu_ctx,
                                     const vfu_migration_callbacks_t *callbacks,
                                     uint64_t data_offset)
{
    int ret = 0;

    assert(vfu_ctx != NULL);
    assert(callbacks != NULL);

    if (vfu_ctx->reg_info[VFU_PCI_DEV_MIGR_REGION_IDX].size == 0) {
        vfu_log(vfu_ctx, LOG_ERR, "no device migration region");
        return ERROR_INT(EINVAL);
    }

    if (callbacks->version != VFU_MIGR_CALLBACKS_VERS) {
        vfu_log(vfu_ctx, LOG_ERR, "unsupported migration callbacks version %d",
                callbacks->version);
        return ERROR_INT(EINVAL);
    }

    vfu_ctx->migration = init_migration(callbacks, data_offset, &ret);
    if (vfu_ctx->migration == NULL) {
        vfu_log(vfu_ctx, LOG_ERR, "failed to initialize device migration");
        return ERROR_INT(ret);
    }

    return 0;
}

static void
quiesce_check_allowed(vfu_ctx_t *vfu_ctx)
{
    if (!(vfu_ctx->in_cb != CB_NONE || vfu_ctx->quiesce == NULL || !vfu_ctx->quiesced)) {
        vfu_log(vfu_ctx, LOG_ERR, "illegal function in quiesced state");
#ifdef DEBUG
        abort();
#endif
    }
}

EXPORT int
vfu_addr_to_sg(vfu_ctx_t *vfu_ctx, vfu_dma_addr_t dma_addr,
               size_t len, dma_sg_t *sg, int max_sg, int prot)
{
    assert(vfu_ctx != NULL);

    if (unlikely(vfu_ctx->dma == NULL)) {
        return ERROR_INT(EINVAL);
    }

    quiesce_check_allowed(vfu_ctx);

    return dma_addr_to_sg(vfu_ctx->dma, dma_addr, len, sg, max_sg, prot);
}

EXPORT int
vfu_map_sg(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, struct iovec *iov, int cnt,
           int flags)
{
    int ret;

    if (unlikely(vfu_ctx->dma_unregister == NULL) || flags != 0) {
        return ERROR_INT(EINVAL);
    }

    quiesce_check_allowed(vfu_ctx);

    ret = dma_map_sg(vfu_ctx->dma, sg, iov, cnt);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

EXPORT void
vfu_unmap_sg(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, struct iovec *iov, int cnt)
{
    if (unlikely(vfu_ctx->dma_unregister == NULL)) {
        return;
    }

    quiesce_check_allowed(vfu_ctx);

    return dma_unmap_sg(vfu_ctx->dma, sg, iov, cnt);
}

static int
vfu_dma_transfer(vfu_ctx_t *vfu_ctx, enum vfio_user_command cmd,
                 dma_sg_t *sg, void *data)
{
    struct vfio_user_dma_region_access *dma_reply;
    struct vfio_user_dma_region_access *dma_req;
    struct vfio_user_dma_region_access dma;
    static int msg_id = 1;
    size_t remaining;
    size_t count;
    size_t rlen;
    void *rbuf;

    assert(cmd == VFIO_USER_DMA_READ || cmd == VFIO_USER_DMA_WRITE);
    assert(vfu_ctx != NULL);
    assert(sg != NULL);

    if (cmd == VFIO_USER_DMA_WRITE && !sg->writeable) {
        return ERROR_INT(EPERM);
    }

    rlen = sizeof(struct vfio_user_dma_region_access) +
           MIN(sg->length, vfu_ctx->client_max_data_xfer_size);

    rbuf = calloc(1, rlen);

    if (rbuf == NULL) {
        return -1;
    }

    remaining = sg->length;
    count = 0;

    if (cmd == VFIO_USER_DMA_READ) {
        dma_req = &dma;
        dma_reply = rbuf;
    } else {
        dma_req = rbuf;
        dma_reply = &dma;
    }

    while (remaining > 0) {
        int ret;

        dma_req->addr = (uint64_t)sg->dma_addr + count;
        dma_req->count = MIN(remaining, vfu_ctx->client_max_data_xfer_size);

        if (cmd == VFIO_USER_DMA_WRITE) {
            memcpy(rbuf + sizeof(*dma_req), data + count, dma_req->count);

            ret = vfu_ctx->tran->send_msg(vfu_ctx, msg_id++, VFIO_USER_DMA_WRITE,
                                          rbuf, rlen, NULL,
                                          dma_reply, sizeof(*dma_reply));
        } else {
            ret = vfu_ctx->tran->send_msg(vfu_ctx, msg_id++, VFIO_USER_DMA_READ,
                                          dma_req, sizeof(*dma_req), NULL,
                                          rbuf, rlen);
        }

        if (ret < 0) {
            ret = errno;
            if (ret == ENOMSG || ret == ECONNRESET) {
                if (vfu_reset_ctx(vfu_ctx, ret) < 0) {
                    vfu_log(vfu_ctx, LOG_WARNING, "failed to reset context: %m");
                }
                ret = ENOTCONN;
            }
            free(rbuf);
            return ERROR_INT(ret);
        }

        if (dma_reply->addr != dma_req->addr ||
            dma_reply->count != dma_req->count) {
            vfu_log(vfu_ctx, LOG_ERR, "bad reply to DMA transfer: "
                    "request:%#lx,%lu reply:%#lx,%lu",
                    dma_req->addr, dma_req->count,
                    dma_reply->addr, dma_reply->count);
            free(rbuf);
            return ERROR_INT(EINVAL);
        }

        if (cmd == VFIO_USER_DMA_READ) {
            memcpy(data + count, rbuf + sizeof(*dma_reply), dma_req->count);
        }

        count += dma_req->count;
        remaining -= dma_req->count;
    }

    free(rbuf);
    return 0;
}

EXPORT int
vfu_dma_read(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, void *data)
{
    assert(vfu_ctx->pending.state == VFU_CTX_PENDING_NONE);
    return vfu_dma_transfer(vfu_ctx, VFIO_USER_DMA_READ, sg, data);
}

EXPORT int
vfu_dma_write(vfu_ctx_t *vfu_ctx, dma_sg_t *sg, void *data)
{
    assert(vfu_ctx->pending.state == VFU_CTX_PENDING_NONE);
    return vfu_dma_transfer(vfu_ctx, VFIO_USER_DMA_WRITE, sg, data);
}

EXPORT bool
vfu_sg_is_mappable(vfu_ctx_t *vfu_ctx, dma_sg_t *sg)
{
    return dma_sg_is_mappable(vfu_ctx->dma, sg);
}

EXPORT int
vfu_device_quiesced(vfu_ctx_t *vfu_ctx, int quiesce_errno)
{
    int ret;

    assert(vfu_ctx != NULL);

    if (vfu_ctx->quiesce == NULL
        || vfu_ctx->pending.state == VFU_CTX_PENDING_NONE) {
        vfu_log(vfu_ctx, LOG_DEBUG,
                "invalid call to quiesce callback, state=%d",
                vfu_ctx->pending.state);
        return ERROR_INT(EINVAL);
    }

    vfu_log(vfu_ctx, LOG_DEBUG, "device quiesced with error=%d", quiesce_errno);
    vfu_ctx->quiesced = true;

    if (quiesce_errno == 0) {
        switch (vfu_ctx->pending.state) {
        case VFU_CTX_PENDING_MSG:
            ret = handle_request(vfu_ctx, vfu_ctx->pending.msg);
            free_msg(vfu_ctx, vfu_ctx->pending.msg);
            break;
        case VFU_CTX_PENDING_CTX_RESET:
            vfu_reset_ctx_quiesced(vfu_ctx);
            ret = 0;
            break;
        default:
            assert(false);
        }
    } else {
        ret = 0;
        free_msg(vfu_ctx, vfu_ctx->pending.msg);
    }

    vfu_ctx->pending.msg = NULL;
    vfu_ctx->pending.state = VFU_CTX_PENDING_NONE;

    vfu_log(vfu_ctx, LOG_DEBUG, "device unquiesced");
    vfu_ctx->quiesced = false;

    return ret;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
