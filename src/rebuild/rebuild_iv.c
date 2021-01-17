/**
 * (C) Copyright 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * rebuild: rebuild initator
 *
 * This file contains the server API methods and the RPC handlers for rebuild
 * initiator.
 */
#define D_LOGFAC	DD_FAC(rebuild)

#include <daos_srv/pool.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/iv.h>
#include <cart/iv.h>
#include "rpc.h"
#include "rebuild_internal.h"
#include "rpc.h"
#include "rebuild_internal.h"

static int
rebuild_iv_alloc_internal(d_sg_list_t *sgl)
{
	int	rc;

	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct rebuild_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = sizeof(struct rebuild_iv);
free:
	if (rc)
		d_sgl_fini(sgl, true);
	return rc;
}

static int
rebuild_iv_ent_init(struct ds_iv_key *iv_key, void *data,
		    struct ds_iv_entry *entry)
{
	int rc;

	rc = rebuild_iv_alloc_internal(&entry->iv_value);
	if (rc)
		return rc;

	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;
	return 0;
}

static int
rebuild_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
rebuild_iv_ent_put(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
rebuild_iv_ent_destroy(d_sg_list_t *sgl)
{
	d_sgl_fini(sgl, true);
	return 0;
}

static int
rebuild_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		     d_sg_list_t *dst, d_sg_list_t *src, void **priv)
{
	D_ASSERT(0);
	return 0;
}

/* Update the rebuild status from leaves to the master */
static int
rebuild_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		      d_sg_list_t *src, void **priv)
{
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	struct rebuild_iv *dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct rebuild_global_pool_tracker *rgt;
	d_rank_t	  rank;
	int		  rc;

	rc = crt_group_rank(NULL, &rank);
	if (rc)
		return rc;

	D_DEBUG(DB_TRACE, "rank %d master rank %d\n", src_iv->riv_rank,
		src_iv->riv_master_rank);

	if (rank != src_iv->riv_master_rank)
		return -DER_IVCB_FORWARD;

	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);

	/* Gathering the rebuild status here */
	rgt = rebuild_global_pool_tracker_lookup(src_iv->riv_pool_uuid,
						 src_iv->riv_ver);
	if (rgt && rgt->rgt_leader_term == src_iv->riv_leader_term) {
		/* update the rebuild global status */
		if (!src_iv->riv_global_done) {
			rgt->rgt_status.rs_toberb_obj_nr +=
				src_iv->riv_toberb_obj_count;
			rgt->rgt_status.rs_obj_nr += src_iv->riv_obj_count;
			rgt->rgt_status.rs_rec_nr += src_iv->riv_rec_count;
			rgt->rgt_status.rs_size += src_iv->riv_size;
		}

		rebuild_global_status_update(rgt, src_iv);
		if (rgt->rgt_status.rs_errno == 0) {
			rgt->rgt_status.rs_errno = src_iv->riv_status;
			if (src_iv->riv_status != 0)
				rgt->rgt_status.rs_fail_rank = src_iv->riv_rank;
		}
		D_DEBUG(DB_TRACE, "update rebuild "DF_UUID" ver %d "
			"toberb_obj/rb_obj/rec/global done/status/rank "
			DF_U64"/"DF_U64"/"DF_U64"/%d/%d/%d\n",
			DP_UUID(rgt->rgt_pool_uuid), rgt->rgt_rebuild_ver,
			rgt->rgt_status.rs_toberb_obj_nr,
			rgt->rgt_status.rs_obj_nr, rgt->rgt_status.rs_rec_nr,
			rgt->rgt_status.rs_done, rgt->rgt_status.rs_errno,
			src_iv->riv_rank);
	}

	D_DEBUG(DB_TRACE, "pool "DF_UUID" master_rank %d\n",
		 DP_UUID(dst_iv->riv_pool_uuid), dst_iv->riv_master_rank);

	return 0;
}

