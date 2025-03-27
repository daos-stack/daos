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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "pci_caps.h"
#include "common.h"
#include "libvfio-user.h"
#include "pci.h"
#include "private.h"

static inline void
pci_hdr_write_bar(vfu_ctx_t *vfu_ctx, uint16_t bar_index, const char *buf)
{
    uint32_t cfg_addr;
    unsigned long mask;
    vfu_pci_hdr_t *hdr;

    assert(vfu_ctx != NULL);

    if (vfu_ctx->reg_info[bar_index].size == 0) {
        return;
    }

    hdr = &vfu_pci_get_config_space(vfu_ctx)->hdr;

    cfg_addr = *(uint32_t *) buf;

    vfu_log(vfu_ctx, LOG_DEBUG, "BAR%d addr 0x%x", bar_index, cfg_addr);

    if (cfg_addr == 0xffffffff) {
        cfg_addr = ~(vfu_ctx->reg_info[bar_index].size) + 1;
    }

    if ((vfu_ctx->reg_info[bar_index].flags & VFU_REGION_FLAG_MEM)) {
        mask = PCI_BASE_ADDRESS_MEM_MASK;
    } else {
        mask = PCI_BASE_ADDRESS_IO_MASK;
    }
    cfg_addr |= (hdr->bars[bar_index].raw & ~mask);

    hdr->bars[bar_index].raw = htole32(cfg_addr);
}

#define BAR_INDEX(offset) ((offset - PCI_BASE_ADDRESS_0) >> 2)

static int
handle_command_write(vfu_ctx_t *ctx, vfu_pci_config_space_t *pci,
                     const char *buf)
{
    uint16_t v;

    assert(ctx != NULL);

    assert(pci != NULL);
    assert(buf != NULL);

    v = *(uint16_t*)buf;

    if ((v & PCI_COMMAND_IO) == PCI_COMMAND_IO) {
        if (!pci->hdr.cmd.iose) {
            pci->hdr.cmd.iose = 0x1;
            vfu_log(ctx, LOG_DEBUG, "I/O space enabled");
        }
        v &= ~PCI_COMMAND_IO;
    } else {
        if (pci->hdr.cmd.iose) {
            pci->hdr.cmd.iose = 0x0;
            vfu_log(ctx, LOG_DEBUG, "I/O space disabled");
        }
    }

    if ((v & PCI_COMMAND_MEMORY) == PCI_COMMAND_MEMORY) {
        if (!pci->hdr.cmd.mse) {
            pci->hdr.cmd.mse = 0x1;
            vfu_log(ctx, LOG_DEBUG, "memory space enabled");
        }
        v &= ~PCI_COMMAND_MEMORY;
    } else {
        if (pci->hdr.cmd.mse) {
            pci->hdr.cmd.mse = 0x0;
            vfu_log(ctx, LOG_DEBUG, "memory space disabled");
        }
    }

    if ((v & PCI_COMMAND_MASTER) == PCI_COMMAND_MASTER) {
        if (!pci->hdr.cmd.bme) {
            pci->hdr.cmd.bme = 0x1;
            vfu_log(ctx, LOG_DEBUG, "bus master enabled");
        }
        v &= ~PCI_COMMAND_MASTER;
    } else {
        if (pci->hdr.cmd.bme) {
            pci->hdr.cmd.bme = 0x0;
            vfu_log(ctx, LOG_DEBUG, "bus master disabled");
        }
    }

    if ((v & PCI_COMMAND_SERR) == PCI_COMMAND_SERR) {
        if (!pci->hdr.cmd.see) {
            pci->hdr.cmd.see = 0x1;
            vfu_log(ctx, LOG_DEBUG, "SERR# enabled");
        }
        v &= ~PCI_COMMAND_SERR;
    } else {
        if (pci->hdr.cmd.see) {
            pci->hdr.cmd.see = 0x0;
            vfu_log(ctx, LOG_DEBUG, "SERR# disabled");
        }
    }

    if ((v & PCI_COMMAND_INTX_DISABLE) == PCI_COMMAND_INTX_DISABLE) {
        if (!pci->hdr.cmd.id) {
            pci->hdr.cmd.id = 0x1;
            vfu_log(ctx, LOG_DEBUG, "INTx emulation disabled");
        }
        v &= ~PCI_COMMAND_INTX_DISABLE;
    } else {
        if (pci->hdr.cmd.id) {
            pci->hdr.cmd.id = 0x0;
            vfu_log(ctx, LOG_DEBUG, "INTx emulation enabled");
        }
    }

    if ((v & PCI_COMMAND_INVALIDATE) == PCI_COMMAND_INVALIDATE) {
        if (!pci->hdr.cmd.mwie) {
            pci->hdr.cmd.mwie = 1U;
            vfu_log(ctx, LOG_DEBUG, "memory write and invalidate enabled");
        }
        v &= ~PCI_COMMAND_INVALIDATE;
    } else {
        if (pci->hdr.cmd.mwie) {
            pci->hdr.cmd.mwie = 0;
            vfu_log(ctx, LOG_DEBUG, "memory write and invalidate disabled");
        }
    }

    if ((v & PCI_COMMAND_VGA_PALETTE) == PCI_COMMAND_VGA_PALETTE) {
        vfu_log(ctx, LOG_DEBUG, "enabling VGA palette snooping ignored");
        v &= ~PCI_COMMAND_VGA_PALETTE;
    }

    if (v != 0) {
        vfu_log(ctx, LOG_ERR, "unconsumed command flags %x", v);
        return ERROR_INT(EINVAL);
    }

    return 0;
}

