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

#ifndef LIB_VFIO_USER_PCI_H
#define LIB_VFIO_USER_PCI_H

#include "libvfio-user.h"
#include "private.h"

ssize_t
pci_nonstd_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                  loff_t offset, bool is_write);

ssize_t
pci_config_space_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                        loff_t pos, bool is_write);


static inline size_t
pci_config_space_size(vfu_ctx_t *vfu_ctx)
{
    return vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX].size;
}

static inline uint8_t *
pci_config_space_ptr(vfu_ctx_t *vfu_ctx, loff_t offset)
{
    assert((size_t)offset < pci_config_space_size(vfu_ctx));
    return (uint8_t *)vfu_ctx->pci.config_space + offset;
}

#endif /* LIB_VFIO_USER_PCI_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
