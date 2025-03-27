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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <alloca.h>
#include <string.h>
#include <linux/pci_regs.h>
#include <sys/param.h>

#include "dma.h"
#include "irq.h"
#include "libvfio-user.h"
#include "migration.h"
#include "migration_priv.h"
#include "mocks.h"
#include "pci.h"
#include "private.h"
#include "tran_sock.h"

#define DMACSIZE (sizeof(dma_controller_t) + sizeof(dma_memory_region_t) * 5)

/*
 * These globals are used in the unit tests; they're re-initialized each time by
 * setup(), but having them as globals makes for significantly less
 * boiler-plate.
 */
static char dmacbuf[DMACSIZE];
static vfu_ctx_t vfu_ctx;
static vfu_msg_t msg;
static size_t nr_fds;
static int fds[2];
static int ret;

static vfu_msg_t *
mkmsg(enum vfio_user_command cmd, void *data, size_t size)
{
    msg.hdr.cmd = cmd;
    msg.hdr.msg_size = size;
    msg.in.iov.iov_base = data;
    msg.in.iov.iov_len = size;

    if (nr_fds != 0) {
        msg.in.fds = fds;
        msg.in.nr_fds = nr_fds;
    } else {
        msg.in.fds = NULL;
        msg.in.nr_fds = 0;
    }

    return &msg;
}

/*
 * FIXME we shouldn't have to specify a setup function explicitly for each unit
 * test, cmocka should provide that. E.g. cmocka_run_group_tests enables us to
 * run a function before/after ALL unit tests have finished, we can extend it
 * and provide a function to execute before and after each unit test.
 */
static int
setup(void **state UNUSED)
{
    memset(&vfu_ctx, 0, sizeof(vfu_ctx));

    vfu_ctx.client_max_fds = 10;

    memset(dmacbuf, 0, DMACSIZE);

    vfu_ctx.dma = (void *)dmacbuf;
    vfu_ctx.dma->max_regions = 10;
    vfu_ctx.dma->vfu_ctx = &vfu_ctx;

    memset(&msg, 0, sizeof(msg));

    msg.hdr.flags.type = VFIO_USER_F_TYPE_COMMAND;
    msg.hdr.msg_size = sizeof(msg.hdr);

    fds[0] = fds[1] = -1;
    nr_fds = 0;
    ret = 0;

    unpatch_all();
    return 0;
}

/* FIXME must replace test_dma_map_without_dma */

static void
test_dma_map_mappable_without_fd(void **state UNUSED)
{
    struct vfio_user_dma_map dma_map = {
        .argsz = sizeof(dma_map),
    };

    ret = handle_dma_map(&vfu_ctx,
                         mkmsg(VFIO_USER_DMA_MAP, &dma_map, sizeof(dma_map)),
                         &dma_map);
    assert_int_equal(0, ret);
}

static void
test_dma_map_without_fd(void **state UNUSED)
{
    struct vfio_user_dma_map dma_map = {
        .argsz = sizeof(dma_map),
        .addr = 0xdeadbeef,
        .size = 0xcafebabe,
        .offset = 0x8badf00d,
        .flags = 0
    };

    patch("dma_controller_add_region");
    will_return(dma_controller_add_region, 0);
    will_return(dma_controller_add_region, 0);
    expect_value(dma_controller_add_region, dma, vfu_ctx.dma);
    expect_value(dma_controller_add_region, dma_addr, dma_map.addr);
    expect_value(dma_controller_add_region, size, dma_map.size);
    expect_value(dma_controller_add_region, fd, -1);
    expect_value(dma_controller_add_region, offset, dma_map.offset);
    expect_value(dma_controller_add_region, prot, PROT_NONE);
    ret = handle_dma_map(&vfu_ctx,
                         mkmsg(VFIO_USER_DMA_MAP, &dma_map, sizeof(dma_map)),
                         &dma_map);
    assert_int_equal(0, ret);
}

static int
check_dma_info(const LargestIntegralType value,
               const LargestIntegralType cvalue)
{
    vfu_dma_info_t *info = (vfu_dma_info_t *)value;
    vfu_dma_info_t *cinfo = (vfu_dma_info_t *)cvalue;

    return info->iova.iov_base == cinfo->iova.iov_base &&
        info->iova.iov_len == cinfo->iova.iov_len &&
        info->vaddr == cinfo->vaddr &&
        info->mapping.iov_base == cinfo->mapping.iov_base &&
        info->mapping.iov_len == cinfo->mapping.iov_len &&
        info->page_size == cinfo->page_size &&
        info->prot == cinfo->prot;
}