static int
handle_erom_write(vfu_ctx_t *ctx, vfu_pci_config_space_t *pci,
                  const char *buf)
{
    uint32_t v;

    assert(ctx != NULL);
    assert(pci != NULL);

    v = *(uint32_t*)buf;

    if (v == (uint32_t)PCI_ROM_ADDRESS_MASK) {
        vfu_log(ctx, LOG_DEBUG, "write mask to EROM ignored");
    } else if (v == 0) {
        vfu_log(ctx, LOG_DEBUG, "cleared EROM");
        pci->hdr.erom = 0;
    } else if (v == (uint32_t)~PCI_ROM_ADDRESS_ENABLE) {
        vfu_log(ctx, LOG_DEBUG, "EROM disable ignored");
    } else if (v == ~0U) {
        vfu_log(ctx, LOG_INFO, "EROM not implemented");
    } else {
        vfu_log(ctx, LOG_ERR, "bad write to EROM 0x%x bytes", v);
        return ERROR_INT(EINVAL);
    }
    return 0;
}

static int
pci_hdr_write(vfu_ctx_t *vfu_ctx, const char *buf, loff_t offset)
{
    vfu_pci_config_space_t *cfg_space;
    int ret = 0;

    assert(vfu_ctx != NULL);
    assert(buf != NULL);

    cfg_space = vfu_pci_get_config_space(vfu_ctx);

    switch (offset) {
    case PCI_COMMAND:
        ret = handle_command_write(vfu_ctx, cfg_space, buf);
        break;
    case PCI_STATUS:
        /* FIXME ignoring write completely is wrong as some bits are RW1C */
        vfu_log(vfu_ctx, LOG_INFO, "write to status ignored");
        break;
    /*
     * According to the PCI spec, writing to read-only registers must be
     * ignored by the device. Some OSes tend to do this, e.g. FreeBSD.
     */
    case offsetof(vfu_pci_hdr_t, rid):
    case offsetof(vfu_pci_hdr_t, cc):
    case offsetof(vfu_pci_hdr_t, intr.ipin):
    case offsetof(vfu_pci_hdr_t, mgnt):
    case offsetof(vfu_pci_hdr_t, mlat):
        break;
    case PCI_INTERRUPT_LINE:
        cfg_space->hdr.intr.iline = buf[0];
        vfu_log(vfu_ctx, LOG_DEBUG, "ILINE=%0x", cfg_space->hdr.intr.iline);
        break;
    case PCI_CACHE_LINE_SIZE:
        cfg_space->hdr.cls = (uint8_t)buf[0];
        vfu_log(vfu_ctx, LOG_DEBUG, "cache line size set to %#hhx",
                cfg_space->hdr.cls);
        break;
    case PCI_LATENCY_TIMER:
        cfg_space->hdr.mlt = (uint8_t)buf[0];
        vfu_log(vfu_ctx, LOG_DEBUG, "set to latency timer to %hhx",
                cfg_space->hdr.mlt);
        break;
    case PCI_BASE_ADDRESS_0:
    case PCI_BASE_ADDRESS_1:
    case PCI_BASE_ADDRESS_2:
    case PCI_BASE_ADDRESS_3:
    case PCI_BASE_ADDRESS_4:
    case PCI_BASE_ADDRESS_5:
        pci_hdr_write_bar(vfu_ctx, BAR_INDEX(offset), buf);
        break;
    case PCI_ROM_ADDRESS:
        ret = handle_erom_write(vfu_ctx, cfg_space, buf);
        break;
    default:
        vfu_log(vfu_ctx, LOG_ERR, "PCI config write %#lx not handled",
                offset);
        ret = ERROR_INT(EINVAL);
    }

    return ret;
}

