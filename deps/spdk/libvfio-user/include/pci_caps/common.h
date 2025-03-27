/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
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

#ifndef LIB_VFIO_USER_PCI_CAPS_COMMON_H
#define LIB_VFIO_USER_PCI_CAPS_COMMON_H

#include <linux/pci_regs.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cap_hdr {
    uint8_t id;
    uint8_t next;
} __attribute__((packed));
_Static_assert(sizeof(struct cap_hdr) == 0x2, "bad PCI capability header size");
_Static_assert(offsetof(struct cap_hdr, id) == PCI_CAP_LIST_ID, "bad offset");
_Static_assert(offsetof(struct cap_hdr, next) == PCI_CAP_LIST_NEXT, "bad offset");

/*
 * Vendor-specific capability
 */
struct vsc {
    struct cap_hdr  hdr;
    uint8_t         size;
    uint8_t         data[];
} __attribute__ ((packed));

/*
 * PCI Express extended capability header.
 */
struct pcie_ext_cap_hdr {
    uint32_t id:16;
    uint32_t version:4;
    uint32_t next:12;
} __attribute__((packed));

/* PCI Express vendor-specific capability header (PCIE 7.19) */
struct pcie_ext_cap_vsc_hdr {
    struct pcie_ext_cap_hdr hdr;
    uint32_t id:16;
    uint32_t rev:4;
    uint32_t len:12;
    uint8_t      data[];
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif /* LIB_VFIO_USER_PCI_CAPS_COMMON_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