/*
 * Checks that handle_dma_map returns 0 when dma_controller_add_region
 * succeeds.
 */
static void
test_dma_map_return_value(void **state UNUSED)
{
    dma_controller_t dma = { 0 };
    vfu_ctx_t vfu_ctx = { .dma = &dma };
    dma.vfu_ctx = &vfu_ctx;
    struct vfio_user_dma_map dma_map = {
        .argsz = sizeof(dma_map)
    };

    patch("dma_controller_add_region");
    expect_value(dma_controller_add_region, dma, vfu_ctx.dma);
    expect_value(dma_controller_add_region, dma_addr, dma_map.addr);
    expect_value(dma_controller_add_region, size, dma_map.size);
    expect_value(dma_controller_add_region, fd, -1);
    expect_value(dma_controller_add_region, offset, dma_map.offset);
    expect_value(dma_controller_add_region, prot, PROT_NONE);
    will_return(dma_controller_add_region, 0);
    will_return(dma_controller_add_region, 2);

    assert_int_equal(0, handle_dma_map(&vfu_ctx,
                                       mkmsg(VFIO_USER_DMA_MAP, &dma_map,
                                             sizeof(dma_map)),
                                       &dma_map));
}

/*
 * Tests that handle_dma_unmap correctly removes a region.
 */
static void
test_handle_dma_unmap(void **state UNUSED)
{
    struct vfio_user_dma_unmap dma_unmap = {
        .argsz = sizeof(dma_unmap),
        .addr = 0x1000,
        .size = 0x1000
    };

    vfu_ctx.dma->nregions = 3;
    vfu_ctx.dma->regions[0].info.iova.iov_base = (void *)0x1000;
    vfu_ctx.dma->regions[0].info.iova.iov_len = 0x1000;
    vfu_ctx.dma->regions[0].fd = -1;
    vfu_ctx.dma->regions[1].info.iova.iov_base = (void *)0x4000;
    vfu_ctx.dma->regions[1].info.iova.iov_len = 0x2000;
    vfu_ctx.dma->regions[1].fd = -1;
    vfu_ctx.dma->regions[2].info.iova.iov_base = (void *)0x8000;
    vfu_ctx.dma->regions[2].info.iova.iov_len = 0x3000;
    vfu_ctx.dma->regions[2].fd = -1;

    vfu_ctx.dma_unregister = mock_dma_unregister;

    expect_value(mock_dma_unregister, vfu_ctx, &vfu_ctx);
    expect_check(mock_dma_unregister, info, check_dma_info,
                 &vfu_ctx.dma->regions[0].info);

    ret = handle_dma_unmap(&vfu_ctx,
                           mkmsg(VFIO_USER_DMA_UNMAP, &dma_unmap,
                                 sizeof(dma_unmap)),
                           &dma_unmap);

    assert_int_equal(0, ret);
    assert_int_equal(2, vfu_ctx.dma->nregions);
    assert_int_equal(0x4000, vfu_ctx.dma->regions[0].info.iova.iov_base);
    assert_int_equal(0x2000, vfu_ctx.dma->regions[0].info.iova.iov_len);
    assert_int_equal(0x8000, vfu_ctx.dma->regions[1].info.iova.iov_base);
    assert_int_equal(0x3000, vfu_ctx.dma->regions[1].info.iova.iov_len);
    free(msg.out.iov.iov_base);
}

static void
test_dma_controller_add_region_no_fd(void **state UNUSED)
{
    vfu_dma_addr_t dma_addr = (void *)0xdeadbeef;
    dma_memory_region_t *r;
    off_t offset = 0;
    size_t size = 0;
    int fd = -1;

    assert_int_equal(0, dma_controller_add_region(vfu_ctx.dma, dma_addr,
                                                  size, fd, offset, PROT_NONE));
    assert_int_equal(1, vfu_ctx.dma->nregions);
    r = &vfu_ctx.dma->regions[0];
    assert_ptr_equal(NULL, r->info.vaddr);
    assert_ptr_equal(NULL, r->info.mapping.iov_base);
    assert_int_equal(0, r->info.mapping.iov_len);
    assert_ptr_equal(dma_addr, r->info.iova.iov_base);
    assert_int_equal(size, r->info.iova.iov_len);
    assert_int_equal(0x1000, r->info.page_size);
    assert_int_equal(offset, r->offset);
    assert_int_equal(fd, r->fd);
    assert_int_equal(0, r->refcnt);
    assert_int_equal(PROT_NONE, r->info.prot);
}