/*
 * Access to the standard PCI header at the given offset.
 */
static ssize_t
pci_hdr_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
               loff_t offset, bool is_write)
{
    ssize_t ret;

    assert(count <= PCI_STD_HEADER_SIZEOF);

    if (is_write) {
        ret = pci_hdr_write(vfu_ctx, buf, offset);
        if (ret < 0) {
            vfu_log(vfu_ctx, LOG_ERR, "failed to write to PCI header: %m");
        } else {
            ret = count;
        }
    } else {
        memcpy(buf, pci_config_space_ptr(vfu_ctx, offset), count);
        ret = count;
    }

    return ret;
}

/*
 * Access to the PCI config space that isn't handled by pci_hdr_access() or a
 * capability handler.
 */
ssize_t
pci_nonstd_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                  loff_t offset, bool is_write)
{
    vfu_region_access_cb_t *cb =
        vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX].cb;

    if (cb != NULL) {
        return cb(vfu_ctx, buf, count, offset, is_write);
    }

    if (is_write) {
        vfu_log(vfu_ctx, LOG_ERR, "no callback for write to config space "
                "offset %lu size %zu", offset, count);
        return ERROR_INT(EINVAL);
    }

    memcpy(buf, pci_config_space_ptr(vfu_ctx, offset), count);
    return count;
}

#define PCI_REG_SZ(reg) \
    [offsetof(vfu_pci_hdr_t, reg)] = sizeof(((vfu_pci_hdr_t *)0)->reg)

static size_t
pci_config_space_size_for_reg(loff_t offset)
{
    static const size_t off2sz[] = {
        PCI_REG_SZ(id),
        PCI_REG_SZ(cmd),
        PCI_REG_SZ(sts),
        PCI_REG_SZ(rid),
        PCI_REG_SZ(cc),
        PCI_REG_SZ(cls),
        PCI_REG_SZ(mlt),
        PCI_REG_SZ(htype),
        PCI_REG_SZ(bist),
        PCI_REG_SZ(bars[0]),
        PCI_REG_SZ(bars[1]),
        PCI_REG_SZ(bars[2]),
        PCI_REG_SZ(bars[3]),
        PCI_REG_SZ(bars[4]),
        PCI_REG_SZ(bars[5]),
        PCI_REG_SZ(ccptr),
        PCI_REG_SZ(ss),
        PCI_REG_SZ(erom),
        PCI_REG_SZ(cap),
        PCI_REG_SZ(intr.iline),
        PCI_REG_SZ(intr.ipin),
        PCI_REG_SZ(mgnt),
        PCI_REG_SZ(mlat)
    };
    assert(offset < PCI_STD_HEADER_SIZEOF);
    return off2sz[offset];
}

/*
 * Returns the size of the next segment to access, which may be less than
 * @count: we might need to split up an access that straddles capabilities and
 * normal config space, for example.
 *
 * @cb is set to the callback to use for accessing the segment.
 */
static size_t
pci_config_space_next_segment(vfu_ctx_t *ctx, size_t count, loff_t offset,
                              bool is_write, vfu_region_access_cb_t **cb)
{
    struct pci_cap *cap;

    if (offset < PCI_STD_HEADER_SIZEOF) {
        *cb = pci_hdr_access;
        if (is_write) {
            size_t reg_size = pci_config_space_size_for_reg(offset);
            if (reg_size == 0) {
                *cb = NULL;
                return 0;
            }
            count = MIN(count, reg_size);
        } else {
            count = MIN(count, (size_t)(PCI_STD_HEADER_SIZEOF - offset));
        }
        return count;
    }

    cap = cap_find_by_offset(ctx, offset, count);

    if (cap == NULL) {
        *cb = pci_nonstd_access;
        return count;
    }

    /* If we have config space before the capability. */
    if (offset < (loff_t)cap->off) {
        *cb = pci_nonstd_access;
        return cap->off - offset;
    }

    *cb = pci_cap_access;
    return MIN(count, cap->size);
}

