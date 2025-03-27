/*
 * Copyright (c) 2021 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *          Swapnil Ingle <swapnil.ingle@nutanix.com>
 *          Felipe Franciosi <felipe@nutanix.com>
 *          John Levon <john.levon@nutanix.com>
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

/*
 * Capability handling. We handle reads and writes to standard capabilities
 * ourselves, and optionally for vendor capabilities too. For each access (via
 * pci_config_space_access() -> pci_cap_access()), if we find that we're
 * reading from a particular capability offset:
 *
 * - if VFU_CAP_FLAG_CALLBACK is set, we call the config space region callback
 *   given by the user
 * - else we memcpy() the capability data back out to the client
 *
 * For writes:
 *
 * - if VFU_CAP_FLAG_READONLY is set, we fail the write
 * - if VFU_CAP_FLAG_CALLBACK is set, we call the config space region callback
 *   given by the user
 * - else we call the cap-specific callback to handle the write.
 *
 * Extended capabilities live in extended space (after the first 256 bytes), so
 * can never clash with a standard capability. An empty capability list is
 * signalled by a zeroed header at offset 256 (which the config space has by
 * default).
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "common.h"
#include "libvfio-user.h"
#include "pci_caps.h"
#include "pci.h"
#include "private.h"

/* All capabilities must be dword-aligned. */
#define CAP_ROUND (4)

static void *
cap_data(vfu_ctx_t *vfu_ctx, struct pci_cap *cap)
{
    return (void *)pci_config_space_ptr(vfu_ctx, cap->off);
}

static size_t
cap_size(vfu_ctx_t *vfu_ctx, void *data, bool extended)
{
    if (extended) {
        uint16_t id = ((struct pcie_ext_cap_hdr *)data)->id;

        switch (id) {
        case PCI_EXT_CAP_ID_DSN:
            return PCI_EXT_CAP_DSN_SIZEOF;
        case PCI_EXT_CAP_ID_VNDR:
            return ((struct pcie_ext_cap_vsc_hdr *)data)->len;
        default:
            vfu_log(vfu_ctx, LOG_ERR, "invalid cap id %u", id);
            abort();
        }
    } else {
        uint8_t id = ((struct cap_hdr *)data)->id;

        switch (id) {
        case PCI_CAP_ID_PM:
            return PCI_PM_SIZEOF;
        case PCI_CAP_ID_EXP:
            return VFIO_USER_PCI_CAP_EXP_SIZEOF;
        case PCI_CAP_ID_MSIX:
            return PCI_CAP_MSIX_SIZEOF;
        case PCI_CAP_ID_VNDR:
            return ((struct vsc *)data)->size;
        default:
            vfu_log(vfu_ctx, LOG_ERR, "invalid cap id %u", id);
            abort();
        }
    }
}

static ssize_t
handle_pmcs_write(vfu_ctx_t *vfu_ctx, struct pmcap *pm,
                  const struct pmcs *const pmcs)
{
    if (pm->pmcs.ps != pmcs->ps) {
        vfu_log(vfu_ctx, LOG_DEBUG, "power state set to %#x", pmcs->ps);
    }
    if (pm->pmcs.pmee != pmcs->pmee) {
        vfu_log(vfu_ctx, LOG_DEBUG, "PME enable set to %#x", pmcs->pmee);
    }
    if (pm->pmcs.dse != pmcs->dse) {
        vfu_log(vfu_ctx, LOG_DEBUG, "data select set to %#x", pmcs->dse);
    }
    if (pm->pmcs.pmes != pmcs->pmes) {
        vfu_log(vfu_ctx, LOG_DEBUG, "PME status set to %#x", pmcs->pmes);
    }
    pm->pmcs = *pmcs;
    return 0;
}

