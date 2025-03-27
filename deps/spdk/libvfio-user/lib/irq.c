/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
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
#include <limits.h>
#include <stdlib.h>
#include <sys/eventfd.h>

#include "irq.h"

#define LM2VFIO_IRQT(type) (type - 1)

static const char *
vfio_irq_idx_to_str(int index)
{
    switch (index) {
    case VFIO_PCI_INTX_IRQ_INDEX: return "INTx";
    case VFIO_PCI_MSI_IRQ_INDEX: return "MSI";
    case VFIO_PCI_MSIX_IRQ_INDEX: return "MSI-X";
    case VFIO_PCI_ERR_IRQ_INDEX: return "ERR";
    case VFIO_PCI_REQ_IRQ_INDEX: return "REQ";
    default:
        abort();
    }
}

int
handle_device_get_irq_info(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_irq_info *in_info;
    struct vfio_irq_info *out_info;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    in_info = msg->in.iov.iov_base;

    if (msg->in.iov.iov_len < sizeof(*in_info) || in_info->argsz < sizeof(*out_info)) {
        return ERROR_INT(EINVAL);
    }

    if (in_info->index >= VFU_DEV_NUM_IRQS) {
        vfu_log(vfu_ctx, LOG_DEBUG, "bad irq_info index %d\n", in_info->index);
        return ERROR_INT(EINVAL);
    }

    msg->out.iov.iov_len = sizeof (*out_info);
    msg->out.iov.iov_base = calloc(1, sizeof(*out_info));

    if (msg->out.iov.iov_base == NULL) {
        return -1;
    }

    out_info = msg->out.iov.iov_base;
    out_info->argsz = sizeof(*out_info);
    out_info->flags = VFIO_IRQ_INFO_EVENTFD;
    out_info->index = in_info->index;
    out_info->count = vfu_ctx->irq_count[in_info->index];

    return 0;
}

static void
irqs_disable(vfu_ctx_t *vfu_ctx, uint32_t index, uint32_t start, uint32_t count)
{
    size_t i;
    int *efds;

    assert(vfu_ctx != NULL);
    assert(index < VFU_DEV_NUM_IRQS);
    assert(start + count <= vfu_ctx->irq_count[index]);

    if (count == 0) {
        count = vfu_ctx->irq_count[index];
    }

    vfu_log(vfu_ctx, LOG_DEBUG, "disabling IRQ type %s range [%u, %u)",
            vfio_irq_idx_to_str(index), start, start + count);

    switch (index) {
    case VFIO_PCI_INTX_IRQ_INDEX:
    case VFIO_PCI_MSI_IRQ_INDEX:
    case VFIO_PCI_MSIX_IRQ_INDEX:
        efds = vfu_ctx->irqs->efds;
        break;
    case VFIO_PCI_ERR_IRQ_INDEX:
        efds = &vfu_ctx->irqs->err_efd;
        break;
    case VFIO_PCI_REQ_IRQ_INDEX:
        efds = &vfu_ctx->irqs->req_efd;
        break;
    }

    for (i = start; i < count; i++) {
        if (efds[i] >= 0) {
            if (close(efds[i]) == -1) {
                vfu_log(vfu_ctx, LOG_DEBUG, "failed to close IRQ fd %d: %m",
                        efds[i]);
            }

            efds[i] = -1;
        }
    }
}

void
irqs_reset(vfu_ctx_t *vfu_ctx)
{
    int *efds = vfu_ctx->irqs->efds;
    size_t i;

    irqs_disable(vfu_ctx, VFIO_PCI_REQ_IRQ_INDEX, 0, 0);
    irqs_disable(vfu_ctx, VFIO_PCI_ERR_IRQ_INDEX, 0, 0);

    for (i = 0; i < vfu_ctx->irqs->max_ivs; i++) {
        if (efds[i] >= 0) {
            if (close(efds[i]) == -1) {
                vfu_log(vfu_ctx, LOG_DEBUG, "failed to close IRQ fd %d: %m",
                        efds[i]);
            }

            efds[i] = -1;
        }
    }
}

static int
irqs_set_data_none(vfu_ctx_t *vfu_ctx, struct vfio_irq_set *irq_set)
{
    int efd;
    uint32_t i;
    long ret;
    eventfd_t val;

    for (i = irq_set->start; i < (irq_set->start + irq_set->count); i++) {
        efd = vfu_ctx->irqs->efds[i];
        if (efd >= 0) {
            val = 1;
            ret = eventfd_write(efd, val);
            if (ret == -1) {
                vfu_log(vfu_ctx, LOG_DEBUG,
                        "IRQ: failed to set data to none: %m");
                return -1;
            }
        }
    }

    return 0;
}

