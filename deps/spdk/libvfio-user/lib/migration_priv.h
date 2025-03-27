/*
 * Copyright (c) 2021 Nutanix Inc. All rights reserved.
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

#ifndef LIB_VFIO_USER_MIGRATION_PRIV_H
#define LIB_VFIO_USER_MIGRATION_PRIV_H

#include <linux/vfio.h>

/*
 * FSM to simplify saving device state.
 */
enum migr_iter_state {
    VFIO_USER_MIGR_ITER_STATE_INITIAL,
    VFIO_USER_MIGR_ITER_STATE_STARTED,
    VFIO_USER_MIGR_ITER_STATE_DATA_PREPARED,
    VFIO_USER_MIGR_ITER_STATE_FINISHED
};

struct migration {
    /*
     * TODO if the user provides an FD then should mmap it and use the migration
     * registers in the file
     */
    struct vfio_user_migration_info info;
    size_t pgsize;
    vfu_migration_callbacks_t callbacks;
    uint64_t data_offset;

    /*
     * This is only for the saving state. The resuming state is simpler so we
     * don't need it.
     */
    struct {
        enum migr_iter_state state;
        uint64_t pending_bytes;
        uint64_t offset;
        uint64_t size;
    } iter;
};

struct migr_state_data {
    uint32_t state;
    const char *name;
};

#define VFIO_DEVICE_STATE_V1_ERROR (VFIO_DEVICE_STATE_V1_SAVING | VFIO_DEVICE_STATE_V1_RESUMING)

/* valid migration state transitions */
static const struct migr_state_data migr_states[(VFIO_DEVICE_STATE_MASK + 1)] = {
    [VFIO_DEVICE_STATE_V1_STOP] = {
        .state =
            (1 << VFIO_DEVICE_STATE_V1_STOP) |
            (1 << VFIO_DEVICE_STATE_V1_RUNNING),
        .name = "stopped"
    },
    [VFIO_DEVICE_STATE_V1_RUNNING] = {
        .state =
            (1 << VFIO_DEVICE_STATE_V1_STOP) |
            (1 << VFIO_DEVICE_STATE_V1_RUNNING) |
            (1 << VFIO_DEVICE_STATE_V1_SAVING) |
            (1 << (VFIO_DEVICE_STATE_V1_RUNNING | VFIO_DEVICE_STATE_V1_SAVING)) |
            (1 << VFIO_DEVICE_STATE_V1_RESUMING) |
            (1 << VFIO_DEVICE_STATE_V1_ERROR),
        .name = "running"
    },
    [VFIO_DEVICE_STATE_V1_SAVING] = {
        .state =
            (1 << VFIO_DEVICE_STATE_V1_STOP) |
            (1 << VFIO_DEVICE_STATE_V1_RUNNING) |
            (1 << VFIO_DEVICE_STATE_V1_SAVING) |
            (1 << VFIO_DEVICE_STATE_V1_ERROR),
        .name = "stop-and-copy"
    },
    [VFIO_DEVICE_STATE_V1_RUNNING | VFIO_DEVICE_STATE_V1_SAVING] = {
        .state =
            (1 << VFIO_DEVICE_STATE_V1_STOP) |
            (1 << VFIO_DEVICE_STATE_V1_SAVING) |
            (1 << VFIO_DEVICE_STATE_V1_RUNNING | VFIO_DEVICE_STATE_V1_SAVING) |
            (1 << VFIO_DEVICE_STATE_V1_ERROR),
        .name = "pre-copy"
    },
    [VFIO_DEVICE_STATE_V1_RESUMING] = {
        .state =
            (1 << VFIO_DEVICE_STATE_V1_RUNNING) |
            (1 << VFIO_DEVICE_STATE_V1_RESUMING) |
            (1 << VFIO_DEVICE_STATE_V1_ERROR),
        .name = "resuming"
    }
};

MOCK_DECLARE(ssize_t, migration_region_access_registers, vfu_ctx_t *vfu_ctx,
             char *buf, size_t count, loff_t pos,  bool is_write);

MOCK_DECLARE(void, migr_state_transition, struct migration *migr,
             enum migr_iter_state state);

MOCK_DECLARE(vfu_migr_state_t, migr_state_vfio_to_vfu, uint32_t device_state);

MOCK_DECLARE(int, state_trans_notify, vfu_ctx_t *vfu_ctx,
             int (*fn)(vfu_ctx_t *, vfu_migr_state_t),
             uint32_t vfio_device_state);

#endif

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