static ssize_t
cap_write_pm(vfu_ctx_t *vfu_ctx, struct pci_cap *cap, char * buf,
             size_t count, loff_t offset)
{
    struct pmcap *pm = cap_data(vfu_ctx, cap);

    switch (offset - cap->off) {
    case offsetof(struct pmcap, pc):
        if (count != sizeof(struct pc)) {
            return ERROR_INT(EINVAL);
        }
        vfu_log(vfu_ctx, LOG_ERR, "FIXME: write to pmcap::pc unimplemented");
        return ERROR_INT(ENOTSUP);
    case offsetof(struct pmcap, pmcs):
        if (count != sizeof(struct pmcs)) {
            return ERROR_INT(EINVAL);
        }
        handle_pmcs_write(vfu_ctx, pm, (struct pmcs *)buf);
        return sizeof(struct pmcs);
    case offsetof(struct pmcap, pmcsr_bse):
        if (count != 1) {
            return ERROR_INT(EINVAL);
        }
        vfu_log(vfu_ctx, LOG_ERR,
                "FIXME: write to pmcap::pmcsr_bse unimplemented");
        return ERROR_INT(ENOTSUP);
    case offsetof(struct pmcap, data):
        if (count != 1) {
            return ERROR_INT(EINVAL);
        }
        vfu_log(vfu_ctx, LOG_ERR, "FIXME: write to pmcap::data unimplemented");
        return ERROR_INT(ENOTSUP);
    }
    return ERROR_INT(EINVAL);
}

static ssize_t
cap_write_msix(vfu_ctx_t *vfu_ctx, struct pci_cap *cap, char *buf,
               size_t count, loff_t offset)
{
    struct msixcap *msix = cap_data(vfu_ctx, cap);
    struct msixcap new_msix = *msix;

    memcpy((char *)&new_msix + offset - cap->off, buf, count);

    /*
     * Same as doing &= (PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE), but
     * prefer to log what's changing.
     */

    if (msix->mxc.fm != new_msix.mxc.fm) {
        if (new_msix.mxc.fm) {
            vfu_log(vfu_ctx, LOG_DEBUG, "all MSI-X vectors masked");
        } else {
            vfu_log(vfu_ctx, LOG_DEBUG,
                   "vector's mask bit determines whether vector is masked");
        }
        msix->mxc.fm = new_msix.mxc.fm;
    }

    if (msix->mxc.mxe != new_msix.mxc.mxe) {
        vfu_log(vfu_ctx, LOG_DEBUG, "%s MSI-X",
                msix->mxc.mxe ? "enable" : "disable");
        msix->mxc.mxe = new_msix.mxc.mxe;
    }

    return count;
}

static int
handle_px_pxdc_write(vfu_ctx_t *vfu_ctx, struct pxcap *px,
                     const union pxdc *const p)
{
    assert(px != NULL);
    assert(p != NULL);

    if (p->cere != px->pxdc.cere) {
        px->pxdc.cere = p->cere;
        vfu_log(vfu_ctx, LOG_DEBUG, "CERE %s", p->cere ? "enable" : "disable");
    }

    if (p->nfere != px->pxdc.nfere) {
        px->pxdc.nfere = p->nfere;
        vfu_log(vfu_ctx, LOG_DEBUG, "NFERE %s",
                p->nfere ? "enable" : "disable");
    }

    if (p->fere != px->pxdc.fere) {
        px->pxdc.fere = p->fere;
        vfu_log(vfu_ctx, LOG_DEBUG, "FERE %s", p->fere ? "enable" : "disable");
    }

    if (p->urre != px->pxdc.urre) {
        px->pxdc.urre = p->urre;
        vfu_log(vfu_ctx, LOG_DEBUG, "URRE %s", p->urre ? "enable" : "disable");
    }

    if (p->ero != px->pxdc.ero) {
        px->pxdc.ero = p->ero;
        vfu_log(vfu_ctx, LOG_DEBUG, "ERO %s", p->ero ? "enable" : "disable");
    }

    if (p->mps != px->pxdc.mps) {
        px->pxdc.mps = p->mps;
        vfu_log(vfu_ctx, LOG_DEBUG, "MPS set to %d", p->mps);
    }

    if (p->ete != px->pxdc.ete) {
        px->pxdc.ete = p->ete;
        vfu_log(vfu_ctx, LOG_DEBUG, "ETE %s", p->ete ? "enable" : "disable");
    }

    if (p->pfe != px->pxdc.pfe) {
        px->pxdc.pfe = p->pfe;
        vfu_log(vfu_ctx, LOG_DEBUG, "PFE %s", p->pfe ? "enable" : "disable");
    }

    if (p->appme != px->pxdc.appme) {
        px->pxdc.appme = p->appme;
        vfu_log(vfu_ctx, LOG_DEBUG, "APPME %s",
                p->appme ? "enable" : "disable");
    }

    if (p->ens != px->pxdc.ens) {
        px->pxdc.ens = p->ens;
        vfu_log(vfu_ctx, LOG_DEBUG, "ENS %s", p->ens ? "enable" : "disable");
    }

    if (p->mrrs != px->pxdc.mrrs) {
        px->pxdc.mrrs = p->mrrs;
        vfu_log(vfu_ctx, LOG_DEBUG, "MRRS set to %d", p->mrrs);
    }

    if (p->iflr) {
        if (px->pxdcap.flrc == 0) {
            vfu_log(vfu_ctx, LOG_ERR, "FLR capability is not supported");
            return ERROR_INT(EINVAL);
        }
        if (vfu_ctx->reset != NULL) {
            vfu_log(vfu_ctx, LOG_DEBUG, "initiate function level reset");
            return vfu_ctx->reset(vfu_ctx, VFU_RESET_PCI_FLR);
        } else {
            vfu_log(vfu_ctx, LOG_ERR, "FLR callback is not implemented");
        }
    }

    return 0;
}