static int
irqs_set_data_bool(vfu_ctx_t *vfu_ctx, struct vfio_irq_set *irq_set, void *data)
{
    uint8_t *d8;
    int efd;
    uint32_t i;
    long ret;
    eventfd_t val;

    assert(data != NULL);

    for (i = irq_set->start, d8 = data; i < (irq_set->start + irq_set->count);
         i++, d8++) {
        efd = vfu_ctx->irqs->efds[i];
        if (efd >= 0 && *d8 == 1) {
            val = 1;
            ret = eventfd_write(efd, val);
            if (ret == -1) {
                vfu_log(vfu_ctx, LOG_DEBUG,
                        "IRQ: failed to set data to bool: %m");
                return -1;
            }
        }
    }

    return 0;
}

static int
irqs_set_data_eventfd(vfu_ctx_t *vfu_ctx, struct vfio_irq_set *irq_set,
                      int *data)
{
    int efd;
    uint32_t i;
    size_t j;

    assert(data != NULL);
    for (i = irq_set->start, j = 0; i < (irq_set->start + irq_set->count);
         i++, j++) {
        efd = vfu_ctx->irqs->efds[i];
        if (efd >= 0) {
            if (close(efd) == -1) {
                vfu_log(vfu_ctx, LOG_DEBUG, "failed to close IRQ fd %d: %m", efd);
            }

            vfu_ctx->irqs->efds[i] = -1;
        }
        assert(data[j] >= 0);
        /*
         * We've already checked in handle_device_set_irqs that
         * nr_fds == irq_set->count.
         */
        vfu_ctx->irqs->efds[i] = consume_fd(data, irq_set->count, j);
        vfu_log(vfu_ctx, LOG_DEBUG, "event fd[%d]=%d", i, vfu_ctx->irqs->efds[i]);
    }

    return 0;
}

static int
device_set_irqs_validate(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_irq_set *irq_set = msg->in.iov.iov_base;
    uint32_t a_type, d_type;
    int line;

    assert(vfu_ctx != NULL);
    assert(irq_set != NULL);

    if (msg->in.iov.iov_len < sizeof(*irq_set) || irq_set->argsz < sizeof(*irq_set)) {
        vfu_log(vfu_ctx, LOG_ERR, "bad size %zu", msg->in.iov.iov_len);
        return ERROR_INT(EINVAL);
    }

    // Separate action and data types from flags.
    a_type = (irq_set->flags & VFIO_IRQ_SET_ACTION_TYPE_MASK);
    d_type = (irq_set->flags & VFIO_IRQ_SET_DATA_TYPE_MASK);

    // bools provided must match count
    if (d_type == VFIO_IRQ_SET_DATA_BOOL &&
        (msg->in.iov.iov_len - sizeof(*irq_set) != sizeof(uint8_t) * irq_set->count)) {
        line = __LINE__;
        goto invalid;
    }

    // Ensure index is within bounds.
    if (irq_set->index >= VFU_DEV_NUM_IRQS) {
        line = __LINE__;
        goto invalid;
    }

    // Only one of MASK/UNMASK/TRIGGER is valid.
    if ((a_type != VFIO_IRQ_SET_ACTION_MASK) &&
        (a_type != VFIO_IRQ_SET_ACTION_UNMASK) &&
        (a_type != VFIO_IRQ_SET_ACTION_TRIGGER)) {
        line = __LINE__;
        goto invalid;
    }
    // Only one of NONE/BOOL/EVENTFD is valid.
    if ((d_type != VFIO_IRQ_SET_DATA_NONE) &&
        (d_type != VFIO_IRQ_SET_DATA_BOOL) &&
        (d_type != VFIO_IRQ_SET_DATA_EVENTFD)) {
        line = __LINE__;
        goto invalid;
    }
    // Ensure irq_set's start and count are within bounds.
    if ((irq_set->start >= vfu_ctx->irq_count[irq_set->index]) ||
        (irq_set->start + irq_set->count > vfu_ctx->irq_count[irq_set->index])) {
        line = __LINE__;
        goto invalid;
    }
    // Only TRIGGER is valid for ERR/REQ.
    if (((irq_set->index == VFIO_PCI_ERR_IRQ_INDEX) ||
         (irq_set->index == VFIO_PCI_REQ_IRQ_INDEX)) &&
        (a_type != VFIO_IRQ_SET_ACTION_TRIGGER)) {
        line = __LINE__;
        goto invalid;
    }
    // if count == 0, start must be 0 too
    if ((irq_set->count == 0) && (irq_set->start != 0)) {
        line = __LINE__;
        goto invalid;
    }
    // count == 0 is only valid with ACTION_TRIGGER and DATA_NONE.
    if ((irq_set->count == 0) && ((a_type != VFIO_IRQ_SET_ACTION_TRIGGER) ||
                                  (d_type != VFIO_IRQ_SET_DATA_NONE))) {
        line = __LINE__;
        goto invalid;
    }
    // If fd's are provided, ensure it's only for VFIO_IRQ_SET_DATA_EVENTFD
    if (msg->in.nr_fds != 0 && d_type != VFIO_IRQ_SET_DATA_EVENTFD) {
        line = __LINE__;
        goto invalid;
    }
    // If fd's are provided, ensure they match ->count
    if (msg->in.nr_fds != 0 && msg->in.nr_fds != irq_set->count) {
        line = __LINE__;
        goto invalid;
    }

    return 0;

invalid:
    vfu_log(vfu_ctx, LOG_DEBUG, "invalid SET_IRQS (%d): action=%u data_type=%u "
            "index=%u start=%u count=%u nr_fds=%zu", line, a_type, d_type,
            irq_set->index, irq_set->start, irq_set->count, msg->in.nr_fds);
    return ERROR_INT(EINVAL);
}