/* Distribute the rebuild uuid/master rank from master to leaves */
static int
rebuild_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		       d_sg_list_t *src, int ref_rc, void **priv)
{
	struct rebuild_iv *dst_iv = entry->iv_value.sg_iovs[0].iov_buf;
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	int rc = 0;

	uuid_copy(dst_iv->riv_pool_uuid, src_iv->riv_pool_uuid);
	dst_iv->riv_master_rank = src_iv->riv_master_rank;
	dst_iv->riv_global_done = src_iv->riv_global_done;
	dst_iv->riv_global_scan_done = src_iv->riv_global_scan_done;
	dst_iv->riv_stable_epoch = src_iv->riv_stable_epoch;

	if (dst_iv->riv_global_done || dst_iv->riv_global_scan_done ||
	    dst_iv->riv_stable_epoch) {
		struct rebuild_tgt_pool_tracker *rpt;
		d_rank_t	rank;

		rpt = rpt_lookup(src_iv->riv_pool_uuid, src_iv->riv_ver);
		if (rpt == NULL)
			return 0;

		if (rpt->rt_leader_term != src_iv->riv_leader_term) {
			rpt_put(rpt);
			return 0;
		}

		D_DEBUG(DB_REBUILD, DF_UUID" rebuild status gsd/gd %d/%d"
			" stable eph "DF_U64"\n",
			 DP_UUID(src_iv->riv_pool_uuid),
			 dst_iv->riv_global_scan_done,
			 dst_iv->riv_global_done, dst_iv->riv_stable_epoch);

		if (rpt->rt_stable_epoch == 0)
			rpt->rt_stable_epoch = dst_iv->riv_stable_epoch;
		else if (rpt->rt_stable_epoch != dst_iv->riv_stable_epoch)
			D_WARN("leader change stable epoch from "DF_U64" to "
			       DF_U64 "\n", rpt->rt_stable_epoch,
			       dst_iv->riv_stable_epoch);

		/* on svc nodes update the rebuild status completed list
		 * to serve rebuild status querying in case of master
		 * node changed.
		 */
		rc = crt_group_rank(NULL, &rank);
		if (dst_iv->riv_global_done && rc == 0) {
			struct daos_rebuild_status rs = { 0 };
			daos_prop_t	*prop = NULL;
			struct daos_prop_entry *prop_entry;
			d_rank_list_t	*svc_list;

			D_ALLOC_PTR(prop);
			if (prop == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			rc = ds_pool_iv_prop_fetch(rpt->rt_pool, prop);
			if (rc)
				D_GOTO(free, rc);

			prop_entry = daos_prop_entry_get(prop,
						    DAOS_PROP_PO_SVC_LIST);
			D_ASSERT(prop_entry != NULL);
			svc_list = prop_entry->dpe_val_ptr;
			if (d_rank_in_rank_list(svc_list, rank)) {
				rs.rs_version	= src_iv->riv_ver;
				rs.rs_errno	= src_iv->riv_status;
				rs.rs_done	= 1;
				rs.rs_obj_nr	= src_iv->riv_obj_count;
				rs.rs_rec_nr	= src_iv->riv_rec_count;
				rs.rs_toberb_obj_nr	=
					src_iv->riv_toberb_obj_count;
				rs.rs_size	= src_iv->riv_size;
				rs.rs_seconds   = src_iv->riv_seconds;
				rc = rebuild_status_completed_update(
						src_iv->riv_pool_uuid, &rs);
				if (rc != 0)
					D_ERROR("_status_completed_update, "
						DF_UUID" failed, rc %d.\n",
						DP_UUID(src_iv->riv_pool_uuid),
						rc);
			}
free:
			if (prop)
				daos_prop_free(prop);
		}
out:
		rpt->rt_global_done = dst_iv->riv_global_done;
		rpt->rt_global_scan_done = dst_iv->riv_global_scan_done;
		rpt_put(rpt);
	}

	return rc;
}

static int
rebuild_iv_alloc(struct ds_iv_entry *entry, d_sg_list_t *sgl)
{
	return rebuild_iv_alloc_internal(sgl);
}

struct ds_iv_class_ops rebuild_iv_ops = {
	.ivc_ent_init		= rebuild_iv_ent_init,
	.ivc_ent_get		= rebuild_iv_ent_get,
	.ivc_ent_put		= rebuild_iv_ent_put,
	.ivc_ent_destroy	= rebuild_iv_ent_destroy,
	.ivc_ent_fetch		= rebuild_iv_ent_fetch,
	.ivc_ent_update		= rebuild_iv_ent_update,
	.ivc_ent_refresh	= rebuild_iv_ent_refresh,
	.ivc_value_alloc	= rebuild_iv_alloc,
};

int
rebuild_iv_fetch(void *ns, struct rebuild_iv *rebuild_iv)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	int			rc;

	iov.iov_buf = rebuild_iv;
	iov.iov_len = sizeof(*rebuild_iv);
	iov.iov_buf_len = sizeof(*rebuild_iv);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_REBUILD;
	rc = ds_iv_fetch(ns, &key, &sgl, true /* retry */);
	if (rc)
		D_ERROR("iv fetch failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
rebuild_iv_update(void *ns, struct rebuild_iv *iv, unsigned int shortcut,
		  unsigned int sync_mode, bool retry)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	int			rc;

	iov.iov_buf = iv;
	iov.iov_len = sizeof(*iv);
	iov.iov_buf_len = sizeof(*iv);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_REBUILD;
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0, retry);
	if (rc)
		D_ERROR("iv update failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
rebuild_iv_init(void)
{
	return ds_iv_class_register(IV_REBUILD, &iv_cache_ops, &rebuild_iv_ops);
}

int
rebuild_iv_fini(void)
{
	return ds_iv_class_unregister(IV_REBUILD);
}