/* TODO implement */
static int
handle_px_pxlc_write(vfu_ctx_t *vfu_ctx UNUSED, struct pxcap *px UNUSED,
                     const union pxlc *const p UNUSED)
{
    return 0;
}

/* TODO implement */
static int
handle_px_pxsc_write(vfu_ctx_t *vfu_ctx UNUSED, struct pxcap *px UNUSED,
                     const struct pxsc *const p UNUSED)
{
    return 0;
}

/* TODO implement */
static int
handle_px_pxrc_write(vfu_ctx_t *vfu_ctx UNUSED, struct pxcap *px UNUSED,
                     const struct pxrc *const p UNUSED)
{
    return 0;
}

static int
handle_px_pxdc2_write(vfu_ctx_t *vfu_ctx, struct pxcap *px,
                      const union pxdc2 *const p)
{
    assert(px != NULL);
    assert(p != NULL);

    if (p->raw != px->pxdc2.raw) {
        vfu_log(vfu_ctx, LOG_DEBUG, "Device Control 2 set to %#x", p->raw);
    }
    px->pxdc2 = *p;
    return 0;
}

static int
handle_px_pxlc2_write(vfu_ctx_t *vfu_ctx, struct pxcap *px,
                      const struct pxlc2 *const p)
{
    assert(px != NULL);
    assert(p != NULL);

    if (p->stuff != px->pxlc2.stuff) {
        vfu_log(vfu_ctx, LOG_DEBUG, "Link Control 2 set to %#x", p->stuff);
    }
    px->pxlc2 = *p;
    return 0;
}

static int
handle_px_write_2_bytes(vfu_ctx_t *vfu_ctx, struct pxcap *px, char *buf,
                        loff_t off)
{
    switch (off) {
    case offsetof(struct pxcap, pxdc):
        return handle_px_pxdc_write(vfu_ctx, px, (union pxdc *)buf);
    case offsetof(struct pxcap, pxlc):
        return handle_px_pxlc_write(vfu_ctx, px, (union pxlc *)buf);
    case offsetof(struct pxcap, pxsc):
        return handle_px_pxsc_write(vfu_ctx, px, (struct pxsc *)buf);
    case offsetof(struct pxcap, pxrc):
        return handle_px_pxrc_write(vfu_ctx, px, (struct pxrc *)buf);
    case offsetof(struct pxcap, pxdc2):
        return handle_px_pxdc2_write(vfu_ctx, px, (union pxdc2 *)buf);
    case offsetof(struct pxcap, pxlc2):
        return handle_px_pxlc2_write(vfu_ctx, px, (struct pxlc2 *)buf);
    case offsetof(struct pxcap, pxsc2): /* RsvdZ */
        return 0;
    }
    return ERROR_INT(EINVAL);
}

static ssize_t
cap_write_px(vfu_ctx_t *vfu_ctx, struct pci_cap *cap, char *buf,
             size_t count, loff_t offset)
{
    struct pxcap *px = cap_data(vfu_ctx, cap);
    int err;

    switch (count) {
    case 2:
        err = handle_px_write_2_bytes(vfu_ctx, px, buf, offset - cap->off);
        break;
    default:
        err = ERROR_INT(EINVAL);
        break;
    }
    if (err != 0) {
        return err;
    }
    return count;
}

