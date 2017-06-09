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
#include <daos_srv/vos.h>
#include "rpc.h"
#include "rebuild_internal.h"

struct rebuild_obj_arg {
	daos_unit_oid_t	oid;
	uuid_t		pool_uuid;
	uuid_t		cont_uuid;
	unsigned int	map_ver;
	daos_epoch_t	epoch;
	daos_key_t	dkey;
	int		*rebuild_building;
	struct ds_cont  *rebuild_cont;
};

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

/* Get nthream idx from idx */
static inline unsigned int
rebuild_get_nstream_idx(daos_key_t *dkey)
{
	unsigned int nstream;
	uint64_t hash;

	nstream = dss_get_threads_number();

	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				   dkey->iov_len, 5731);
	hash %= nstream;

	return hash;
}

#define MAX_BUF_SIZE 2048
static int
rebuild_fetch_update_inline(struct rebuild_obj_arg *arg, daos_handle_t oh,
			    daos_key_t *akey, unsigned int num,
			    unsigned int type, daos_recx_t *recxs,
			    daos_epoch_range_t *eprs, uuid_t cookie)
{
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	daos_iov_t		iov;
	char			iov_buf[MAX_BUF_SIZE];
	int			rc;

	daos_iov_set(&iov, iov_buf, MAX_BUF_SIZE);
	sgl.sg_nr.num = 1;
	sgl.sg_nr.num_out = 1;
	sgl.sg_iovs = &iov;

	memset(&iod, 0, sizeof(iod));
	memcpy(&iod.iod_name, akey, sizeof(daos_key_t));
	iod.iod_recxs = recxs;
	iod.iod_eprs = eprs;
	iod.iod_nr = num;
	iod.iod_type = type;

	rc = ds_obj_fetch(oh, arg->epoch, &arg->dkey, 1, &iod,
			  &sgl, NULL);
	if (rc)
		return rc;

	rc = vos_obj_update(arg->rebuild_cont->sc_hdl,
			    arg->oid, eprs->epr_lo, cookie, &arg->dkey,
			    1, &iod, &sgl);
	return rc;
}

static int
rebuild_fetch_update_bulk(struct rebuild_obj_arg *arg, daos_handle_t oh,
			  daos_key_t *akey, unsigned int num, unsigned int type,
			  daos_size_t size, daos_recx_t *recxs,
			  daos_epoch_range_t *eprs, uuid_t cookie)
{
	daos_iod_t	iod;
	daos_sg_list_t	*sgl;
	daos_handle_t	ioh;
	int		rc;

	memset(&iod, 0, sizeof(iod));
	memcpy(&iod.iod_name, akey, sizeof(daos_key_t));
	iod.iod_recxs = recxs;
	iod.iod_eprs = eprs;
	iod.iod_nr = num;
	iod.iod_type = type;
	iod.iod_size = size;

	rc = vos_obj_zc_update_begin(arg->rebuild_cont->sc_hdl,
				     arg->oid, eprs->epr_lo, &arg->dkey, 1,
				     &iod, &ioh);
	if (rc != 0) {
		D_ERROR(DF_UOID"preparing update fails: %d\n",
			DP_UOID(arg->oid), rc);
		return rc;
	}

	rc = vos_obj_zc_sgl_at(ioh, 0, &sgl);
	if (rc)
		D_GOTO(end, rc);

	rc = ds_obj_fetch(oh, arg->epoch, &arg->dkey, 1, &iod, sgl, NULL);
	if (rc)
		D_GOTO(end, rc);
end:
	vos_obj_zc_update_end(ioh, cookie, &arg->dkey, 1, &iod, rc);
	return rc;
}

static int
rebuild_fetch_update(struct rebuild_obj_arg *arg, daos_handle_t oh,
		     daos_key_t *akey, unsigned int num, unsigned int type,
		     daos_size_t size, daos_recx_t *recxs,
		     daos_epoch_range_t *eprs, uuid_t cookie)
{
	daos_size_t	buf_size = 0;

	buf_size = size * num;
	if (buf_size < MAX_BUF_SIZE)
		return rebuild_fetch_update_inline(arg, oh, akey, num, type,
						   recxs, eprs, cookie);
	else
		return rebuild_fetch_update_bulk(arg, oh, akey, num, type,
						 size, recxs, eprs, cookie);
}

