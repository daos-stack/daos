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
#define DD_SUBSYS	DD_FAC(rebuild)

#include <daos_srv/pool.h>
#include <daos/btree_class.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_server.h>
#include "rpc.h"
#include "rebuild_internal.h"

static int
rebuild_object(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	       daos_key_t *akey, int akey_num)
{
	/* pull date from contributor and update the local object */
	return 0;
}

typedef int (*rebuild_obj_iter_cb_t)(daos_unit_oid_t oid, unsigned int shard,
				     void *arg);
struct rebuild_iter_arg {
	void			*arg;
	uuid_t			pool_uuid;
	uuid_t			cont_uuid;
	rebuild_obj_iter_cb_t	obj_cb;
	unsigned int		map_ver;
	daos_handle_t		root_hdl;
	daos_handle_t		cont_hdl;
};

#define ITER_COUNT	5

/**
 * Iterate akeys/dkeys of the object
 */
static int
rebuild_obj_iterate_keys(daos_unit_oid_t oid, unsigned int shard, void *data)
{
	struct rebuild_iter_arg	*arg = data;
	daos_hash_out_t		hash_out;
	daos_handle_t		oh;
	daos_epoch_t		epoch = 0;
	daos_iov_t		dkey_iov;
	daos_size_t		dkey_buf_size = 1024;
	daos_iov_t		akey_iov;
	daos_size_t		akey_buf_size = 1024;
	int			rc;

	D_ALLOC(dkey_iov.iov_buf, dkey_buf_size);
	if (dkey_iov.iov_buf == NULL)
		return -DER_NOMEM;
	dkey_iov.iov_buf_len = dkey_buf_size;

	D_ALLOC(akey_iov.iov_buf, akey_buf_size);
	if (akey_iov.iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	akey_iov.iov_buf_len = akey_buf_size;

	rc = ds_obj_open(arg->cont_hdl, oid.id_pub, epoch, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(free, rc);

	memset(&hash_out, 0, sizeof(hash_out));
	enum_anchor_set_shard(&hash_out, shard);
	while (!daos_hash_is_eof(&hash_out)) {
		uint32_t	num = ITER_COUNT;
		daos_key_desc_t	kds[ITER_COUNT];
		daos_sg_list_t	sgl;
		daos_hash_out_t akey_hash_out;
		void		*ptr;
		int		i;

		sgl.sg_nr.num = 1;
		sgl.sg_iovs = &dkey_iov;

		rc = ds_obj_single_shard_list_dkey(oh, epoch, &num, kds,
						   &sgl, &hash_out);
		if (rc)
			break;

		if (num == 0)
			continue;

		D_DEBUG(DB_TRACE, "rebuild list dkey %d\n", num);
		memset(&akey_hash_out, 0, sizeof(akey_hash_out));
		enum_anchor_set_shard(&akey_hash_out, shard);
		for (ptr = dkey_iov.iov_buf, i = 0;
		     i < num && !daos_hash_is_eof(&akey_hash_out); i++) {
			daos_key_t dkey;
			daos_key_t akey[ITER_COUNT];
			uint32_t   akey_num = ITER_COUNT;
			daos_key_desc_t akey_kds[ITER_COUNT];
			daos_sg_list_t	akey_sgl;
			void		*akey_ptr;
			int		j;

			dkey.iov_buf = ptr;
			dkey.iov_len = kds[i].kd_key_len;
			dkey.iov_buf_len = kds[i].kd_key_len;

			akey_sgl.sg_nr.num = 1;
			akey_sgl.sg_iovs = &akey_iov;

			rc = ds_obj_list_akey(oh, epoch, &dkey,
					      &akey_num, akey_kds, &akey_sgl,
					      &akey_hash_out);
			if (rc)
				D_GOTO(free, rc);

			if (akey_num == 0)
				continue;

			D_DEBUG(DB_TRACE, "rebuild list akey %d dkey %*.s\n",
				num, (int)dkey.iov_buf_len,
				(char *)dkey.iov_buf);
			memset(akey, 0, sizeof(daos_key_t) * ITER_COUNT);
			for (akey_ptr = akey_iov.iov_buf, j = 0;
			     j < akey_num; j++) {

				akey[j].iov_buf = akey_ptr;
				akey[j].iov_len = akey_kds[j].kd_key_len;
				akey[j].iov_buf_len = akey_kds[j].kd_key_len;
				akey_ptr += akey_kds[j].kd_key_len;
			}
			rc = rebuild_object(oh, epoch, &dkey, akey, akey_num);
			if (rc < 0)
				break;

			ptr += kds[i].kd_key_len;
		}
	}

	ds_obj_close(oh);
free:
	if (dkey_iov.iov_buf != NULL)
		D_FREE(dkey_iov.iov_buf, dkey_buf_size);
	if (akey_iov.iov_buf != NULL)
		D_FREE(akey_iov.iov_buf, akey_buf_size);

	return rc;
}

static int
rebuild_obj_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		    daos_iov_t *val_iov, void *data)
{
	struct rebuild_iter_arg *arg = data;
	daos_unit_oid_t		*oid = key_iov->iov_buf;
	unsigned int		*shard = val_iov->iov_buf;
	int			rc;

	D_ASSERT(arg->obj_cb != NULL);
	rc = arg->obj_cb(*oid, *shard, arg);

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	return rc;
}

static int
rebuild_cont_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		     daos_iov_t *val_iov, void *data)
{
	struct rebuild_root	*root = val_iov->iov_buf;
	struct rebuild_iter_arg *arg = data;
	struct rebuild_tls	*tls = rebuild_tls_get();
	daos_handle_t		coh;
	int rc;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	rc = dc_cont_local_open(arg->cont_uuid, tls->rebuild_cont_hdl_uuid,
				0, tls->rebuild_pool_hdl, &coh);
	if (rc)
		return rc;
	arg->cont_hdl = coh;

	rc = dbtree_iterate(root->root_hdl, false, rebuild_obj_iter_cb, arg);

	dc_cont_local_close(tls->rebuild_pool_hdl, coh);
	if (rc)
		return rc;

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	return rc;
}