static ssize_t
cap_write_vendor(vfu_ctx_t *vfu_ctx, struct pci_cap *cap UNUSED, char *buf,
                 size_t count, loff_t offset)
{
    memcpy(pci_config_space_ptr(vfu_ctx, offset), buf, count);
    return count;
}

static ssize_t
ext_cap_write_dsn(vfu_ctx_t *vfu_ctx, struct pci_cap *cap, char *buf UNUSED,
                  size_t count UNUSED, loff_t offset UNUSED)
{
    vfu_log(vfu_ctx, LOG_ERR, "%s capability is read-only", cap->name);
    return ERROR_INT(EPERM);
}

static ssize_t
ext_cap_write_vendor(vfu_ctx_t *vfu_ctx, struct pci_cap *cap UNUSED, char *buf,
                     size_t count, loff_t offset)
{
    memcpy(pci_config_space_ptr(vfu_ctx, offset), buf, count);
    return count;
}

static bool
ranges_intersect(size_t off1, size_t size1, size_t off2, size_t size2)
{
    return (off1 < (off2 + size2) && (off1 + size1) >= off2);
}

struct pci_cap *
cap_find_by_offset(vfu_ctx_t *vfu_ctx, loff_t offset, size_t count)
{
    size_t i;

    for (i = 0; i < vfu_ctx->pci.nr_caps; i++) {
        struct pci_cap *cap = &vfu_ctx->pci.caps[i];
        if (ranges_intersect(offset, count, cap->off, cap->size)) {
            return cap;
        }
    }

    for (i = 0; i < vfu_ctx->pci.nr_ext_caps; i++) {
        struct pci_cap *cap = &vfu_ctx->pci.ext_caps[i];
        if (ranges_intersect(offset, count, cap->off, cap->size)) {
            return cap;
        }
    }
    return NULL;
}

ssize_t
pci_cap_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count, loff_t offset,
               bool is_write)
{
    struct pci_cap *cap = cap_find_by_offset(vfu_ctx, offset, count);

    assert(cap != NULL);
    assert((size_t)offset >= cap->off);
    assert(count <= cap->size);

    if (is_write && (cap->flags & VFU_CAP_FLAG_READONLY)) {
        vfu_log(vfu_ctx, LOG_ERR, "write of %zu bytes to read-only capability "
                "%u (%s)", count, cap->id, cap->name);
        return ERROR_INT(EPERM);
    }

    if (cap->flags & VFU_CAP_FLAG_CALLBACK) {
        return pci_nonstd_access(vfu_ctx, buf, count, offset, is_write);
    }

    if (!is_write) {
        memcpy(buf, pci_config_space_ptr(vfu_ctx, offset), count);
        return count;
    }

    if (offset - cap->off < cap->hdr_size) {
        vfu_log(vfu_ctx, LOG_ERR,
                "disallowed write to header for cap %d (%s)",
                cap->id, cap->name);
        return ERROR_INT(EPERM);
    }

    return cap->cb(vfu_ctx, cap, buf, count, offset);
}

/*
 * Place the new capability after the previous (or after the standard header if
 * this is the first capability).
 *
 * If cap->off is already provided, place it directly, but first check it
 * doesn't overlap an existing capability, or the PCI header. We still also need
 * to link it into the list. There's no guarantee that the list is ordered by
 * offset after doing so.
 */