static void
test_dma_controller_remove_region_mapped(void **state UNUSED)
{
    vfu_ctx.dma->nregions = 1;
    vfu_ctx.dma->regions[0].info.iova.iov_base = (void *)0xdeadbeef;
    vfu_ctx.dma->regions[0].info.iova.iov_len = 0x100;
    vfu_ctx.dma->regions[0].info.mapping.iov_base = (void *)0xcafebabe;
    vfu_ctx.dma->regions[0].info.mapping.iov_len = 0x1000;
    vfu_ctx.dma->regions[0].info.vaddr = (void *)0xcafebabe;

    expect_value(mock_dma_unregister, vfu_ctx, &vfu_ctx);
    expect_check(mock_dma_unregister, info, check_dma_info,
                 &vfu_ctx.dma->regions[0].info);
    /* FIXME add unit test when dma_unregister fails */
    patch("dma_controller_unmap_region");
    expect_value(dma_controller_unmap_region, dma, vfu_ctx.dma);
    expect_value(dma_controller_unmap_region, region, &vfu_ctx.dma->regions[0]);
    assert_int_equal(0,
        dma_controller_remove_region(vfu_ctx.dma, (void *)0xdeadbeef, 0x100,
            mock_dma_unregister, &vfu_ctx));
}

static void
test_dma_controller_remove_region_unmapped(void **state UNUSED)
{
    vfu_ctx.dma->nregions = 1;
    vfu_ctx.dma->regions[0].info.iova.iov_base = (void *)0xdeadbeef;
    vfu_ctx.dma->regions[0].info.iova.iov_len = 0x100;
    vfu_ctx.dma->regions[0].fd = -1;

    expect_value(mock_dma_unregister, vfu_ctx, &vfu_ctx);
    expect_check(mock_dma_unregister, info, check_dma_info,
                 &vfu_ctx.dma->regions[0].info);
    patch("dma_controller_unmap_region");
    assert_int_equal(0,
        dma_controller_remove_region(vfu_ctx.dma, (void *)0xdeadbeef, 0x100,
            mock_dma_unregister, &vfu_ctx));
}

static void
test_dma_addr_to_sg(void **state UNUSED)
{
    dma_memory_region_t *r, *r1;
    struct iovec iov[2] = { };
    dma_sg_t sg[2];
    int ret;

    vfu_ctx.dma->nregions = 1;
    r = &vfu_ctx.dma->regions[0];
    r->info.iova.iov_base = (void *)0x1000;
    r->info.iova.iov_len = 0x4000;
    r->info.vaddr = (void *)0xdeadbeef;

    /* fast path, region hint hit */
    r->info.prot = PROT_WRITE;
    ret = dma_addr_to_sg(vfu_ctx.dma, (vfu_dma_addr_t)0x2000,
                         0x400, sg, 1, PROT_READ);
    assert_int_equal(1, ret);
    assert_int_equal(r->info.iova.iov_base, sg[0].dma_addr);
    assert_int_equal(0, sg[0].region);
    assert_int_equal(0x2000 - (unsigned long long)r->info.iova.iov_base,
                     sg[0].offset);
    assert_int_equal(0x400, sg[0].length);
    assert_true(vfu_sg_is_mappable(&vfu_ctx, &sg[0]));

    errno = 0;
    r->info.prot = PROT_WRITE;
    ret = dma_addr_to_sg(vfu_ctx.dma, (vfu_dma_addr_t)0x6000,
                         0x400, sg, 1, PROT_READ);
    assert_int_equal(-1, ret);
    assert_int_equal(ENOENT, errno);

    r->info.prot = PROT_READ;
    ret = dma_addr_to_sg(vfu_ctx.dma, (vfu_dma_addr_t)0x2000,
                         0x400, sg, 1, PROT_WRITE);
    assert_int_equal(-1, ret);
    assert_int_equal(EACCES, errno);

    r->info.prot = PROT_READ|PROT_WRITE;
    ret = dma_addr_to_sg(vfu_ctx.dma, (vfu_dma_addr_t)0x2000,
                         0x400, sg, 1, PROT_READ);
    assert_int_equal(1, ret);

    vfu_ctx.dma->nregions = 2;
    r1 = &vfu_ctx.dma->regions[1];
    r1->info.iova.iov_base = (void *)0x5000;
    r1->info.iova.iov_len = 0x2000;
    r1->info.vaddr = (void *)0xcafebabe;
    r1->info.prot = PROT_WRITE;
    ret = dma_addr_to_sg(vfu_ctx.dma, (vfu_dma_addr_t)0x1000,
                         0x5000, sg, 2, PROT_READ);
    assert_int_equal(2, ret);
    assert_int_equal(0x4000, sg[0].length);
    assert_int_equal(r->info.iova.iov_base, sg[0].dma_addr);
    assert_int_equal(0, sg[0].region);
    assert_int_equal(0, sg[0].offset);
    assert_true(vfu_sg_is_mappable(&vfu_ctx, &sg[0]));

    assert_int_equal(0x1000, sg[1].length);
    assert_int_equal(r1->info.iova.iov_base, sg[1].dma_addr);
    assert_int_equal(1, sg[1].region);
    assert_int_equal(0, sg[1].offset);
    assert_true(vfu_sg_is_mappable(&vfu_ctx, &sg[1]));

    assert_int_equal(0, dma_map_sg(vfu_ctx.dma, sg, iov, 2));
    assert_int_equal(r->info.vaddr + sg[0].offset, iov[0].iov_base);
    assert_int_equal(sg[0].length, iov[0].iov_len);
    assert_int_equal(r1->info.vaddr + sg[1].offset, iov[1].iov_base);
    assert_int_equal(sg[1].length, iov[1].iov_len);

    /* TODO test more scenarios */
}

