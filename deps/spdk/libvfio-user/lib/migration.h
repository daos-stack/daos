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


// FIXME: license header (and SPDX ?) everywhere

#ifndef LIB_VFIO_USER_MIGRATION_H
#define LIB_VFIO_USER_MIGRATION_H

/*
 * These are not public routines, but for convenience, they are used by the
 * sample/test code as well as privately within libvfio-user.
 */

#include <stddef.h>

#include "libvfio-user.h"
#include "private.h"

struct migration *
init_migration(const vfu_migration_callbacks_t *callbacks,
               uint64_t data_offset, int *err);

ssize_t
migration_region_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                        loff_t pos, bool is_write);

bool
migration_available(vfu_ctx_t *vfu_ctx);

MOCK_DECLARE(bool, device_is_stopped, struct migration *migr);

MOCK_DECLARE(bool, device_is_stopped_and_copying, struct migration *migration);

size_t
migration_get_pgsize(struct migration *migr);

int
migration_set_pgsize(struct migration *migr, size_t pgsize);

MOCK_DECLARE(bool, vfio_migr_state_transition_is_valid, uint32_t from,
             uint32_t to);

MOCK_DECLARE(ssize_t, handle_device_state, vfu_ctx_t *vfu_ctx,
             struct migration *migr, uint32_t device_state, bool notify);

bool
access_migration_needs_quiesce(const vfu_ctx_t *vfu_ctx, size_t region_index,
                              uint64_t offset);

#endif /* LIB_VFIO_USER_MIGRATION_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
