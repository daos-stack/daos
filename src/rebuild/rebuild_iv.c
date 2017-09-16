/**
 * (C) Copyright 2017 Intel Corporation.
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
#define DDSUBSYS	DDFAC(rebuild)

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

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct rebuild_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = sizeof(struct rebuild_iv);
free:
	if (rc)
		daos_sgl_fini(sgl, true);
	return rc;
}

static int
rebuild_iv_ent_alloc(struct ds_iv_key *iv_key, void *data,
		     d_sg_list_t *sgl)
{
	return rebuild_iv_alloc_internal(sgl);
}

static int
rebuild_iv_ent_get(d_sg_list_t *sgl, struct ds_iv_entry *entry)
{
	if (sgl->sg_iovs != NULL && sgl->sg_iovs[0].iov_buf != NULL)
		return 0;

	return rebuild_iv_alloc_internal(sgl);
}

static int
rebuild_iv_ent_put(d_sg_list_t *sgl, struct ds_iv_entry *entry)
{
	return 0;
}

static int
rebuild_iv_ent_destroy(d_sg_list_t *sgl)
{
	daos_sgl_fini(sgl, true);
	return 0;
}

static int
rebuild_iv_ent_copy(d_sg_list_t *dst, d_sg_list_t *src)
{
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	struct rebuild_iv *dst_iv = dst->sg_iovs[0].iov_buf;

	if (dst_iv == src_iv)
		return 0;

	D__ASSERT(src_iv != NULL);
	D__ASSERT(dst_iv != NULL);
	uuid_copy(dst_iv->riv_poh_uuid, src_iv->riv_poh_uuid);
	uuid_copy(dst_iv->riv_coh_uuid, src_iv->riv_coh_uuid);
	D__DEBUG(DB_TRACE, "coh/poh "DF_UUID"/"DF_UUID"\n",
		 DP_UUID(dst_iv->riv_coh_uuid),
		 DP_UUID(dst_iv->riv_poh_uuid));

	return 0;
}

static int
rebuild_iv_ent_fetch(d_sg_list_t *dst, d_sg_list_t *src)
{
	return rebuild_iv_ent_copy(dst, src);
}

static int
rebuild_iv_ent_update(d_sg_list_t *dst, d_sg_list_t *src)
{
	struct rebuild_iv *src_iv = src->sg_iovs[0].iov_buf;
	struct rebuild_iv *dst_iv = dst->sg_iovs[0].iov_buf;

	if (dst_iv != src_iv) {
		dst_iv->riv_obj_count += src_iv->riv_obj_count;
		dst_iv->riv_rec_count += src_iv->riv_rec_count;
		dst_iv->riv_done += src_iv->riv_done;
		if (dst_iv->riv_status == 0)
			dst_iv->riv_status = src_iv->riv_status;
	}

	rebuild_gst.rg_obj_count += src_iv->riv_obj_count;
	rebuild_gst.rg_rec_count += src_iv->riv_rec_count;
	rebuild_gst.rg_done += src_iv->riv_done;
	if (rebuild_gst.rg_status.rs_errno == 0)
		rebuild_gst.rg_status.rs_errno = src_iv->riv_status;

	D__DEBUG(DB_TRACE, "rebuild_gst rg_done %d ver %d riv_done %d "
		 "rank %d obj "DF_U64" rec "DF_U64" rs_errno %d\n",
		 rebuild_gst.rg_done, rebuild_gst.rg_rebuild_ver,
		 src_iv->riv_done, src_iv->riv_rank, rebuild_gst.rg_obj_count,
		 rebuild_gst.rg_rec_count, rebuild_gst.rg_status.rs_errno);

	return rebuild_iv_ent_copy(dst, src);
}

struct ds_iv_entry_ops rebuild_iv_ops = {
	.iv_ent_alloc	= rebuild_iv_ent_alloc,
	.iv_ent_get	= rebuild_iv_ent_get,
	.iv_ent_put	= rebuild_iv_ent_put,
	.iv_ent_destroy	= rebuild_iv_ent_destroy,
	.iv_ent_fetch	= rebuild_iv_ent_fetch,
	.iv_ent_update	= rebuild_iv_ent_update,
};

int
rebuild_iv_fetch(void *ns, struct rebuild_iv *rebuild_iv)
{
	d_sg_list_t		sgl;
	daos_iov_t		iov;
	int			rc;

	memset(&sgl, 0, sizeof(sgl));
	memset(&iov, 0, sizeof(iov));
	iov.iov_buf = rebuild_iv;
	iov.iov_len = sizeof(*rebuild_iv);
	iov.iov_buf_len = sizeof(*rebuild_iv);
	sgl.sg_nr.num = 1;
	sgl.sg_iovs = &iov;

	rc = ds_iv_fetch(ns, IV_REBUILD, &sgl);
	if (rc)
		D__ERROR("iv fetch failed %d\n", rc);

	return rc;
}

int
rebuild_iv_update(void *ns, struct rebuild_iv *iv,
		  unsigned int shortcut, unsigned int sync_mode)
{
	d_sg_list_t	sgl;
	daos_iov_t	iov;
	int		rc;

	iov.iov_buf = iv;
	iov.iov_len = sizeof(*iv);
	iov.iov_buf_len = sizeof(*iv);
	sgl.sg_nr.num = 1;
	sgl.sg_nr.num_out = 0;
	sgl.sg_iovs = &iov;
	rc = ds_iv_update(ns, IV_REBUILD, &sgl, shortcut, sync_mode);
	if (rc)
		D__ERROR("iv update failed %d\n", rc);

	return rc;
}

/**
 * Note: this handler will only handle the off-line rebuild
 * case. As for on-line rebuild, the iv_ns will be created
 * in ds_pool_connect_handler().
 */
