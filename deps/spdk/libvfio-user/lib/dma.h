/*
 * Copyright (c) 2019 Nutanix Inc. All rights reserved.
 *
 * Authors: Mike Cui <cui@nutanix.com>
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

#ifndef LIB_VFIO_USER_DMA_H
#define LIB_VFIO_USER_DMA_H

/*
 * FIXME check whether DMA regions must be page aligned. If so then the
 * implementation can be greatly simpified.
 */

/*
 * This library emulates a DMA controller for a device emulation application to
 * perform DMA operations on a foreign memory space.
 *
 * Concepts:
 * - A DMA controller has its own 64-bit DMA address space.
 * - Foreign memory is made available to the DMA controller in linear chunks
 *   called memory regions.
 * - Each memory region is backed by a file descriptor and
 *   is registered with the DMA controllers at a unique, non-overlapping
 *   linear span of the DMA address space.
 * - To perform DMA, the application should first build a scatter-gather
 *   list (sglist) of dma_sg_t from DMA addresses. Then the sglist
 *   can be mapped using dma_map_sg() into the process's virtual address space
 *   as an iovec for direct access, and unmapped using dma_unmap_sg() when done.
 *   Every region is mapped into the application's virtual address space
 *   at registration time with R/W permissions.
 *   dma_map_sg() ignores all protection bits and only does lookups and
 *   returns pointers to the previously mapped regions. dma_unmap_sg() is
 *   effectively a no-op.
 */

#ifdef DMA_MAP_PROTECTED
#undef DMA_MAP_FAST
#define DMA_MAP_FAST_IMPL 0
#else
#define DMA_MAP_FAST_IMPL 1
#endif

#include <assert.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/queue.h>

#include "libvfio-user.h"
#include "common.h"
#include "private.h"

#define iov_end(iov) ((iov)->iov_base + (iov)->iov_len)

struct vfu_ctx;

struct dma_sg {
    vfu_dma_addr_t dma_addr;
    int region;
    uint64_t length;
    uint64_t offset;
    bool writeable;
    LIST_ENTRY(dma_sg) entry;
};

typedef struct {
    vfu_dma_info_t info;
    int fd;                     // File descriptor to mmap
    off_t offset;               // File offset
    int refcnt;                 // Number of users of this region
    char *dirty_bitmap;         // Dirty page bitmap
} dma_memory_region_t;

typedef struct dma_controller {
    int max_regions;
    size_t max_size;
    int nregions;
    struct vfu_ctx *vfu_ctx;
    size_t dirty_pgsize;        // Dirty page granularity
    LIST_HEAD(, dma_sg) maps;
    dma_memory_region_t regions[0];
} dma_controller_t;

dma_controller_t *
dma_controller_create(vfu_ctx_t *vfu_ctx, size_t max_regions, size_t max_size);

void
dma_controller_remove_all_regions(dma_controller_t *dma,
                                  vfu_dma_unregister_cb_t *dma_unregister,
                                  void *data);

void
dma_controller_destroy(dma_controller_t *dma);

/* Registers a new memory region.
 * Returns:
 * - On success, a non-negative region number
 * - On failure, -1 with errno set.
 */
MOCK_DECLARE(int, dma_controller_add_region, dma_controller_t *dma,
             vfu_dma_addr_t dma_addr, size_t size, int fd, off_t offset,
             uint32_t prot);

MOCK_DECLARE(int, dma_controller_remove_region, dma_controller_t *dma,
             vfu_dma_addr_t dma_addr, size_t size,
             vfu_dma_unregister_cb_t *dma_unregister, void *data);

MOCK_DECLARE(void, dma_controller_unmap_region, dma_controller_t *dma,
             dma_memory_region_t *region);

// Helper for dma_addr_to_sg() slow path.
int
_dma_addr_sg_split(const dma_controller_t *dma,
                   vfu_dma_addr_t dma_addr, uint64_t len,
                   dma_sg_t *sg, int max_sg, int prot);

static void
_dma_mark_dirty(const dma_controller_t *dma, const dma_memory_region_t *region,
                dma_sg_t *sg)
{
    size_t i, start, end;

    assert(dma != NULL);
    assert(region != NULL);
    assert(sg != NULL);
    assert(region->dirty_bitmap != NULL);

    start = sg->offset / dma->dirty_pgsize;
    end = start + (sg->length / dma->dirty_pgsize) + (sg->length % dma->dirty_pgsize != 0) - 1;
    for (i = start; i <= end; i++) {
        region->dirty_bitmap[i / CHAR_BIT] |= 1 << (i % CHAR_BIT);
    }
}

