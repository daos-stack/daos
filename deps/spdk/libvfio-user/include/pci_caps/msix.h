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

#ifndef LIB_VFIO_USER_PCI_CAPS_MSIX_H
#define LIB_VFIO_USER_PCI_CAPS_MSIX_H

#include <linux/pci_regs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message Control for MSI-X */
struct mxc {
	uint16_t ts:11;         /* RO */
	uint16_t reserved:3;    /* read must return 0, write has no effect */
	uint16_t fm:1;          /* RW */
	uint16_t mxe:1;         /* RW */
} __attribute__ ((packed));
_Static_assert(sizeof(struct mxc) == PCI_MSIX_FLAGS, "bad MXC size");

/* Table Offset / Table BIR for MSI-X */
struct mtab {
	uint32_t tbir:3;    /* RO */
	uint32_t to:29;     /* RO */
} __attribute__ ((packed));
_Static_assert(sizeof(struct mtab) == PCI_MSIX_TABLE, "bad MTAB size");

/* PBA Offset / PBA BIR for MSI-X */
struct mpba {
	uint32_t pbir:3;    /* RO */
	uint32_t pbao:29;   /* RO */
} __attribute__ ((packed));
_Static_assert(sizeof(struct mtab) == PCI_MSIX_PBA - PCI_MSIX_TABLE,
               "bad MPBA size");

struct msixcap {
    struct cap_hdr hdr;
	struct mxc mxc;
	struct mtab mtab;
	struct mpba mpba;
} __attribute__ ((packed)) __attribute__ ((aligned(4)));
_Static_assert(sizeof(struct msixcap) == PCI_CAP_MSIX_SIZEOF, "bad MSI-X size");
_Static_assert(offsetof(struct msixcap, hdr) == 0, "bad offset");

#ifdef __cplusplus
}
#endif

#endif /* VFU_CAP_MSIX_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
