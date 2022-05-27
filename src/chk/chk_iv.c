/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <gurt/list.h>
#include <gurt/debug.h>
#include <cart/iv.h>
#include <daos_srv/iv.h>
#include <daos_srv/daos_engine.h>

#include "chk_internal.h"

static int
chk_iv_alloc_internal(d_sg_list_t *sgl)
{
	int	rc;

	rc = d_sgl_init(sgl, 1);
	if (rc != 0)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct chk_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL) {
		d_sgl_fini(sgl, true);
		return -DER_NOMEM;
	}

	sgl->sg_iovs[0].iov_buf_len = sizeof(struct chk_iv);
	sgl->sg_iovs[0].iov_len = sizeof(struct chk_iv);

	return 0;
}

static int
chk_iv_ent_init(struct ds_iv_key *iv_key, void *data, struct ds_iv_entry *entry)
{
	int	rc;

	rc = chk_iv_alloc_internal(&entry->iv_value);
	if (rc == 0) {
		entry->iv_key.class_id = iv_key->class_id;
		entry->iv_key.rank = iv_key->rank;
	}

	return rc;
}

static int
chk_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
chk_iv_ent_put(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
chk_iv_ent_destroy(d_sg_list_t *sgl)
{
	d_sgl_fini(sgl, true);

	return 0;
}

static int
chk_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key, d_sg_list_t *dst, void **priv)
{
	D_ASSERT(0);

	return 0;
}

/* Update the chk pool svc lists and status from engine to leader. */
static int
chk_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		  d_sg_list_t *src, void **priv)
{
	struct chk_iv	*dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct chk_iv	*src_iv = src->sg_iovs[0].iov_buf;
	int		 rc = 0;

	/*
	 * When check leader (IV master) tirgger chk_iv_update, it will set ci_to_leader as 0,
	 * then chk_iv_ent_update() for any IV message from leader will get -DER_IVCB_FORWARD.
	 */
	if (!src_iv->ci_to_leader)
		D_GOTO(out, rc = -DER_IVCB_FORWARD);

	if (dst_iv->ci_gen == 0)
		goto update;

	if (unlikely(dst_iv->ci_gen != src_iv->ci_gen)) {
		D_WARN("Receive invalid update IV message: "DF_X64" vs "DF_X64"\n",
		       dst_iv->ci_gen, src_iv->ci_gen);
		goto out;
	}

	/* Old IV update message. */
	if (dst_iv->ci_phase > src_iv->ci_phase)
		goto out;

update:
	dst_iv->ci_gen = src_iv->ci_gen;
	dst_iv->ci_rank = src_iv->ci_rank;
	dst_iv->ci_phase = src_iv->ci_phase;
	dst_iv->ci_status = src_iv->ci_status;

	rc = chk_leader_notify(dst_iv->ci_gen, dst_iv->ci_rank, dst_iv->ci_phase,
			       dst_iv->ci_status);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Handled CHK IV update with gen "DF_X64", rank %u, phase %u, status %d: "DF_RC"\n",
		 src_iv->ci_gen, src_iv->ci_rank, src_iv->ci_phase, src_iv->ci_status, DP_RC(rc));

out:
	return rc;
}

/* Refresh the chk status from leader to engines. */
static int
chk_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		   d_sg_list_t *src, int ref_rc, void **priv)
{
	struct chk_iv	*dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct chk_iv	*src_iv = src->sg_iovs[0].iov_buf;
	int		 rc = 0;

	D_ASSERT(src_iv->ci_to_leader == 0);

	if (dst_iv->ci_gen == 0)
		goto refresh;

	if (unlikely(dst_iv->ci_gen != src_iv->ci_gen)) {
		D_WARN("Receive invalid refresh IV message: "DF_X64" vs "DF_X64"\n",
		       dst_iv->ci_gen, src_iv->ci_gen);
		goto out;
	}

	/* Repeated or old IV refresh message. */
	if (dst_iv->ci_status >= src_iv->ci_status)
		goto out;

refresh:
	dst_iv->ci_gen = src_iv->ci_gen;
	uuid_copy(dst_iv->ci_uuid, src_iv->ci_uuid);
	dst_iv->ci_rank = src_iv->ci_rank;
	dst_iv->ci_phase = src_iv->ci_phase;
	dst_iv->ci_status = src_iv->ci_status;
	dst_iv->ci_remove_pool = src_iv->ci_remove_pool;

	rc = chk_engine_notify(dst_iv->ci_gen, dst_iv->ci_uuid, dst_iv->ci_rank, dst_iv->ci_phase,
			       dst_iv->ci_status, dst_iv->ci_remove_pool ? true : false);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Handled CHK IV refresh with gen "DF_X64", phase %u, status %d: "DF_RC"\n",
		 src_iv->ci_gen, src_iv->ci_phase, src_iv->ci_status, DP_RC(rc));

out:
	return rc;
}

static int
chk_iv_value_alloc(struct ds_iv_entry *entry, struct ds_iv_key *key, d_sg_list_t *sgl)
{
	return chk_iv_alloc_internal(sgl);
}

struct ds_iv_class_ops chk_iv_ops = {
	.ivc_ent_init		= chk_iv_ent_init,
	.ivc_ent_get		= chk_iv_ent_get,
	.ivc_ent_put		= chk_iv_ent_put,
	.ivc_ent_destroy	= chk_iv_ent_destroy,
	.ivc_ent_fetch		= chk_iv_ent_fetch,
	.ivc_ent_update		= chk_iv_ent_update,
	.ivc_ent_refresh	= chk_iv_ent_refresh,
	.ivc_value_alloc	= chk_iv_value_alloc,
};

int
chk_iv_update(void *ns, struct chk_iv *iv, uint32_t shortcut, uint32_t sync_mode, bool retry)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	int			rc;

	iv->ci_rank = dss_self_rank();
	iov.iov_buf = iv;
	iov.iov_len = sizeof(*iv);
	iov.iov_buf_len = sizeof(*iv);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_CHK;
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0, retry);
	if (rc != 0)
		D_ERROR("CHK iv update failed: "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
chk_iv_init(void)
{
	return ds_iv_class_register(IV_CHK, &iv_cache_ops, &chk_iv_ops);
}

int
chk_iv_fini(void)
{
	return ds_iv_class_unregister(IV_CHK);
}