static void
rebuild_iterate_object(void *arg)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct rebuild_iter_arg	*iter_arg = arg;
	int			rc;

	rc = dbtree_iterate(iter_arg->root_hdl, false, rebuild_cont_iter_cb,
			    iter_arg);
	if (rc)
		D_ERROR("dbtree iterate fails %d\n", rc);

	tls->rebuild_task_init = 0;
	D_FREE_PTR(iter_arg);
}

static int
ds_rebuild_obj_hdl_get(uuid_t pool_uuid, daos_handle_t *hdl)
{
	struct rebuild_tls	*tls;
	struct btr_root		*btr_root;
	struct umem_attr	uma;
	int rc;

	tls = rebuild_tls_get();
	if (tls->rebuild_local_root_init) {
		*hdl = tls->rebuild_local_root_hdl;
		return 0;
	}

	uuid_copy(tls->rebuild_pool_uuid, pool_uuid);
	btr_root = &tls->rebuild_local_root;
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_NV, 0, 4, &uma,
				   btr_root, &tls->rebuild_local_root_hdl);
	if (rc != 0) {
		D_ERROR("failed to create rebuild tree: %d\n", rc);
		return rc;
	}

	tls->rebuild_local_root_init = 1;
	*hdl = tls->rebuild_local_root_hdl;
	return 0;
}

/* Got the object list from scanner and rebuild the objects */
int
ds_rebuild_obj_handler(crt_rpc_t *rpc)
{
	struct rebuild_tls	*tls;
	struct rebuild_objs_in	*rebuild_in;
	struct rebuild_out	*rebuild_out;
	daos_unit_oid_t		*oids;
	unsigned int		oids_count;
	uuid_t			*co_uuids;
	unsigned int		co_count;
	uint32_t		*shards;
	unsigned int		shards_count;
	daos_handle_t		btr_hdl;
	unsigned int		i;
	int			rc;

	rebuild_in = crt_req_get(rpc);
	oids = rebuild_in->roi_oids.da_arrays;
	oids_count = rebuild_in->roi_oids.da_count;
	co_uuids = rebuild_in->roi_uuids.da_arrays;
	co_count = rebuild_in->roi_uuids.da_count;
	shards = rebuild_in->roi_shards.da_arrays;
	shards_count = rebuild_in->roi_shards.da_count;

	if (co_count == 0 || oids_count == 0 || shards_count == 0 ||
	    oids_count != co_count || oids_count != shards_count) {
		D_ERROR("oids_count %u co_count %u shards_count %u\n",
			oids_count, co_count, shards_count);
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* Initialize the local rebuild tree */
	rc = ds_rebuild_obj_hdl_get(rebuild_in->roi_pool_uuid,
				    &btr_hdl);
	if (rc)
		D_GOTO(out, rc);

	/* Insert these oids/conts into the local rebuild tree */
	for (i = 0; i < oids_count; i++) {
		rc = ds_rebuild_cont_obj_insert(btr_hdl, co_uuids[i],
						oids[i], shards[i]);
		if (rc == 1)
			D_DEBUG(DB_TRACE, "insert local "DF_UOID" "DF_UUID
				" %u\n", DP_UOID(oids[i]), DP_UUID(co_uuids[i]),
				shards[i]);
		else if (rc == 0)
			D_DEBUG(DB_TRACE, ""DF_UOID" "DF_UUID" %u exist.\n",
				DP_UOID(oids[i]), DP_UUID(co_uuids[i]),
				shards[i]);
		else if (rc < 0)
			break;
	}
	if (rc < 0)
		D_GOTO(out, rc);

	/* Check and create task to iterate the local rebuild tree */
	tls = rebuild_tls_get();
	if (!tls->rebuild_task_init) {
		struct rebuild_iter_arg *arg;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		uuid_copy(arg->pool_uuid, rebuild_in->roi_pool_uuid);
		arg->obj_cb = rebuild_obj_iterate_keys;
		arg->root_hdl = btr_hdl;
		arg->map_ver = rebuild_in->roi_map_ver;
		rc = dss_thread_create(rebuild_iterate_object, arg);
		if (rc == 0)
			tls->rebuild_task_init = 1;
		else
			D_FREE_PTR(arg);
	}

out:
	rebuild_out = crt_reply_get(rpc);
	rebuild_out->ro_status = rc;
	return crt_reply_send(rpc);
}