static int
rebuild_rec(struct rebuild_obj_arg *arg, daos_handle_t oh, daos_key_t *akey,
	    unsigned int num, unsigned int type, daos_size_t size,
	    daos_recx_t *recxs, daos_epoch_range_t *eprs, uuid_t *cookies)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	uuid_t			cookie;
	int			start;
	int			i;
	int			rc = 0;

	start = 0;
	uuid_copy(cookie, cookies[0]);
	for (i = 1; i < num; i++) {
		if (uuid_compare(cookie, cookies[i]) == 0)
			continue;

		rc = rebuild_fetch_update(arg, oh, akey, i - start,
					  type, size, &recxs[start],
					  &eprs[start], cookie);
		if (rc)
			break;
		start = i;
		uuid_copy(cookie, cookies[start]);
	}

	if (i > start && rc == 0)
		rc = rebuild_fetch_update(arg, oh, akey, i - start, type,
					  size, &recxs[start], &eprs[start],
					  cookie);

	D_DEBUG(DB_TRACE, "rebuild "DF_UOID" dkey %.*s akey %.*s rc %d"
		" tag %d\n", DP_UOID(arg->oid), (int)arg->dkey.iov_len,
		(char *)arg->dkey.iov_buf, (int)akey->iov_len,
		(char *)akey->iov_buf, rc, dss_get_module_info()->dmi_tid);

	tls->rebuild_rec_count += num;

	return rc;
}

#define ITER_COUNT	5
static int
rebuild_akey(struct rebuild_obj_arg *arg, daos_handle_t oh, int type,
	     daos_key_t *akey)
{
	daos_recx_t		recxs[ITER_COUNT];
	daos_epoch_range_t	eprs[ITER_COUNT];
	uuid_t			cookies[ITER_COUNT];
	daos_hash_out_t		hash;
	int			rc = 0;

	memset(&hash, 0, sizeof(hash));
	while (!daos_hash_is_eof(&hash)) {
		unsigned int	rec_num = ITER_COUNT;
		daos_size_t	size;
		int		i;

		memset(recxs, 0, sizeof(*recxs) * ITER_COUNT);
		memset(eprs, 0, sizeof(*eprs) * ITER_COUNT);
		memset(cookies, 0, sizeof(*cookies) * ITER_COUNT);
		rc = ds_obj_list_rec(oh, arg->epoch, &arg->dkey, akey,
				     type, &size, &rec_num,
				     recxs, eprs, cookies, &hash, true);
		if (rc)
			break;
		if (rec_num == 0)
			continue;

		/* Temporary fix to satisfied VOS update */
		for (i = 0; i < rec_num; i++)
			eprs[i].epr_hi = DAOS_EPOCH_MAX;

		rc = rebuild_rec(arg, oh, akey, rec_num, type, size, recxs,
				 eprs, cookies);
		if (rc)
			break;
	}
	return rc;
}

