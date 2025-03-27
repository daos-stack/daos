/*
 * Copyright (c) 2019, Nutanix Inc. All rights reserved.
 *     Author: Thanos Makatos <thanos@nutanix.com>
 *             Swapnil Ingle <swapnil.ingle@nutanix.com>
 *             Felipe Franciosi <felipe@nutanix.com>
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
 * gpio-pci-idio-16: a simple example server identifying as a GPIO PCI device.
 */

#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "common.h"
#include "libvfio-user.h"

static void
_log(vfu_ctx_t *vfu_ctx UNUSED, UNUSED int level, char const *msg)
{
    fprintf(stderr, "gpio: %s\n", msg);
}

static int pin;
bool dirty = true;

static ssize_t
bar2_access(vfu_ctx_t *vfu_ctx UNUSED, char * const buf,
            size_t count, loff_t offset, const bool is_write)
{
    if (offset == 0 && !is_write)
        buf[0] = pin++ / 3;

    dirty = true;

    return count;
}

static void _sa_handler(UNUSED int signum)
{
}

static int
migration_device_state_transition(vfu_ctx_t *vfu_ctx, vfu_migr_state_t state)
{
    vfu_log(vfu_ctx, LOG_DEBUG, "migration: transition to state %d", state);
    return 0;
}

static uint64_t
migration_get_pending_bytes(UNUSED vfu_ctx_t *vfu_ctx)
{
    if (dirty) {
        return sizeof(pin);
    }
    return 0;
}

static int
migration_prepare_data(UNUSED vfu_ctx_t *vfu_ctx,
                       uint64_t *offset, uint64_t *size)
{
    *offset = 0;
    if (size != NULL) { /* null means resuming */
        *size = sizeof(pin);
    }
    return 0;
}

static ssize_t
migration_read_data(UNUSED vfu_ctx_t *vfu_ctx, void *buf,
                    uint64_t size, uint64_t offset)
{
    assert(offset == 0);
    assert(size == sizeof(pin));
    memcpy(buf, &pin, sizeof(pin));
    dirty = false;
    return 0;
}

static int
migration_data_written(UNUSED vfu_ctx_t *vfu_ctx, uint64_t count)
{
    assert(count == sizeof(pin));
    return 0;
}

static ssize_t
migration_write_data(UNUSED vfu_ctx_t *vfu_ctx, void *buf,
                     uint64_t size, uint64_t offset)
{
    assert(offset == 0);
    assert(size == sizeof(pin));
    memcpy(&pin, buf, sizeof(pin));
    return 0;
}

static void
dma_register(UNUSED vfu_ctx_t *vfu_ctx, UNUSED vfu_dma_info_t *info)
{
}

static void
dma_unregister(UNUSED vfu_ctx_t *vfu_ctx, UNUSED vfu_dma_info_t *info)
{
}

int
main(int argc, char *argv[])
{
    int ret;
    bool verbose = false;
    bool restart = true;
    bool enable_migr = true;
    int opt;
    struct sigaction act = { .sa_handler = _sa_handler };
    vfu_ctx_t *vfu_ctx;
    size_t migr_regs_size = vfu_get_migr_register_area_size();
    size_t migr_data_size = sysconf(_SC_PAGE_SIZE);
    size_t migr_size = migr_regs_size + migr_data_size;
    const vfu_migration_callbacks_t migr_callbacks = {
        .version = VFU_MIGR_CALLBACKS_VERS,
        .transition = &migration_device_state_transition,
        .get_pending_bytes = &migration_get_pending_bytes,
        .prepare_data = &migration_prepare_data,
        .read_data = &migration_read_data,
        .data_written = &migration_data_written,
        .write_data = &migration_write_data
    };

    while ((opt = getopt(argc, argv, "MRv")) != -1) {
        switch (opt) {
            case 'R':
                restart = false;
                break;
            case 'v':
                verbose = true;
                break;
            case 'M':
                enable_migr = false;
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-Rv] <socketpath>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        errx(EXIT_FAILURE, "missing vfio-user socket path");
    }

    sigemptyset(&act.sa_mask);
    if (sigaction(SIGINT, &act, NULL) == -1) {
        err(EXIT_FAILURE, "failed to register signal handler");
    }

    vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, argv[optind], 0, NULL,
                             VFU_DEV_TYPE_PCI);
    if (vfu_ctx == NULL) {
        if (errno == EINTR) {
            printf("interrupted\n");
            exit(EXIT_SUCCESS);
        }
        err(EXIT_FAILURE, "failed to initialize device emulation");
    }

    ret = vfu_setup_log(vfu_ctx, _log, verbose ? LOG_DEBUG : LOG_ERR);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to setup log");
    }

    ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL,
                       PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0) {
        err(EXIT_FAILURE, "vfu_pci_init() failed");
    }

    vfu_pci_set_id(vfu_ctx, 0x494f, 0x0dc8, 0x0, 0x0);

    ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR2_REGION_IDX, 0x100,
                           &bar2_access, VFU_REGION_FLAG_RW, NULL, 0, -1, 0);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to setup region");
    }

    if (enable_migr) {
        ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_MIGR_REGION_IDX, migr_size,
                               NULL, VFU_REGION_FLAG_RW, NULL, 0, -1, 0);
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to setup migration region");
        }
        ret = vfu_setup_device_migration_callbacks(vfu_ctx, &migr_callbacks,
                                                   migr_regs_size);
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to setup device migration");
        }
    }

    ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_INTX_IRQ, 1);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to setup irq counts");
    }

    ret = vfu_setup_device_dma(vfu_ctx, dma_register, dma_unregister);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to setup DMA");
    }

    ret = vfu_realize_ctx(vfu_ctx);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to realize device");
    }

    ret = vfu_attach_ctx(vfu_ctx);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to attach device");
    }

    do {
        ret = vfu_run_ctx(vfu_ctx);
        if (ret != 0) {
            if (errno == ENOTCONN) {
                ret = vfu_attach_ctx(vfu_ctx);
                if (ret < 0) {
                    err(EXIT_FAILURE, "failed to re-attach device");
                 }
            } else if (errno != EINTR) {
                err(EXIT_FAILURE, "vfu_run_ctx() failed");
            }
        }
    } while (restart);

    vfu_destroy_ctx(vfu_ctx);
    return EXIT_SUCCESS;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
