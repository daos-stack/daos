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

#include <err.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libvfio-user.h"

int main(void)
{
    int i, j;
    char *buf;
    const int bytes_per_line = 0x10;
    struct vsc *vsc = alloca(sizeof(*vsc) + 0xd);
    struct pcie_ext_cap_vsc_hdr *evsc = alloca(sizeof(*evsc) + 0xd);
    struct dsncap dsn = { .hdr.id = PCI_EXT_CAP_ID_DSN,
                          .sn_lo = 0xdeadbeef,
                          .sn_hi = 0xcafebabe };
    struct pmcap pm = { .hdr.id = PCI_CAP_ID_PM, .pmcs.nsfrst = 0x1 };
    /* Required for lspci to report extended caps. */
    struct pxcap px = {
            .hdr.id = PCI_CAP_ID_EXP,
            .pxdcap = {.flrc = 0x1}
    };

    vfu_ctx_t *vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, "",
                                        LIBVFIO_USER_FLAG_ATTACH_NB, NULL,
                                        VFU_DEV_TYPE_PCI);
    if (vfu_ctx == NULL) {
        err(EXIT_FAILURE, "failed to create libvfio-user context");
    }
    if (vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_EXPRESS,
                     PCI_HEADER_TYPE_NORMAL, 0) < 0) {
        err(EXIT_FAILURE, "vfu_pci_init() failed");
    }

    if (vfu_pci_add_capability(vfu_ctx, 0, 0, &pm) < 0) {
        err(EXIT_FAILURE, "vfu_pci_add_capability() failed");
    }

    memset(vsc, 0, 0x10);
    vsc->hdr.id = PCI_CAP_ID_VNDR;
    vsc->size = 0x10;
    memcpy(vsc->data, "abcdefgh", 8);

    if (vfu_pci_add_capability(vfu_ctx, 0, 0, vsc) < 0) {
        err(EXIT_FAILURE, "vfu_pci_add_capability() failed");
    }

    if (vfu_pci_add_capability(vfu_ctx, 0, 0, &px) < 0) {
        err(EXIT_FAILURE, "vfu_pci_add_capability() failed");
    }

    if (vfu_pci_add_capability(vfu_ctx, 0, VFU_CAP_FLAG_EXTENDED, &dsn) < 0) {
        err(EXIT_FAILURE, "vfu_pci_add_capability() failed");
    }

    memset(evsc, 0, sizeof(*evsc) + 0xd);
    evsc->hdr.id = PCI_EXT_CAP_ID_VNDR;
    evsc->id = 1;
    evsc->rev = 1;
    evsc->len = sizeof(*evsc) + 0xd;

    if (vfu_pci_add_capability(vfu_ctx, 0, VFU_CAP_FLAG_EXTENDED, evsc) < 0) {
        err(EXIT_FAILURE, "vfu_pci_add_capability() failed");
    }

    evsc->id = 2;
    evsc->rev = 2;

    if (vfu_pci_add_capability(vfu_ctx, 0x400,
        VFU_CAP_FLAG_EXTENDED, evsc) < 0) {
        err(EXIT_FAILURE, "vfu_pci_add_capability() failed");
    }

    if (vfu_realize_ctx(vfu_ctx) < 0) {
        err(EXIT_FAILURE, "failed to realize device");
    }
    buf = (char*)vfu_pci_get_config_space(vfu_ctx);
    printf("00:00.0 bogus PCI device\n");
    for (i = 0; i < PCI_CFG_SPACE_EXP_SIZE / bytes_per_line; i++) {
        printf("%02x:", i * bytes_per_line);
        for (j = 0; j < bytes_per_line; j++) {
            printf(" %02x", buf[i * bytes_per_line + j] & 0xff);
        }
        printf("\n");
    }

    vfu_destroy_ctx(vfu_ctx);

    return 0;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