static void
test_vfu_setup_device_dma(void **state UNUSED)
{
    vfu_ctx_t vfu_ctx = { 0 };

    assert_int_equal(0, vfu_setup_device_dma(&vfu_ctx, NULL, NULL));
    assert_non_null(vfu_ctx.dma);
    free(vfu_ctx.dma);
}

typedef struct {
    int fd;
    int conn_fd;
} tran_sock_t;

static void
test_migration_state_transitions(void **state UNUSED)
{
    bool (*f)(uint32_t, uint32_t) = vfio_migr_state_transition_is_valid;
    uint32_t i, j;

    /* from stopped (000b): all transitions are invalid except to running */
    assert_true(f(0, 0));
    assert_true(f(0, 1));
    for (i = 2; i < 8; i++) {
        assert_false(f(0, i));
    }

    /* from running (001b) */
    assert_true(f(1, 0));
    assert_true(f(1, 1));
    assert_true(f(1, 2));
    assert_true(f(1, 3));
    assert_true(f(1, 4));
    assert_false(f(1, 5));
    assert_true(f(1, 6));
    assert_false(f(1, 5));

    /* from stop-and-copy (010b) */
    assert_true(f(2, 0));
    assert_true(f(2, 1));
    assert_true(f(2, 2));
    assert_false(f(2, 3));
    assert_false(f(2, 4));
    assert_false(f(2, 5));
    assert_true(f(2, 6));
    assert_false(f(2, 7));

    /* from pre-copy (011b) */
    assert_true(f(3, 0));
    assert_true(f(3, 1));
    assert_true(f(3, 2));
    assert_false(f(3, 3));
    assert_false(f(3, 4));
    assert_false(f(3, 5));
    assert_true(f(3, 6));
    assert_false(f(3, 7));

    /* from resuming (100b) */
    assert_false(f(4, 0));
    assert_true(f(4, 1));
    assert_false(f(4, 2));
    assert_false(f(4, 3));
    assert_true(f(4, 4));
    assert_false(f(4, 5));
    assert_true(f(4, 6));
    assert_false(f(4, 7));

    /*
     * Transitioning to any other state from the remaining 3 states
     * (101b - invalid, 110b - error, 111b - invalid)  is invalid.
     * Transitioning from the error state to the stopped state is possible but
     * that requires a device reset, so we don't consider it a valid state
     * transition.
     */
    for (i = 5; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            assert_false(f(i, j));
        }
    }
}

static struct test_setup_migr_reg_dat {
    vfu_ctx_t *v;
    size_t rs; /* migration registers size */
    size_t ds; /* migration data size */
    size_t s; /* migration region size*/
    const vfu_migration_callbacks_t c;
} migr_reg_data = {
    .c = {
        .version = VFU_MIGR_CALLBACKS_VERS,
        .transition = (void *)0x1,
        .get_pending_bytes = (void *)0x2,
        .prepare_data = (void *)0x3,
        .read_data = (void *)0x4,
        .write_data = (void *)0x5,
        .data_written = (void *)0x6
    }
};