/*
 * Special handler for config space: we handle all accesses to the standard PCI
 * header, as well as to any capabilities.
 *
 * Outside of those areas, if a callback is specified for the region, we'll use
 * that; otherwise, writes are not allowed, and reads are satisfied with
 * memcpy().
 *
 * Returns the number of bytes handled, or -1 and errno on error.
 */
ssize_t
pci_config_space_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                        loff_t offset, bool is_write)
{
    loff_t start = offset;
    ssize_t ret = 0;

    assert(vfu_ctx != NULL);

    while (count > 0) {
        vfu_region_access_cb_t *cb;
        size_t size;

        size = pci_config_space_next_segment(vfu_ctx, count, offset, is_write,
                                             &cb);
        if (cb == NULL) {
            vfu_log(vfu_ctx, LOG_ERR, "bad write to PCI config space %#lx-%#lx",
                    offset, offset + count - 1);
            return size;
        }

        ret = cb(vfu_ctx, buf, size, offset, is_write);

        // FIXME: partial reads, still return an error?
        if (ret < 0) {
            return ret;
        }

        offset += ret;
        count -= ret;
        buf += ret;
    }

    return offset - start;
}

EXPORT int
vfu_pci_init(vfu_ctx_t *vfu_ctx, vfu_pci_type_t pci_type,
             int hdr_type, int revision UNUSED)
{
    vfu_pci_config_space_t *cfg_space;
    size_t size;

    assert(vfu_ctx != NULL);

    switch (pci_type) {
    case VFU_PCI_TYPE_CONVENTIONAL:
    case VFU_PCI_TYPE_PCI_X_1:
        size = PCI_CFG_SPACE_SIZE;
        break;
    case VFU_PCI_TYPE_PCI_X_2:
    case VFU_PCI_TYPE_EXPRESS:
        size = PCI_CFG_SPACE_EXP_SIZE;
        break;
    default:
        vfu_log(vfu_ctx, LOG_ERR, "invalid PCI type %u", pci_type);
        return ERROR_INT(EINVAL);
    }

    if (hdr_type != PCI_HEADER_TYPE_NORMAL) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid PCI header type %d", hdr_type);
        return ERROR_INT(EINVAL);
    }

    /*
     * TODO there no real reason why we shouldn't allow this, we should just
     * clean up and redo it.
     */
    if (vfu_ctx->pci.config_space != NULL) {
        vfu_log(vfu_ctx, LOG_ERR,
                "PCI configuration space header already setup");
        return ERROR_INT(EEXIST);
    }

    // Allocate a buffer for the config space.
    cfg_space = calloc(1, size);
    if (cfg_space == NULL) {
        return ERROR_INT(ENOMEM);
    }

    vfu_ctx->pci.type = pci_type;
    vfu_ctx->pci.config_space = cfg_space;
    vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX].size = size;

    return 0;
}

EXPORT void
vfu_pci_set_id(vfu_ctx_t *vfu_ctx, uint16_t vid, uint16_t did,
               uint16_t ssvid, uint16_t ssid)
{
    vfu_ctx->pci.config_space->hdr.id.vid = vid;
    vfu_ctx->pci.config_space->hdr.id.did = did;
    vfu_ctx->pci.config_space->hdr.ss.vid = ssvid;
    vfu_ctx->pci.config_space->hdr.ss.sid = ssid;
}

EXPORT void
vfu_pci_set_class(vfu_ctx_t *vfu_ctx, uint8_t base, uint8_t sub, uint8_t pi)
{
    vfu_ctx->pci.config_space->hdr.cc.bcc = base;
    vfu_ctx->pci.config_space->hdr.cc.scc = sub;
    vfu_ctx->pci.config_space->hdr.cc.pi = pi;
}

EXPORT vfu_pci_config_space_t *
vfu_pci_get_config_space(vfu_ctx_t *vfu_ctx)
{
    assert(vfu_ctx != NULL);
    return vfu_ctx->pci.config_space;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