static int
cap_place(vfu_ctx_t *vfu_ctx, struct pci_cap *cap, void *data)
{
    vfu_pci_config_space_t *config_space;
    uint8_t *prevp = NULL;
    size_t offset;

    config_space = vfu_pci_get_config_space(vfu_ctx);

    prevp = &config_space->hdr.cap;

    if (cap->off != 0) {
        if (cap->off < PCI_STD_HEADER_SIZEOF) {
            vfu_log(vfu_ctx, LOG_ERR, "invalid offset %#lx for capability "
                    "%u (%s)", cap->off, cap->id, cap->name);
            return ERROR_INT(EINVAL);
        }

        if (cap_find_by_offset(vfu_ctx, cap->off, cap->size) != NULL) {
            vfu_log(vfu_ctx, LOG_ERR, "overlap found for capability "
                    "%u (%s)", cap->id, cap->name);
            return ERROR_INT(EINVAL);
        }

        while (*prevp != 0) {
            prevp = pci_config_space_ptr(vfu_ctx, *prevp + PCI_CAP_LIST_NEXT);
        }
    } else if (*prevp == 0) {
        cap->off = PCI_STD_HEADER_SIZEOF;
    } else {
        for (offset = *prevp; offset != 0; offset = *prevp) {
            size_t size;

            prevp = pci_config_space_ptr(vfu_ctx, offset + PCI_CAP_LIST_NEXT);

            if (*prevp == 0) {
                size = cap_size(vfu_ctx, pci_config_space_ptr(vfu_ctx, offset),
                                false);
                cap->off = ROUND_UP(offset + size, 4);
                break;
            }
        }
    }

    if (cap->off + cap->size > pci_config_space_size(vfu_ctx)) {
        vfu_log(vfu_ctx, LOG_ERR, "no config space left for capability "
                "%u (%s) of size %zu bytes at offset %#lx", cap->id,
                cap->name, cap->size, cap->off);
        return ERROR_INT(ENOSPC);
    }

    memcpy(cap_data(vfu_ctx, cap), data, cap->size);
    /* Make sure the previous cap's PCI_CAP_LIST_NEXT points to us. */
    *prevp = cap->off;
    /* Make sure our PCI_CAP_LIST_NEXT is zeroed. */
    *pci_config_space_ptr(vfu_ctx, cap->off + PCI_CAP_LIST_NEXT) = 0;
    return 0;
}

/*
 * Place the new extended capability after the previous (or at the beginning of
 * extended config space, replacing the initial zeroed capability).
 *
 * If cap->off is already provided, place it directly, but first check it
 * doesn't overlap an existing extended capability, and that the first one
 * replaces the initial zeroed capability. We also still need to link it into
 * the list.
 */
static int
ext_cap_place(vfu_ctx_t *vfu_ctx, struct pci_cap *cap, void *data)
{
    struct pcie_ext_cap_hdr *hdr = NULL;

    hdr = (void *)pci_config_space_ptr(vfu_ctx, PCI_CFG_SPACE_SIZE);

    if (cap->off != 0) {
        if (cap->off < PCI_CFG_SPACE_SIZE) {
            vfu_log(vfu_ctx, LOG_ERR, "invalid offset %#lx for capability "
                    "%u (%s)", cap->off, cap->id, cap->name);
            return ERROR_INT(EINVAL);
        }

        if (cap_find_by_offset(vfu_ctx, cap->off, cap->size) != NULL) {
            vfu_log(vfu_ctx, LOG_ERR, "overlap found for capability "
                    "%u (%s)", cap->id, cap->name);
            return ERROR_INT(EINVAL);
        }

        if (hdr->id == 0x0 && cap->off != PCI_CFG_SPACE_SIZE) {
            vfu_log(vfu_ctx, LOG_ERR, "first extended capability must be at "
                    "%#x", PCI_CFG_SPACE_SIZE);
            return ERROR_INT(EINVAL);
        }

        while (hdr->next != 0) {
            hdr = (void *)pci_config_space_ptr(vfu_ctx, hdr->next);
        }
    } else if (hdr->id == 0x0) {
        hdr = NULL;
        cap->off = PCI_CFG_SPACE_SIZE;
    } else {
        while (hdr->next != 0) {
            hdr = (void *)pci_config_space_ptr(vfu_ctx, hdr->next);
        }

        cap->off = ROUND_UP((uint8_t *)hdr + cap_size(vfu_ctx, hdr, true) -
                            pci_config_space_ptr(vfu_ctx, 0), CAP_ROUND);
    }

    if (cap->off + cap->size > pci_config_space_size(vfu_ctx)) {
        vfu_log(vfu_ctx, LOG_ERR, "no config space left for capability "
                "%u (%s) of size %zu bytes at offset %#lx", cap->id,
                cap->name, cap->size, cap->off);
        return ERROR_INT(ENOSPC);
    }

    memcpy(cap_data(vfu_ctx, cap), data, cap->size);

    /* Make sure the previous cap's next points to us. */
    if (hdr != NULL) {
        assert((cap->off & 0x3) == 0);
        hdr->next = cap->off;
    }

    hdr = (void *)pci_config_space_ptr(vfu_ctx, cap->off);
    hdr->next = 0;
    return 0;
}

