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

#ifndef LIB_VFIO_USER_PCI_CAPS_H
#define LIB_VFIO_USER_PCI_CAPS_H

#include "libvfio-user.h"

/*
 * This is an arbitrary value we presume is enough: as we statically allocate
 * based on this in struct vfu_ctx, we don't want it to get too big.
 */
#define VFU_MAX_CAPS (128)

struct pci_cap;

typedef ssize_t (cap_write_cb_t)(vfu_ctx_t *vfu_ctx, struct pci_cap *cap,
                                 char *buf, size_t count, loff_t offset);

struct pci_cap {
    const char *name;
    bool extended;
    uint16_t id;
    size_t off;
    size_t hdr_size;
    size_t size;
    unsigned int flags;
    cap_write_cb_t *cb;
};

/*
 * Return the first cap (if any) that intersects with the [off, off+count)
 * interval.
 */
struct pci_cap *
cap_find_by_offset(vfu_ctx_t *ctx, loff_t off, size_t count);

/*
 * Handle an access to a capability.  The access is guaranteed to be entirely
 * within a capability.
 */
ssize_t
pci_cap_access(vfu_ctx_t *ctx, char *buf, size_t count, loff_t offset,
               bool is_write);

bool
access_is_pci_cap_exp(const vfu_ctx_t *vfu_ctx, size_t region_index,
                      uint64_t offset);

#endif /* LIB_VFIO_USER_PCI_CAPS_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