static inline int
dma_init_sg(const dma_controller_t *dma, dma_sg_t *sg, vfu_dma_addr_t dma_addr,
            uint64_t len, int prot, int region_index)
{
    const dma_memory_region_t *const region = &dma->regions[region_index];

    if ((prot & PROT_WRITE) && !(region->info.prot & PROT_WRITE)) {
        return ERROR_INT(EACCES);
    }

    sg->dma_addr = region->info.iova.iov_base;
    sg->region = region_index;
    sg->offset = dma_addr - region->info.iova.iov_base;
    sg->length = len;
    sg->writeable = prot & PROT_WRITE;

    return 0;
}

/* Takes a linear dma address span and returns a sg list suitable for DMA.
 * A single linear dma address span may need to be split into multiple
 * scatter gather regions due to limitations of how memory can be mapped.
 *
 * Returns:
 * - On success, number of scatter gather entries created.
 * - On failure:
 *     -1 if
 *          - the DMA address span is invalid
 *          - protection violation (errno=EACCES)
 *     (-x - 1) if @max_sg is too small, where x is the number of sg entries
 *     necessary to complete this request.
 */
static inline int
dma_addr_to_sg(const dma_controller_t *dma,
               vfu_dma_addr_t dma_addr, size_t len,
               dma_sg_t *sg, int max_sg, int prot)
{
    static __thread int region_hint;
    int cnt, ret;

    const dma_memory_region_t *const region = &dma->regions[region_hint];
    const void *region_end = iov_end(&region->info.iova);

    // Fast path: single region.
    if (likely(max_sg > 0 && len > 0 &&
               dma_addr >= region->info.iova.iov_base &&
               dma_addr + len <= region_end &&
               region_hint < dma->nregions)) {
        ret = dma_init_sg(dma, sg, dma_addr, len, prot, region_hint);
        if (ret < 0) {
            return ret;
        }

        return 1;
    }
    // Slow path: search through regions.
    cnt = _dma_addr_sg_split(dma, dma_addr, len, sg, max_sg, prot);
    if (likely(cnt > 0)) {
        region_hint = sg->region;
    }
    return cnt;
}

static inline int
dma_map_sg(dma_controller_t *dma, dma_sg_t *sg, struct iovec *iov,
           int cnt)
{
    dma_memory_region_t *region;

    assert(dma != NULL);
    assert(sg != NULL);
    assert(iov != NULL);
    assert(cnt > 0);

    do {
        if (sg->region >= dma->nregions) {
            return ERROR_INT(EINVAL);
        }
        region = &dma->regions[sg->region];

        if (region->info.vaddr == NULL) {
            return ERROR_INT(EFAULT);
        }

        if (sg->writeable) {
            if (dma->dirty_pgsize > 0) {
                _dma_mark_dirty(dma, region, sg);
            }
            LIST_INSERT_HEAD(&dma->maps, sg, entry);
        }
        vfu_log(dma->vfu_ctx, LOG_DEBUG, "map %p-%p",
                sg->dma_addr + sg->offset,
                sg->dma_addr + sg->offset + sg->length);
        iov->iov_base = region->info.vaddr + sg->offset;
        iov->iov_len = sg->length;
        region->refcnt++;

        sg++;
        iov++;
    } while (--cnt > 0);

    return 0;
}

static inline void
dma_unmap_sg(dma_controller_t *dma, const dma_sg_t *sg,
	     UNUSED struct iovec *iov, int cnt)
{

    assert(dma != NULL);
    assert(sg != NULL);
    assert(iov != NULL);
    assert(cnt > 0);

    do {
        dma_memory_region_t *r;
        /*
         * FIXME this double loop will be removed if we replace the array with
         * tfind(3)
         */
        for (r = dma->regions;
             r < dma->regions + dma->nregions &&
             r->info.iova.iov_base != sg->dma_addr;
             r++);
        if (r > dma->regions + dma->nregions) {
            /* bad region */
            continue;
        }
        if (sg->writeable) {
            LIST_REMOVE(sg, entry);
        }
        vfu_log(dma->vfu_ctx, LOG_DEBUG, "unmap %p-%p",
                sg->dma_addr + sg->offset,
                sg->dma_addr + sg->offset + sg->length);
        r->refcnt--;
        sg++;
    } while (--cnt > 0);
}

int
dma_controller_dirty_page_logging_start(dma_controller_t *dma, size_t pgsize);

void
dma_controller_dirty_page_logging_stop(dma_controller_t *dma);

int
dma_controller_dirty_page_get(dma_controller_t *dma, vfu_dma_addr_t addr,
                              uint64_t len, size_t pgsize, size_t size,
                              char *bitmap);
bool
dma_sg_is_mappable(const dma_controller_t *dma, const dma_sg_t *sg);


#endif /* LIB_VFIO_USER_DMA_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