void
rebuild_iv_ns_handler(crt_rpc_t *rpc)
{
	struct rebuild_iv_ns_in		*in;
	struct rebuild_out		*out;
	struct ds_pool_create_arg	arg;
	struct ds_pool			*pool;
	int				rc;

	in = crt_req_get(rpc);
	out = crt_reply_get(rpc);

	memset(&arg, 0, sizeof(arg));
	rc = ds_pool_lookup_create(in->rin_pool_uuid, &arg, &pool);
	if (rc != 0)
		D_GOTO(out, rc);

	if (pool->sp_iv_ns != NULL) {
		/* Destroy the previous IV ns */
		ds_iv_ns_destroy(pool->sp_iv_ns);
		pool->sp_iv_ns = NULL;
	}

	rc = ds_iv_ns_attach(rpc->cr_ctx, in->rin_ns_id,
			     in->rin_master_rank, &in->rin_iov,
			     &pool->sp_iv_ns);
	if (rc != 0) {
		ds_pool_put(pool);
		D_GOTO(out, rc);
	}

	ABT_mutex_lock(rebuild_gst.rg_lock);
	if (rebuild_gst.rg_pool == NULL)
		rebuild_gst.rg_pool = pool; /* pin it */
	ABT_mutex_unlock(rebuild_gst.rg_lock);

out:
	out->ro_status = rc;
	D__DEBUG(DB_TRACE, "rebuild ns create rc = %d\n", rc);
	crt_reply_send(rpc);
}

int
rebuild_iv_ns_create(struct ds_pool *pool, d_rank_list_t *exclude_tgts,
		     unsigned int master_rank)
{
	struct rebuild_iv_ns_in	*in;
	struct rebuild_out	*out;
	unsigned int		iv_ns_id;
	daos_iov_t		iv_iov;
	struct ds_iv_ns		*ns;
	crt_rpc_t		*rpc;
	int			rc;

	rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx,
			     &iv_ns_id, &iv_iov, &ns);
	if (rc) {
		D__ERROR("pool "DF_UUID" iv ns create failed %d\n",
			 DP_UUID(pool->sp_uuid), rc);
		return rc;
	}

	pool->sp_iv_ns = ns;
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_IV_NS_CREATE, &rpc, NULL,
				  exclude_tgts);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	in->rin_iov = iv_iov;
	in->rin_ns_id = iv_ns_id;
	in->rin_master_rank = master_rank;
	uuid_copy(in->rin_pool_uuid, pool->sp_uuid);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->ro_status;
	if (rc != 0)
		D_GOTO(out_rpc, rc);

out_rpc:
	crt_req_decref(rpc);
out:
	if (rc)
		ds_iv_ns_destroy(ns);
	return rc;
}