int
handle_device_set_irqs(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    struct vfio_irq_set *irq_set = msg->in.iov.iov_base;
    uint32_t data_type;
    int ret;

    assert(vfu_ctx != NULL);
    assert(msg != NULL);

    ret = device_set_irqs_validate(vfu_ctx, msg);
    if (ret != 0) {
        return ret;
    }

    switch (irq_set->flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
    case VFIO_IRQ_SET_ACTION_MASK:
    case VFIO_IRQ_SET_ACTION_UNMASK:
        // We're always edge-triggered without un/mask support.
        // FIXME: return an error? We don't report MASKABLE
        return 0;
    case VFIO_IRQ_SET_ACTION_TRIGGER:
        break;
    }

    data_type = irq_set->flags & VFIO_IRQ_SET_DATA_TYPE_MASK;

    if ((data_type == VFIO_IRQ_SET_DATA_NONE && irq_set->count == 0) ||
        (data_type == VFIO_IRQ_SET_DATA_EVENTFD && msg->in.nr_fds == 0)) {
        irqs_disable(vfu_ctx, irq_set->index, irq_set->start, irq_set->count);
        return 0;
    }

    vfu_log(vfu_ctx, LOG_DEBUG, "setting IRQ %s flags=%#x range [%u, %u)",
            vfio_irq_idx_to_str(irq_set->index), irq_set->flags,
            irq_set->start, irq_set->start + irq_set->count);

    switch (data_type) {
    case VFIO_IRQ_SET_DATA_NONE:
        return irqs_set_data_none(vfu_ctx, irq_set);
    case VFIO_IRQ_SET_DATA_EVENTFD:
        return irqs_set_data_eventfd(vfu_ctx, irq_set, msg->in.fds);
    case VFIO_IRQ_SET_DATA_BOOL:
        return irqs_set_data_bool(vfu_ctx, irq_set, irq_set + 1);
        break;
    default:
        // we already checked this
        abort();
    }
}

static bool
validate_irq_subindex(vfu_ctx_t *vfu_ctx, uint32_t subindex)
{
    if ((subindex >= vfu_ctx->irqs->max_ivs)) {
        vfu_log(vfu_ctx, LOG_ERR, "bad IRQ %d, max=%d", subindex,
               vfu_ctx->irqs->max_ivs);
        return false;
    }

    return true;
}

EXPORT int
vfu_irq_trigger(vfu_ctx_t *vfu_ctx, uint32_t subindex)
{
    eventfd_t val = 1;

    assert(vfu_ctx != NULL);

    if (!validate_irq_subindex(vfu_ctx, subindex)) {
        return ERROR_INT(EINVAL);
    }

    if (vfu_ctx->irqs->efds[subindex] == -1) {
        vfu_log(vfu_ctx, LOG_ERR, "no fd for interrupt %d", subindex);
        return ERROR_INT(ENOENT);
    }

    return eventfd_write(vfu_ctx->irqs->efds[subindex], val);
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