static int
setup_test_setup_migration_region(void **state)
{
    struct test_setup_migr_reg_dat *p = &migr_reg_data;
    p->v = vfu_create_ctx(VFU_TRANS_SOCK, "test", 0, NULL,
        VFU_DEV_TYPE_PCI);
    if (p->v == NULL) {
        return -1;
    }
    p->rs = ROUND_UP(sizeof(struct vfio_user_migration_info),
                     sysconf(_SC_PAGE_SIZE));
    p->ds = sysconf(_SC_PAGE_SIZE);
    p->s = p->rs + p->ds;
    *state = p;
    return setup(state);
}

static vfu_ctx_t *
get_vfu_ctx(void **state)
{
    return (*((struct test_setup_migr_reg_dat **)(state)))->v;
}

static int
teardown_test_setup_migration_region(void **state)
{
    struct test_setup_migr_reg_dat *p = *state;
    vfu_destroy_ctx(p->v);
    return 0;
}

static void
test_setup_migration_region_size_ok(void **state)
{
    vfu_ctx_t *v = get_vfu_ctx(state);
    int r = vfu_setup_region(v, VFU_PCI_DEV_MIGR_REGION_IDX,
        vfu_get_migr_register_area_size(), NULL,
        VFU_REGION_FLAG_READ | VFU_REGION_FLAG_WRITE, NULL, 0, -1, 0);
    assert_int_equal(0, r);
}

static void
test_setup_migration_region_sparsely_mappable_valid(void **state)
{
    struct test_setup_migr_reg_dat *p = *state;
    struct iovec mmap_areas[] = {
        [0] = {
            .iov_base = (void *)p->rs,
            .iov_len = p->ds
        }
    };
    int r = vfu_setup_region(p->v, VFU_PCI_DEV_MIGR_REGION_IDX, p->s, NULL,
        VFU_REGION_FLAG_READ | VFU_REGION_FLAG_WRITE, mmap_areas, 1,
        0xdeadbeef, 0);
    assert_int_equal(0, r);
}

static void
test_setup_migration_callbacks_without_migration_region(void **state)
{
    struct test_setup_migr_reg_dat *p = *state;
    assert_int_equal(-1, vfu_setup_device_migration_callbacks(p->v, &p->c, 0));
    assert_int_equal(EINVAL, errno);
}

static void
test_setup_migration_callbacks_bad_data_offset(void **state)
{
    struct test_setup_migr_reg_dat *p = *state;
    int r = vfu_setup_region(p->v, VFU_PCI_DEV_MIGR_REGION_IDX, p->s, NULL,
        VFU_REGION_FLAG_READ | VFU_REGION_FLAG_WRITE, NULL, 0, -1, 0);
    assert_int_equal(0, r);
    r = vfu_setup_device_migration_callbacks(p->v, &p->c,
        vfu_get_migr_register_area_size() - 1);
    assert_int_equal(-1, r);
}

static void
test_setup_migration_callbacks(void **state)
{
    struct test_setup_migr_reg_dat *p = *state;
    int r = vfu_setup_region(p->v, VFU_PCI_DEV_MIGR_REGION_IDX, p->s, NULL,
        VFU_REGION_FLAG_READ | VFU_REGION_FLAG_WRITE, NULL, 0, -1, 0);
    assert_int_equal(0, r);
    r = vfu_setup_device_migration_callbacks(p->v, &p->c,
        vfu_get_migr_register_area_size());
    assert_int_equal(0, r);
    assert_non_null(p->v->migration);
    /* FIXME can't validate p->v->migration because it's a private strcut, need to move it out of lib/migration.c */
}

static void
test_device_is_stopped_and_copying(UNUSED void **state)
{
    assert_false(device_is_stopped_and_copying(vfu_ctx.migration));
    assert_false(device_is_stopped(vfu_ctx.migration));

    size_t i;
    struct migration migration;
    vfu_ctx.migration = &migration;
    for (i = 0; i < ARRAY_SIZE(migr_states); i++) {
        if (migr_states[i].name == NULL) {
            continue;
        }
        migration.info.device_state = i;
        bool r = device_is_stopped_and_copying(vfu_ctx.migration);
        if (i == VFIO_DEVICE_STATE_V1_SAVING) {
            assert_true(r);
        } else {
            assert_false(r);
        }
        r = device_is_stopped(vfu_ctx.migration);
        if (i == VFIO_DEVICE_STATE_V1_STOP) {
            assert_true(r);
        } else {
            assert_false(r);
        }
    }
}

