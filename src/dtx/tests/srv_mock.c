/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <abt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/container.h>

uint32_t dss_tgt_nr = 4;

static void *
mock_init(int tags, int xs_id, int tgt_id)
{
	assert_true(false);
	return NULL;
}

static void
mock_fini(int tags, void *data)
{
	assert_true(false);
}

struct dss_module_key daos_srv_modkey = {
    .dmk_tags  = DAOS_SERVER_TAG,
    .dmk_index = -1,
    .dmk_init  = mock_init,
    .dmk_fini  = mock_fini,
};

bool
dss_has_enough_helper(void)
{
	assert_true(false);
	return false;
}

int
dss_sleep(uint64_t msec)
{
	assert_true(false);
	return -DER_NOMEM;
}

int
ds_pool_lookup(const uuid_t uuid, struct ds_pool **pool)
{
	assert_true(false);
	return -DER_NOMEM;
}

int
ds_pool_target_status_check(struct ds_pool *pool, uint32_t id, uint8_t matched_status,
			    struct pool_target **p_tgt)
{
	assert_true(false);
	return -DER_NOMEM;
}

void
ds_pool_child_put(struct ds_pool_child *child)
{
	assert_true(false);
}

struct ds_pool_child *
ds_pool_child_lookup(const uuid_t uuid)
{
	assert_true(false);
	return NULL;
}

void
dss_set_start_epoch(void)
{
	assert_true(false);
}

int
ds_cont_iter(daos_handle_t ph, uuid_t co_uuid, cont_iter_cb_t callback, void *arg, uint32_t type,
	     uint32_t flags)
{
	assert_true(false);
	return -DER_NOMEM;
}

void
ds_cont_child_put(struct ds_cont_child *cont)
{
	assert_true(false);
}

bool
dss_xstream_exiting(struct dss_xstream *dxs)
{
	assert_true(false);
	return false;
}

void
ds_cont_child_get(struct ds_cont_child *cont)
{
	assert_true(false);
}

void
ds_pool_put(struct ds_pool *pool)
{
	assert_true(false);
}

int
ds_cont_child_lookup(uuid_t pool_uuid, uuid_t cont_uuid, struct ds_cont_child **ds_cont)
{
	assert_true(false);
	return -DER_NOMEM;
}

d_rank_t
dss_self_rank(void)
{
	assert_true(false);
	return -1;
}