EXPORT ssize_t
vfu_pci_add_capability(vfu_ctx_t *vfu_ctx, size_t pos, int flags, void *data)
{
    bool extended = (flags & VFU_CAP_FLAG_EXTENDED);
    struct pci_cap cap = { 0 };
    int ret;

    assert(vfu_ctx != NULL);

    if (flags & ~(VFU_CAP_FLAG_EXTENDED | VFU_CAP_FLAG_CALLBACK |
        VFU_CAP_FLAG_READONLY)) {
        vfu_log(vfu_ctx, LOG_DEBUG, "bad flags %#x", flags);
        return ERROR_INT(EINVAL);
    }

    if ((flags & VFU_CAP_FLAG_CALLBACK) &&
        vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX].cb == NULL) {
        vfu_log(vfu_ctx, LOG_DEBUG, "no callback");
        return ERROR_INT(EINVAL);
    }

    cap.off = pos;
    cap.flags = flags;
    cap.extended = extended;

    if (extended) {
        switch (vfu_ctx->pci.type) {
        case VFU_PCI_TYPE_PCI_X_2:
        case VFU_PCI_TYPE_EXPRESS:
            break;
        default:
            vfu_log(vfu_ctx, LOG_DEBUG, "bad PCI type %#x", vfu_ctx->pci.type);
            return ERROR_INT(EINVAL);
        }

        if (vfu_ctx->pci.nr_ext_caps == VFU_MAX_CAPS) {
            return ERROR_INT(ENOSPC);
        }

        cap.id = ((struct pcie_ext_cap_hdr *)data)->id;
        cap.hdr_size = sizeof(struct pcie_ext_cap_hdr);

        switch (cap.id) {
        case PCI_EXT_CAP_ID_DSN:
            cap.name = "Device Serial Number";
            cap.cb = ext_cap_write_dsn;
            break;
        case PCI_EXT_CAP_ID_VNDR:
            cap.name = "Vendor-Specific";
            cap.cb = ext_cap_write_vendor;
            cap.hdr_size = sizeof(struct pcie_ext_cap_vsc_hdr);
            break;
        default:
            vfu_log(vfu_ctx, LOG_ERR, "unsupported capability %#x", cap.id);
            return ERROR_INT(ENOTSUP);
        }

        cap.size = cap_size(vfu_ctx, data, extended);

        if (cap.off + cap.size >= pci_config_space_size(vfu_ctx)) {
            vfu_log(vfu_ctx, LOG_DEBUG, "bad PCIe capability offset");
            return ERROR_INT(EINVAL);
        }

        ret = ext_cap_place(vfu_ctx, &cap, data);

    } else {
        if (vfu_ctx->pci.nr_caps == VFU_MAX_CAPS) {
            return ERROR_INT(ENOSPC);
        }

        cap.id = ((struct cap_hdr *)data)->id;
        cap.hdr_size = sizeof(struct cap_hdr);

        switch (cap.id) {
        case PCI_CAP_ID_PM:
            cap.name = "Power Management";
            cap.cb = cap_write_pm;
            break;
        case PCI_CAP_ID_EXP:
            cap.name = "PCI Express";
            cap.cb = cap_write_px;
            break;
        case PCI_CAP_ID_MSIX:
            cap.name = "MSI-X";
            cap.cb = cap_write_msix;
            break;
        case PCI_CAP_ID_VNDR:
            cap.name = "Vendor-Specific";
            cap.cb = cap_write_vendor;
            cap.hdr_size = sizeof(struct vsc);
            break;
        default:
            vfu_log(vfu_ctx, LOG_ERR, "unsupported capability %#x", cap.id);
            return ERROR_INT(ENOTSUP);
        }

        cap.size = cap_size(vfu_ctx, data, extended);

        if (cap.off + cap.size >= pci_config_space_size(vfu_ctx)) {
                vfu_log(vfu_ctx, LOG_DEBUG,
                        "PCI capability past end of config space, %#lx >= %#lx",
                        cap.off + cap.size, pci_config_space_size(vfu_ctx));
            return ERROR_INT(EINVAL);
        }

        ret = cap_place(vfu_ctx, &cap, data);
    }

    if (ret != 0) {
        return ret;
    }

    vfu_log(vfu_ctx, LOG_DEBUG, "added PCI cap \"%s\" size=%#zx offset=%#zx",
            cap.name, cap.size, cap.off);

    if (extended) {
        memcpy(&vfu_ctx->pci.ext_caps[vfu_ctx->pci.nr_ext_caps],
               &cap, sizeof(cap));
        vfu_ctx->pci.nr_ext_caps++;
    } else {
        memcpy(&vfu_ctx->pci.caps[vfu_ctx->pci.nr_caps], &cap, sizeof(cap));
        vfu_ctx->pci.nr_caps++;
    }


    if (cap.id == PCI_CAP_ID_EXP) {
        vfu_ctx->pci_cap_exp_off = cap.off;
    }
    return cap.off;
}