static void
rebuild_dkey_thread(void *data)
{
	struct rebuild_obj_arg	*arg = data;
	struct rebuild_tls	*tls = rebuild_tls_get();
	daos_iov_t		akey_iov;
	daos_sg_list_t		akey_sgl;
	daos_size_t		akey_buf_size = 1024;
	daos_hash_out_t		hash;
	unsigned int		i;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	daos_handle_t		oh;
	int			rc;

	D_ALLOC(akey_iov.iov_buf, akey_buf_size);
	if (akey_iov.iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	akey_iov.iov_buf_len = akey_buf_size;
	akey_sgl.sg_nr.num = 1;
	akey_sgl.sg_iovs = &akey_iov;

	if (daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;
		struct pool_map *map;

		map = ds_pool_get_pool_map(tls->rebuild_pool_uuid);
		if (map == NULL)
			D_GOTO(free, rc = -DER_NONEXIST);

		rc = dc_pool_local_open(tls->rebuild_pool_uuid,
					tls->rebuild_pool_hdl_uuid,
					0, NULL, map, tls->rebuild_svc_list,
					&ph);
		pool_map_decref(map);
		if (rc)
			D_GOTO(free, rc);

		tls->rebuild_pool_hdl = ph;
	}

	/* Open client dc handle */
	rc = dc_cont_local_open(arg->cont_uuid, tls->rebuild_cont_hdl_uuid,
				0, tls->rebuild_pool_hdl, &coh);
	if (rc)
		D_GOTO(free, rc);

	rc = ds_obj_open(coh, arg->oid.id_pub, arg->epoch, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(cont_close, rc);

	rc = ds_cont_lookup(arg->pool_uuid, arg->cont_uuid, &arg->rebuild_cont);
	if (rc)
		D_GOTO(obj_close, rc);

	memset(&hash, 0, sizeof(hash));
	while (!daos_hash_is_eof(&hash)) {
		daos_key_desc_t	akey_kds[ITER_COUNT];
		uint32_t	akey_num = ITER_COUNT;
		void		*akey_ptr;

		rc = ds_obj_list_akey(oh, arg->epoch, &arg->dkey, &akey_num,
				      akey_kds, &akey_sgl, &hash);
		if (rc)
			break;

		D_DEBUG(DB_TRACE, ""DF_UOID" list akey %d for dkey %.*s\n",
			DP_UOID(arg->oid), akey_num, (int)arg->dkey.iov_len,
			(char *)arg->dkey.iov_buf);
		if (akey_num == 0)
			continue;

		for (akey_ptr = akey_iov.iov_buf, i = 0; i < akey_num;
		     akey_ptr += akey_kds[i].kd_key_len, i++) {
			daos_key_t akey;

			akey.iov_buf = akey_ptr;
			akey.iov_len = akey_kds[i].kd_key_len;
			akey.iov_buf_len = akey_kds[i].kd_key_len;

			rc = rebuild_akey(arg, oh, DAOS_IOD_ARRAY, &akey);
			if (rc < 0)
				break;

			rc = rebuild_akey(arg, oh, DAOS_IOD_SINGLE, &akey);
			if (rc < 0)
				break;

		}
	}
	
	ds_cont_put(arg->rebuild_cont);
obj_close:
	ds_obj_close(oh);
cont_close:
	dc_cont_local_close(tls->rebuild_pool_hdl, coh);
free:
	if (akey_iov.iov_buf != NULL)
		D_FREE(akey_iov.iov_buf, akey_buf_size);
	if (tls->rebuild_status == 0)
		tls->rebuild_status = rc;
	(*arg->rebuild_building)--;
	D_DEBUG(DB_TRACE, "finish rebuild dkey %.*s tag %d rebuilding %p/%d\n",
		(int)arg->dkey.iov_len, (char *)arg->dkey.iov_buf,
		dss_get_module_info()->dmi_tid, arg->rebuild_building,
		*arg->rebuild_building);
	daos_iov_free(&arg->dkey);
	D_FREE_PTR(arg);
}

static int
rebuild_dkey(daos_unit_oid_t oid, daos_epoch_t epoch,
	     daos_key_t *dkey, struct rebuild_iter_arg *iter_arg)
{
	struct rebuild_obj_arg	*arg;
	struct rebuild_tls	*tls = rebuild_tls_get();
	unsigned int		tgt;
	int			rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -DER_NOMEM;

	rc = daos_iov_copy(&arg->dkey, dkey);
	if (rc)
		D_GOTO(free, rc);

	arg->oid = oid;
	uuid_copy(arg->pool_uuid, iter_arg->pool_uuid);
	uuid_copy(arg->cont_uuid, iter_arg->cont_uuid);
	arg->map_ver = iter_arg->map_ver;
	arg->epoch = epoch;
	tgt = rebuild_get_nstream_idx(dkey);
	arg->rebuild_building = &tls->rebuild_building[tgt];
	tls->rebuild_building[tgt]++;
	D_DEBUG(DB_TRACE, "start rebuild dkey %.*s tag %d rebuilding %p/%d\n",
		(int)arg->dkey.iov_len, (char *)arg->dkey.iov_buf, tgt,
		&tls->rebuild_building[tgt], tls->rebuild_building[tgt]);
	rc = dss_ult_create(rebuild_dkey_thread, arg, tgt, NULL);
	if (rc) {
		tls->rebuild_building[tgt]--;
		D_GOTO(free, rc);
	}

	return rc;
free:
	if (arg != NULL) {
		daos_iov_free(&arg->dkey);
		D_FREE_PTR(arg);
	}

	return rc;
}

/**
 * Iterate akeys/dkeys of the object
 */
static int
rebuild_obj_iterate_keys(daos_unit_oid_t oid, unsigned int shard, void *data)
{
	struct rebuild_iter_arg	*arg = data;
	struct rebuild_tls	*tls = rebuild_tls_get();
	daos_hash_out_t		hash_out;
	daos_handle_t		oh;
	daos_epoch_t		epoch = DAOS_EPOCH_MAX;
	daos_sg_list_t		dkey_sgl;
	daos_iov_t		dkey_iov;
	daos_size_t		dkey_buf_size = 1024;
	int			rc;

	D_ALLOC(dkey_iov.iov_buf, dkey_buf_size);
	if (dkey_iov.iov_buf == NULL)
		return -DER_NOMEM;
	dkey_iov.iov_buf_len = dkey_buf_size;
	dkey_sgl.sg_nr.num = 1;
	dkey_sgl.sg_iovs = &dkey_iov;

	rc = ds_obj_open(arg->cont_hdl, oid.id_pub, epoch, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(free, rc);

	D_DEBUG(DB_TRACE, "start rebuild obj "DF_UOID" for shard %u\n",
		DP_UOID(oid), shard);
	memset(&hash_out, 0, sizeof(hash_out));
	enum_anchor_set_shard(&hash_out, shard);
	while (!daos_hash_is_eof(&hash_out)) {
		daos_key_desc_t	kds[ITER_COUNT];
		uint32_t	num = ITER_COUNT;
		void		*ptr;
		int		i;

		rc = ds_obj_single_shard_list_dkey(oh, epoch, &num, kds,
						   &dkey_sgl, &hash_out);
		if (rc)
			break;
		if (num == 0)
			continue;

		D_DEBUG(DB_TRACE, "rebuild list dkey %d\n", num);
		for (ptr = dkey_iov.iov_buf, i = 0;
		     i < num; ptr += kds[i].kd_key_len, i++) {
			daos_key_t dkey;

			dkey.iov_buf = ptr;
			dkey.iov_len = kds[i].kd_key_len;
			dkey.iov_buf_len = kds[i].kd_key_len;

			rc = rebuild_dkey(oid, epoch, &dkey, arg);
			if (rc < 0)
				break;
		}
	}

	ds_obj_close(oh);
free:
	if (dkey_iov.iov_buf != NULL)
		D_FREE(dkey_iov.iov_buf, dkey_buf_size);
	tls->rebuild_obj_count++;
	D_DEBUG(DB_TRACE, "stop rebuild obj "DF_UOID" for shard %u rc %d\n",
		DP_UOID(oid), shard, rc);

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

	D_DEBUG(DB_TRACE, "obj rebuild "DF_UUID"/%"PRIx64" start\n",
		DP_UUID(arg->cont_uuid), ih.cookie);
	D_ASSERT(arg->obj_cb != NULL);

	rc = arg->obj_cb(*oid, *shard, arg);
	if (rc)
		return rc;

	D_DEBUG(DB_TRACE, "obj rebuild "DF_UUID"/%"PRIx64" end\n",
		DP_UUID(arg->cont_uuid), ih.cookie);

	/* Some one might insert new record to the tree let's reprobe */
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, key_iov, NULL);
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

static int
rebuild_cont_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		     daos_iov_t *val_iov, void *data)
{
	struct rebuild_root	*root = val_iov->iov_buf;
	struct rebuild_iter_arg *arg = data;
	struct rebuild_tls	*tls = rebuild_tls_get();
	daos_handle_t		coh = DAOS_HDL_INVAL;
	int rc;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	D_DEBUG(DB_TRACE, "iter cont "DF_UUID"/%"PRIx64" %"PRIx64" start\n",
		DP_UUID(arg->cont_uuid), ih.cookie, root->root_hdl.cookie);

	/* Create dc_pool locally */
	if (daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;
		struct pool_map *map;

		map = ds_pool_get_pool_map(tls->rebuild_pool_uuid);
		if (map == NULL)
			return -DER_NONEXIST;

		rc = dc_pool_local_open(tls->rebuild_pool_uuid,
					tls->rebuild_pool_hdl_uuid,
					0, NULL, map, tls->rebuild_svc_list,
					&ph);
		pool_map_decref(map);
		if (rc)
			return rc;

		tls->rebuild_pool_hdl = ph;
	}

	rc = dc_cont_local_open(arg->cont_uuid, tls->rebuild_cont_hdl_uuid,
				0, tls->rebuild_pool_hdl, &coh);
	if (rc)
		return rc;
	arg->cont_hdl = coh;

	while (!dbtree_is_empty(root->root_hdl)) {
		rc = dbtree_iterate(root->root_hdl, false,
				    rebuild_obj_iter_cb, arg);
		if (rc) {
			if (tls->rebuild_status == 0)
				tls->rebuild_status = rc;
			D_ERROR("iterate cont "DF_UUID" failed: rc %d\n",
				DP_UUID(arg->cont_uuid), rc);
			break;
		}
	}

	dc_cont_local_close(tls->rebuild_pool_hdl, coh);
	if (rc)
		return rc;

	D_DEBUG(DB_TRACE, "iter cont "DF_UUID"/%"PRIx64" finish.\n",
		DP_UUID(arg->cont_uuid), ih.cookie);

	/* Some one might insert new record to the tree let's reprobe */
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, key_iov, NULL);
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

	while (!dbtree_is_empty(iter_arg->root_hdl)) {
		rc = dbtree_iterate(iter_arg->root_hdl, false,
				    rebuild_cont_iter_cb, iter_arg);
		if (rc) {
			D_ERROR("dbtree iterate fails %d\n", rc);
			if (tls->rebuild_status == 0)
				tls->rebuild_status = rc;
			break;
		}
	}

	tls->rebuild_task_init = 0;
	tls->rebuild_building[0]--;
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
				" %u hdl %"PRIx64"\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i],
				btr_hdl.cookie);
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
		D_ASSERT(tls->rebuild_building != NULL);
		tls->rebuild_building[0]++;
		tls->rebuild_task_init = 1;
		rc = dss_ult_create(rebuild_iterate_object, arg, -1, NULL);
		if (rc) {
			tls->rebuild_task_init = 0;
			tls->rebuild_building[0]--;
			D_FREE_PTR(arg);
		}
	}

out:
	rebuild_out = crt_reply_get(rpc);
	rebuild_out->ro_status = rc;
	return crt_reply_send(rpc);
}
