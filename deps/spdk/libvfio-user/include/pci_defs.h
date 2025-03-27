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

#ifndef LIBVFIO_USER_PCI_DEFS_H
#define LIBVFIO_USER_PCI_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <linux/pci_regs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PCI standard header definitions.
 *
 * TODO lots of the sizes of each member are defined in pci_regs.h, use those
 * instead?
 */

typedef union {
    uint32_t raw;
    struct {
        uint16_t vid;
        uint16_t sid;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_ss_t;
_Static_assert(sizeof(vfu_pci_hdr_ss_t) == 0x4, "bad SS size");

typedef union {
    uint8_t raw;
} __attribute__ ((packed)) vfu_pci_hdr_bist_t;
_Static_assert(sizeof(vfu_pci_hdr_bist_t) == 0x1, "bad BIST size");

typedef union {
    uint32_t raw;
    union {
        struct {
            unsigned int region_type:1;
            unsigned int locatable:2;
            unsigned int prefetchable:1;
            unsigned int base_address:28;
        } __attribute__ ((packed)) mem;
        struct {
            unsigned int region_type:1;
            unsigned int reserved:1;
            unsigned int base_address:30;
        } __attribute__ ((packed)) io;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_bar_t;
_Static_assert(sizeof(vfu_bar_t) == 0x4, "bad BAR size");

typedef union {
    uint8_t raw;
} __attribute__ ((packed)) vfu_pci_hdr_htype_t;
_Static_assert(sizeof(vfu_pci_hdr_htype_t) == 0x1, "bad HTYPE size");

typedef union {
    uint8_t raw[3];
    struct {
        uint8_t pi;
        uint8_t scc;
        uint8_t bcc;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_cc_t;
_Static_assert(sizeof(vfu_pci_hdr_cc_t) == 0x3, "bad CC size");

/* device status */
typedef union {
    uint16_t raw;
    struct {
        unsigned int res1:3;
        unsigned int is:1;
        unsigned int cl:1;
        unsigned int c66:1;
        unsigned int res2:1;
        unsigned int fbc:1;
        unsigned int dpd:1;
        unsigned int devt:2;
        unsigned int sta:1;
        unsigned int rta:1;
        unsigned int rma:1;
        unsigned int sse:1;
        unsigned int dpe:1;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_sts_t;
_Static_assert(sizeof(vfu_pci_hdr_sts_t) == 0x2, "bad STS size");

typedef union {
    uint16_t raw;
    struct {
        uint8_t iose:1;
        uint8_t mse:1;
        uint8_t bme:1;
        uint8_t sce:1;
        uint8_t mwie:1;
        uint8_t vga:1;
        uint8_t pee:1;
        uint8_t zero:1;
        uint8_t see:1;
        uint8_t fbe:1;
        uint8_t id:1;
        uint8_t res1:5;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_cmd_t;
_Static_assert(sizeof(vfu_pci_hdr_cmd_t) == 0x2, "bad CMD size");

typedef union {
    uint32_t raw;
    struct {
        uint16_t vid;
        uint16_t did;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_id_t;
_Static_assert(sizeof(vfu_pci_hdr_id_t) == 0x4, "bad ID size");

typedef union {
    uint16_t raw;
    struct {
        uint8_t iline;
        uint8_t ipin;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_intr_t;
_Static_assert(sizeof(vfu_pci_hdr_intr_t) == 0x2, "bad INTR size");

typedef union {
    uint8_t raw[PCI_STD_HEADER_SIZEOF];
    struct {
        vfu_pci_hdr_id_t id;
        vfu_pci_hdr_cmd_t cmd;
        vfu_pci_hdr_sts_t sts;
        uint8_t rid;
        vfu_pci_hdr_cc_t cc;
        uint8_t cls;
        uint8_t mlt;
        vfu_pci_hdr_htype_t htype;
        vfu_pci_hdr_bist_t bist;
#define PCI_BARS_NR 6
        vfu_bar_t bars[PCI_BARS_NR];
        uint32_t ccptr;
        vfu_pci_hdr_ss_t ss;
        uint32_t erom;
        uint8_t cap;
        uint8_t res1[7];
        vfu_pci_hdr_intr_t intr;
        uint8_t mgnt;
        uint8_t mlat;
    } __attribute__ ((packed));
} __attribute__ ((packed)) vfu_pci_hdr_t;
_Static_assert(sizeof(vfu_pci_hdr_t) == 0x40, "bad PCI header size");

/*
 * Note that extended config space is 4096 bytes.
 */
typedef struct {
    union {
        uint8_t raw[PCI_CFG_SPACE_SIZE];
        vfu_pci_hdr_t hdr;
    } __attribute__ ((packed));
    uint8_t extended[];
} __attribute__ ((packed)) vfu_pci_config_space_t;
_Static_assert(sizeof(vfu_pci_config_space_t) == 0x100,
               "bad PCI configuration space size");

#ifdef __cplusplus
}
#endif

#endif /* LIBVFIO_USER_PCI_DEFS_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