static size_t
vfu_pci_find_next_ext_capability(vfu_ctx_t *vfu_ctx, size_t offset, int cap_id)
{
    struct pcie_ext_cap_hdr *hdr = NULL;

    if (offset + sizeof(*hdr) >= pci_config_space_size(vfu_ctx)) {
        errno = EINVAL;
        return 0;
    }

    if (offset == 0) {
        offset = PCI_CFG_SPACE_SIZE;
        hdr = (void *)pci_config_space_ptr(vfu_ctx, offset);
    } else {
        hdr = (void *)pci_config_space_ptr(vfu_ctx, offset);
        hdr = (void *)pci_config_space_ptr(vfu_ctx, hdr->next);
    }

    for (;;) {
        offset = (uint8_t *)hdr - pci_config_space_ptr(vfu_ctx, 0);

        if (offset + sizeof(*hdr) >= pci_config_space_size(vfu_ctx)) {
            errno = EINVAL;
            return 0;
        }

        if (hdr->id == cap_id) {
            return offset;
        }

        if (hdr->next == 0) {
            break;
        }

        hdr = (void *)pci_config_space_ptr(vfu_ctx, hdr->next);
    }

    errno = ENOENT;
    return 0;
}

EXPORT size_t
vfu_pci_find_next_capability(vfu_ctx_t *vfu_ctx, bool extended,
                             size_t offset, int cap_id)
{

    assert(vfu_ctx != NULL);

    if (extended) {
        return vfu_pci_find_next_ext_capability(vfu_ctx, offset, cap_id);
    }

    if (offset + PCI_CAP_LIST_NEXT >= pci_config_space_size(vfu_ctx)) {
        errno = EINVAL;
        return 0;
    }

    if (offset == 0) {
        offset = vfu_pci_get_config_space(vfu_ctx)->hdr.cap;
    } else {
        offset = *pci_config_space_ptr(vfu_ctx, offset + PCI_CAP_LIST_NEXT);
    }

    if (offset == 0) {
        errno = ENOENT;
        return 0;
    }

    for (;;) {
        uint8_t id, next;

        /* Sanity check. */
        if (offset + PCI_CAP_LIST_NEXT >= pci_config_space_size(vfu_ctx)) {
            errno = EINVAL;
            return 0;
        }

        id = *pci_config_space_ptr(vfu_ctx, offset + PCI_CAP_LIST_ID);
        next = *pci_config_space_ptr(vfu_ctx, offset + PCI_CAP_LIST_NEXT);

        if (id == cap_id) {
            return offset;
        }

        offset = next;

        if (offset == 0) {
            errno = ENOENT;
            return 0;
        }
    }
}

EXPORT size_t
vfu_pci_find_capability(vfu_ctx_t *vfu_ctx, bool extended, int cap_id)
{
    return vfu_pci_find_next_capability(vfu_ctx, extended, 0, cap_id);
}

bool
access_is_pci_cap_exp(const vfu_ctx_t *vfu_ctx, size_t region_index,
                      uint64_t offset)
{
    size_t _offset = vfu_ctx->pci_cap_exp_off + offsetof(struct pxcap, pxdc);
    return region_index == VFU_PCI_DEV_CFG_REGION_IDX && offset == _offset;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