static void
test_cmd_allowed_when_stopped_and_copying(UNUSED void **state)
{
    size_t i;

    for (i = 0; i < VFIO_USER_MAX; i++) {
        bool r = cmd_allowed_when_stopped_and_copying(i);
        if (i == VFIO_USER_REGION_READ || i == VFIO_USER_REGION_WRITE ||
            i == VFIO_USER_DIRTY_PAGES) {
            assert_true(r);
        } else {
            assert_false(r);
        }
    }
}

static void
test_should_exec_command(UNUSED void **state)
{
    struct migration migration = { { 0 } };

    vfu_ctx.migration = &migration;

    patch("device_is_stopped_and_copying");
    patch("cmd_allowed_when_stopped_and_copying");
    patch("device_is_stopped");

    /* TEST stopped and copying, command allowed */
    will_return(device_is_stopped_and_copying, true);
    expect_value(device_is_stopped_and_copying, migration, &migration);
    will_return(cmd_allowed_when_stopped_and_copying, true);
    expect_value(cmd_allowed_when_stopped_and_copying, cmd, 0xbeef);
    assert_true(should_exec_command(&vfu_ctx, 0xbeef));

    /* TEST stopped and copying, command not allowed */
    will_return(device_is_stopped_and_copying, true);
    expect_any(device_is_stopped_and_copying, migration);
    will_return(cmd_allowed_when_stopped_and_copying, false);
    expect_any(cmd_allowed_when_stopped_and_copying, cmd);
    assert_false(should_exec_command(&vfu_ctx, 0xbeef));

    /* TEST stopped */
    will_return(device_is_stopped_and_copying, false);
    expect_any(device_is_stopped_and_copying, migration);
    will_return(device_is_stopped, true);
    expect_value(device_is_stopped, migration, &migration);
    will_return(cmd_allowed_when_stopped_and_copying, false);
    expect_value(cmd_allowed_when_stopped_and_copying, cmd, 0xbeef);
    assert_false(should_exec_command(&vfu_ctx, 0xbeef));

    /* TEST none of the above */
    will_return(device_is_stopped_and_copying, false);
    expect_any(device_is_stopped_and_copying, migration);
    will_return(device_is_stopped, false);
    expect_any(device_is_stopped, migration);
    assert_true(should_exec_command(&vfu_ctx, 0xbeef));
}

int
main(void)
{
   const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_dma_map_mappable_without_fd, setup),
        cmocka_unit_test_setup(test_dma_map_without_fd, setup),
        cmocka_unit_test_setup(test_dma_map_return_value, setup),
        cmocka_unit_test_setup(test_handle_dma_unmap, setup),
        cmocka_unit_test_setup(test_dma_controller_add_region_no_fd, setup),
        cmocka_unit_test_setup(test_dma_controller_remove_region_mapped, setup),
        cmocka_unit_test_setup(test_dma_controller_remove_region_unmapped, setup),
        cmocka_unit_test_setup(test_dma_addr_to_sg, setup),
        cmocka_unit_test_setup(test_vfu_setup_device_dma, setup),
        cmocka_unit_test_setup(test_migration_state_transitions, setup),
        cmocka_unit_test_setup_teardown(test_setup_migration_region_size_ok,
            setup_test_setup_migration_region,
            teardown_test_setup_migration_region),
        cmocka_unit_test_setup_teardown(test_setup_migration_region_sparsely_mappable_valid,
            setup_test_setup_migration_region,
            teardown_test_setup_migration_region),
        cmocka_unit_test_setup_teardown(test_setup_migration_callbacks_without_migration_region,
            setup_test_setup_migration_region,
            teardown_test_setup_migration_region),
        cmocka_unit_test_setup_teardown(test_setup_migration_callbacks_bad_data_offset,
            setup_test_setup_migration_region,
            teardown_test_setup_migration_region),
        cmocka_unit_test_setup_teardown(test_setup_migration_callbacks,
            setup_test_setup_migration_region,
            teardown_test_setup_migration_region),
        cmocka_unit_test_setup(test_device_is_stopped_and_copying, setup),
        cmocka_unit_test_setup(test_cmd_allowed_when_stopped_and_copying, setup),
        cmocka_unit_test_setup(test_should_exec_command, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
