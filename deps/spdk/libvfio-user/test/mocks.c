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

#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <cmocka.h>

#include "dma.h"
#include "migration.h"
#include "mocks.h"
#include "private.h"
#include "tran_sock.h"
#include "migration_priv.h"

struct function
{
    const char *name;
    bool patched;
};

static int (*__real_close)(int);

static struct function funcs[] = {
    /* mocked internal funcs */
    { .name = "cmd_allowed_when_stopped_and_copying" },
    { .name = "device_is_stopped_and_copying" },
    { .name = "device_is_stopped" },
    { .name = "dma_controller_add_region" },
    { .name = "dma_controller_remove_region" },
    { .name = "dma_controller_unmap_region" },
    { .name = "should_exec_command" },
    { .name = "migration_region_access_registers" },
    { .name = "handle_device_state" },
    {. name = "vfio_migr_state_transition_is_valid" },
    { .name = "state_trans_notify" },
    { .name = "migr_trans_to_valid_state" },
    { .name = "migr_state_vfio_to_vfu" },
    { .name = "migr_state_transition" },
    /* system libs */
    { .name = "bind" },
    { .name = "close" },
    { .name = "listen" },
};

static struct function *
find(const char *name)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(funcs); i++) {
        if (strcmp(name, funcs[i].name) == 0) {
            return &funcs[i];
        }
    }
    assert(false);
}

void
patch(const char *name)
{
    struct function *func = find(name);
    func->patched = true;
}

static bool
is_patched(const char *name)
{
    return find(name)->patched;
}

void
unpatch_all(void)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(funcs); i++) {
        funcs[i].patched = false;
    }
}

int
dma_controller_add_region(dma_controller_t *dma, void *dma_addr,
                          size_t size, int fd, off_t offset,
                          uint32_t prot)
{
    if (!is_patched("dma_controller_add_region")) {
        return __real_dma_controller_add_region(dma, dma_addr, size, fd, offset,
                                                prot);
    }

    check_expected_ptr(dma);
    check_expected(dma_addr);
    check_expected(size);
    check_expected(fd);
    check_expected(offset);
    check_expected(prot);
    errno = mock();
    return mock();
}

int
dma_controller_remove_region(dma_controller_t *dma,
                             void *dma_addr, size_t size,
                             vfu_dma_unregister_cb_t *dma_unregister,
                             void *data)
{
    if (!is_patched("dma_controller_remove_region")) {
        return __real_dma_controller_remove_region(dma, dma_addr, size,
                                                   dma_unregister, data);
    }

    check_expected(dma);
    check_expected(dma_addr);
    check_expected(size);
    check_expected(dma_unregister);
    check_expected(data);
    return mock();
}

void
dma_controller_unmap_region(dma_controller_t *dma,
                            dma_memory_region_t *region)
{
    check_expected(dma);
    check_expected(region);
}

bool
device_is_stopped(struct migration *migration)
{
    if (!is_patched("device_is_stopped")) {
        return __real_device_is_stopped(migration);
    }
    check_expected(migration);
    return mock();
}

bool
device_is_stopped_and_copying(struct migration *migration)
{
    if (!is_patched("device_is_stopped_and_copying")) {
        return __real_device_is_stopped_and_copying(migration);
    }
    check_expected(migration);
    return mock();
}

bool
cmd_allowed_when_stopped_and_copying(uint16_t cmd)
{
    if (!is_patched("cmd_allowed_when_stopped_and_copying")) {
        return __real_cmd_allowed_when_stopped_and_copying(cmd);
    }
    check_expected(cmd);
    return mock();
}

bool
should_exec_command(vfu_ctx_t *vfu_ctx, uint16_t cmd)
{
    if (!is_patched("should_exec_command")) {
        return __real_should_exec_command(vfu_ctx, cmd);
    }
    check_expected(vfu_ctx);
    check_expected(cmd);
    return mock();
}

ssize_t
migration_region_access_registers(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                                  loff_t pos, bool is_write)
{
    if (!is_patched("migration_region_access_registers")) {
        return __real_migration_region_access_registers(vfu_ctx, buf, count,
                                                        pos, is_write);
    }
    check_expected(vfu_ctx);
    check_expected(buf);
    check_expected(count);
    check_expected(pos);
    check_expected(is_write);
    errno = mock();
    return mock();
}

ssize_t
handle_device_state(vfu_ctx_t *vfu_ctx, struct migration *migr,
                    uint32_t device_state, bool notify) {

    if (!is_patched("handle_device_state")) {
        return __real_handle_device_state(vfu_ctx, migr, device_state,
                                          notify);
    }
    check_expected(vfu_ctx);
    check_expected(migr);
    check_expected(device_state);
    check_expected(notify);
    return mock();
}

void
migr_state_transition(struct migration *migr, enum migr_iter_state state)
{
    if (!is_patched("migr_state_transition")) {
        __real_migr_state_transition(migr, state);
        return;
    }
    check_expected(migr);
    check_expected(state);
}

bool
vfio_migr_state_transition_is_valid(uint32_t from, uint32_t to)
{
    if (!is_patched("vfio_migr_state_transition_is_valid")) {
        return __real_vfio_migr_state_transition_is_valid(from, to);
    }
    check_expected(from);
    check_expected(to);
    return mock();
}

int
state_trans_notify(vfu_ctx_t *vfu_ctx, int (*fn)(vfu_ctx_t*, vfu_migr_state_t),
                   uint32_t vfio_device_state)
{
    if (!is_patched("state_trans_notify")) {
        return __real_state_trans_notify(vfu_ctx, fn, vfio_device_state);
    }
    check_expected(vfu_ctx);
    check_expected(fn);
    check_expected(vfio_device_state);
    errno = mock();
    return mock();
}

ssize_t
migr_trans_to_valid_state(vfu_ctx_t *vfu_ctx, struct migration *migr,
                          uint32_t device_state, bool notify)
{
    if (!is_patched("migr_trans_to_valid_state")) {
        return __real_migr_trans_to_valid_state(vfu_ctx, migr, device_state,
                                                notify);
    }
    check_expected(vfu_ctx);
    check_expected(migr);
    check_expected(device_state);
    check_expected(notify);
    return mock();
}

vfu_migr_state_t
migr_state_vfio_to_vfu(uint32_t vfio_device_state)
{
    if (!is_patched("migr_state_vfio_to_vfu")) {
        return __real_migr_state_vfio_to_vfu(vfio_device_state);
    }
    check_expected(vfio_device_state);
    return mock();
}

/* Always mocked. */
void
mock_dma_register(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    check_expected(vfu_ctx);
    check_expected(info);
}

void
mock_dma_unregister(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    check_expected(vfu_ctx);
    check_expected(info);
}

int
mock_reset_cb(vfu_ctx_t *vfu_ctx, vfu_reset_type_t type)
{
    check_expected(vfu_ctx);
    check_expected(type);
    return mock();
}


int
mock_notify_migr_state_trans_cb(vfu_ctx_t *vfu_ctx, vfu_migr_state_t vfu_state)
{
    check_expected(vfu_ctx);
    check_expected(vfu_state);
    return mock();
}

/* System-provided funcs. */

int
bind(int sockfd UNUSED, const struct sockaddr *addr UNUSED,
     socklen_t addrlen UNUSED)
{
    return 0;
}

int
close(int fd)
{
    if (!is_patched("close")) {
        if (__real_close == NULL) {
            __real_close = dlsym(RTLD_NEXT, "close");
        }

        return __real_close(fd);
    }

    check_expected(fd);
    return mock();
}

int
listen(int sockfd UNUSED, int backlog UNUSED)
{
    return 0;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
