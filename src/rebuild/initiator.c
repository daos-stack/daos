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

typedef int (*rebuild_obj_iter_cb_t)(daos_unit_oid_t oid, unsigned int shard,
				     void *arg);
struct rebuild_iter_arg {
	void			*arg;
	uuid_t			pool_uuid;
	uuid_t			cont_uuid;
	struct rebuild_tgt_pool_tracker *rpt;
	rebuild_obj_iter_cb_t	obj_cb;
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
rebuild_fetch_update_inline(struct rebuild_dkey *rdkey, daos_handle_t oh,
			    daos_key_t *akey, unsigned int num,
			    unsigned int type, daos_recx_t *recxs,
			    daos_epoch_range_t *eprs, uuid_t cookie,
			    uint32_t version, struct ds_cont *ds_cont)
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

	rc = ds_obj_fetch(oh, rdkey->rd_epoch, &rdkey->rd_dkey, 1, &iod,
			  &sgl, NULL);
	if (rc)
		return rc;

	rc = vos_obj_update(ds_cont->sc_hdl, rdkey->rd_oid, eprs->epr_lo,
			    cookie, version, &rdkey->rd_dkey, 1, &iod, &sgl);
	return rc;
}

static int
rebuild_fetch_update_bulk(struct rebuild_dkey *rdkey, daos_handle_t oh,
			  daos_key_t *akey, unsigned int num, unsigned int type,
			  daos_size_t size, daos_recx_t *recxs,
			  daos_epoch_range_t *eprs, uuid_t cookie,
			  uint32_t version, struct ds_cont *ds_cont)
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

	rc = vos_obj_zc_update_begin(ds_cont->sc_hdl, rdkey->rd_oid,
				     eprs->epr_lo, &rdkey->rd_dkey, 1,
				     &iod, &ioh);
	if (rc != 0) {
		D__ERROR(DF_UOID"preparing update fails: %d\n",
			DP_UOID(rdkey->rd_oid), rc);
		return rc;
	}

	rc = vos_obj_zc_sgl_at(ioh, 0, &sgl);
	if (rc)
		D__GOTO(end, rc);

	rc = ds_obj_fetch(oh, rdkey->rd_epoch, &rdkey->rd_dkey, 1, &iod,
			  sgl, NULL);
	if (rc)
		D__GOTO(end, rc);
end:
	vos_obj_zc_update_end(ioh, cookie, version,
			      &rdkey->rd_dkey, 1, &iod, rc);
	return rc;
}

static int
rebuild_fetch_update(struct rebuild_dkey *rdkey, daos_handle_t oh,
		     daos_key_t *akey, unsigned int num, unsigned int type,
		     daos_size_t size, daos_recx_t *recxs,
		     daos_epoch_range_t *eprs, uuid_t cookie, uint32_t version,
		     struct ds_cont *ds_cont)
{
	daos_size_t	buf_size = 0;

	buf_size = size * num;
	if (buf_size < MAX_BUF_SIZE)
		return rebuild_fetch_update_inline(rdkey, oh, akey, num, type,
						   recxs, eprs, cookie, version,
						   ds_cont);
	else
		return rebuild_fetch_update_bulk(rdkey, oh, akey, num, type,
						 size, recxs, eprs, cookie,
						 version, ds_cont);
}

static int
rebuild_rec(struct rebuild_tgt_pool_tracker *rpt, struct ds_cont *ds_cont,
	    daos_handle_t oh, struct rebuild_dkey *rdkey, daos_key_t *akey,
	    unsigned int num, unsigned int type, daos_size_t size,
	    daos_recx_t *recxs, daos_epoch_range_t *eprs, uuid_t *cookies,
	    uint32_t *versions)
{
	struct rebuild_pool_tls	*tls;
	uuid_t			cookie;
	int			start;
	int			i;
	int			rc = 0;
	int			cnt = 0;
	int			total = 0;
	int			version = 0;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	start = 0;
	for (i = 0; i < num; i++) {
		/* check if the record needs to be rebuilt.*/
		if (versions[i] >= rpt->rt_rebuild_ver) {
			D__DEBUG(DB_TRACE, "i %d rec ver %d rebuild ver %d "
				"does not needs to be rebuilt\n",
				i, versions[i], rpt->rt_rebuild_ver);
			if (cnt == 0)
				goto next;
		} else {
			cnt++;
		}

		if (i == 0) {
			uuid_copy(cookie, cookies[0]);
			version = versions[0];
			if (type != DAOS_IOD_SINGLE)
				continue;
		} else {
			/* Sigh vos_obj_update only suppport single
			 * cookie & version.
			 */
			if (uuid_compare(cookie, cookies[i]) == 0 &&
			    version == versions[i] &&
			    type != DAOS_IOD_SINGLE)
				continue;
		}

		rc = rebuild_fetch_update(rdkey, oh, akey, cnt,
					  type, size, &recxs[start],
					  &eprs[start], cookie, version,
					  ds_cont);
		if (rc)
			break;
		total += cnt;
		cnt = 0;
next:
		start = i;
		uuid_copy(cookie, cookies[start]);
	}

	if (cnt > 0 && rc == 0) {
		rc = rebuild_fetch_update(rdkey, oh, akey, cnt, type,
					  size, &recxs[start], &eprs[start],
					  cookie, version, ds_cont);
		if (rc == 0)
			total += cnt;
	}

	D__DEBUG(DB_TRACE, "rebuild "DF_UOID" ver %d dkey %.*s akey %.*s rc %d"
		" total %d tag %d\n", DP_UOID(rdkey->rd_oid),
		rpt->rt_rebuild_ver, (int)rdkey->rd_dkey.iov_len,
		(char *)rdkey->rd_dkey.iov_buf, (int)akey->iov_len,
		(char *)akey->iov_buf, rc, total,
		dss_get_module_info()->dmi_tid);

	tls->rebuild_pool_rec_count += total;

	return rc;
}

#define ITER_COUNT	5
static int
rebuild_akey(struct rebuild_tgt_pool_tracker *rpt, struct ds_cont *ds_cont,
	     daos_handle_t oh, int type, struct rebuild_dkey *rdkey,
	     daos_key_t *akey)
{
	struct rebuild_pool_tls	*tls;
	daos_recx_t		recxs[ITER_COUNT];
	daos_epoch_range_t	eprs[ITER_COUNT];
	uuid_t			cookies[ITER_COUNT];
	uint32_t		versions[ITER_COUNT];
	daos_hash_out_t		hash;
	int			rc = 0;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	memset(&hash, 0, sizeof(hash));
	while (!daos_hash_is_eof(&hash)) {
		struct pool_map	*map;
		unsigned int	rec_num = ITER_COUNT;
		daos_size_t	size;
		int		i;

		map = rebuild_pool_map_get(rpt->rt_pool);
		dc_pool_update_map(tls->rebuild_pool_hdl, map);
		rebuild_pool_map_put(map);

		memset(recxs, 0, sizeof(*recxs) * ITER_COUNT);
		memset(eprs, 0, sizeof(*eprs) * ITER_COUNT);
		memset(cookies, 0, sizeof(*cookies) * ITER_COUNT);
		memset(versions, 0, sizeof(*versions) * ITER_COUNT);
		rc = ds_obj_list_rec(oh, rdkey->rd_epoch, &rdkey->rd_dkey, akey,
				     type, &size, &rec_num, recxs, eprs,
				     cookies, versions, &hash, true);
		if (rc)
			break;
		if (rec_num == 0)
			continue;

		/* Temporary fix to satisfied VOS update */
		for (i = 0; i < rec_num; i++)
			eprs[i].epr_hi = DAOS_EPOCH_MAX;

		rc = rebuild_rec(rpt, ds_cont, oh, rdkey, akey, rec_num, type,
				 size, recxs, eprs, cookies, versions);
		if (rc)
			break;
	}
	return rc;
}

static int
rebuild_one_dkey(struct rebuild_tgt_pool_tracker *rpt,
		 struct rebuild_dkey *rdkey)
{
	struct rebuild_pool_tls	*tls;
	daos_iov_t		akey_iov;
	daos_sg_list_t		akey_sgl;
	daos_size_t		akey_buf_size = 1024;
	struct ds_cont		*rebuild_cont;
	daos_hash_out_t		hash;
	unsigned int		i;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	daos_handle_t		oh;
	int			rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	D__ALLOC(akey_iov.iov_buf, akey_buf_size);
	if (akey_iov.iov_buf == NULL)
		D__GOTO(free, rc = -DER_NOMEM);

	akey_iov.iov_buf_len = akey_buf_size;
	akey_sgl.sg_nr.num = 1;
	akey_sgl.sg_iovs = &akey_iov;

	if (daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;
		struct pool_map *map = rebuild_pool_map_get(rpt->rt_pool);

		rc = dc_pool_local_open(rpt->rt_pool_uuid, rpt->rt_poh_uuid,
					0, NULL, map, rpt->rt_svc_list, &ph);
		rebuild_pool_map_put(map);
		if (rc)
			D__GOTO(free, rc);

		tls->rebuild_pool_hdl = ph;
	}

	/* Open client dc handle */
	rc = dc_cont_local_open(rdkey->rd_cont_uuid, rpt->rt_coh_uuid,
				0, tls->rebuild_pool_hdl, &coh);
	if (rc)
		D__GOTO(free, rc);

	rc = ds_obj_open(coh, rdkey->rd_oid.id_pub, rdkey->rd_epoch, DAOS_OO_RW,
			 &oh);
	if (rc)
		D__GOTO(cont_close, rc);

	rc = ds_cont_lookup(rpt->rt_pool_uuid, rdkey->rd_cont_uuid,
			    &rebuild_cont);
	if (rc)
		D__GOTO(obj_close, rc);

	memset(&hash, 0, sizeof(hash));
	while (!daos_hash_is_eof(&hash)) {
		daos_key_desc_t	akey_kds[ITER_COUNT];
		uint32_t	akey_num = ITER_COUNT;
		void		*akey_ptr;

		rc = ds_obj_list_akey(oh, rdkey->rd_epoch, &rdkey->rd_dkey,
				      &akey_num, akey_kds, &akey_sgl, &hash);
		if (rc)
			break;

		D__DEBUG(DB_TRACE, ""DF_UOID" list akey %d for dkey %.*s\n",
			DP_UOID(rdkey->rd_oid), akey_num,
			(int)rdkey->rd_dkey.iov_len,
			(char *)rdkey->rd_dkey.iov_buf);
		if (akey_num == 0)
			continue;

		for (akey_ptr = akey_iov.iov_buf, i = 0; i < akey_num;
		     akey_ptr += akey_kds[i].kd_key_len, i++) {
			daos_key_t akey;

			akey.iov_buf = akey_ptr;
			akey.iov_len = akey_kds[i].kd_key_len;
			akey.iov_buf_len = akey_kds[i].kd_key_len;

			rc = rebuild_akey(rpt, rebuild_cont, oh, DAOS_IOD_ARRAY,
					  rdkey, &akey);
			if (rc < 0)
				break;

			rc = rebuild_akey(rpt, rebuild_cont, oh,
					  DAOS_IOD_SINGLE, rdkey, &akey);
			if (rc < 0)
				break;
		}
	}

	ds_cont_put(rebuild_cont);
obj_close:
	ds_obj_close(oh);
cont_close:
	dc_cont_local_close(tls->rebuild_pool_hdl, coh);
free:
	if (akey_iov.iov_buf != NULL)
		D__FREE(akey_iov.iov_buf, akey_buf_size);

	return rc;
}

static void
rebuild_dkey_ult(void *arg)
{
	struct rebuild_pool_tls		*tls;
	struct rebuild_tgt_pool_tracker *rpt = arg;
	struct rebuild_puller		*puller;
	unsigned int			idx;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D__ASSERT(tls != NULL);
	D__ASSERT(rpt->rt_pullers != NULL);
	idx = dss_get_module_info()->dmi_tid;
	puller = &rpt->rt_pullers[idx];
	puller->rp_ult_running = 1;
	while (1) {
		struct rebuild_dkey	*rdkey;
		struct rebuild_dkey	*tmp;
		daos_list_t		dkey_list;
		int			rc;

		DAOS_INIT_LIST_HEAD(&dkey_list);
		ABT_mutex_lock(puller->rp_lock);
		daos_list_for_each_entry_safe(rdkey, tmp, &puller->rp_dkey_list,
					      rd_list) {
			daos_list_move(&rdkey->rd_list, &dkey_list);
			puller->rp_inflight++;

		}
		ABT_mutex_unlock(puller->rp_lock);

		daos_list_for_each_entry_safe(rdkey, tmp, &dkey_list, rd_list) {
			daos_list_del(&rdkey->rd_list);
			rc = rebuild_one_dkey(rpt, rdkey);
			D__DEBUG(DB_TRACE, DF_UOID" rebuild dkey %.*s rc = %d"
				" tag %d\n", DP_UOID(rdkey->rd_oid),
				(int)rdkey->rd_dkey.iov_len,
				(char *)rdkey->rd_dkey.iov_buf, rc, idx);

			D__ASSERT(puller->rp_inflight > 0);
			puller->rp_inflight--;

			/* Ignore nonexistent error because puller could race
			 * with user's container destroy:
			 * - puller got the container+oid from a remote scanner
			 * - user destroyed the container
			 * - puller try to open container or pulling data
			 *   (nonexistent)
			 * This is just a workaround...
			 */
			if (tls->rebuild_pool_status == 0 &&
			    rc != -DER_NONEXIST)
				tls->rebuild_pool_status = rc;

			/* XXX If rebuild fails, Should we add this back to
			 * dkey list
			 */
			daos_iov_free(&rdkey->rd_dkey);
			D__FREE_PTR(rdkey);
		}

		/* check if it should exist */
		ABT_mutex_lock(puller->rp_lock);
		if (daos_list_empty(&puller->rp_dkey_list) &&
		    rpt->rt_finishing) {
			ABT_mutex_unlock(puller->rp_lock);
			break;
		}

		if (rpt->rt_abort) {
			ABT_mutex_unlock(puller->rp_lock);
			break;
		}

		/* XXX exist if rebuild is aborted */
		ABT_mutex_unlock(puller->rp_lock);
		ABT_thread_yield();
	}

	ABT_mutex_lock(puller->rp_lock);
	ABT_cond_signal(puller->rp_fini_cond);
	puller->rp_ult_running = 0;
	ABT_mutex_unlock(puller->rp_lock);
}

/**
 * queue dkey to the rebuild dkey list on each xstream.
 */
static int
rebuild_dkey_queue(daos_unit_oid_t oid, daos_epoch_t epoch,
		   daos_key_t *dkey, struct rebuild_iter_arg *iter_arg)
{
	struct rebuild_puller		*puller;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	struct rebuild_dkey		*rdkey = NULL;
	unsigned int			idx;
	int				rc;

	idx = rebuild_get_nstream_idx(dkey);
	puller = &rpt->rt_pullers[idx];
	if (puller->rp_ult == NULL) {
		/* Create puller ULT thread, and destroy ULT until
		 * rebuild finish in rebuild_fini().
		 */
		D__ASSERT(puller->rp_ult_running == 0);
		D__DEBUG(DB_TRACE, "create rebuild dkey ult %d\n", idx);
		rc = dss_ult_create(rebuild_dkey_ult, rpt, idx,
				    &puller->rp_ult);
		if (rc)
			D__GOTO(free, rc);
	}

	D__ALLOC_PTR(rdkey);
	if (rdkey == NULL)
		D__GOTO(free, rc = -DER_NOMEM);

	DAOS_INIT_LIST_HEAD(&rdkey->rd_list);
	rc = daos_iov_copy(&rdkey->rd_dkey, dkey);
	if (rc != 0)
		D__GOTO(free, rc);

	rdkey->rd_oid = oid;
	uuid_copy(rdkey->rd_cont_uuid, iter_arg->cont_uuid);
	rdkey->rd_epoch = epoch;

	ABT_mutex_lock(puller->rp_lock);
	daos_list_add_tail(&rdkey->rd_list, &puller->rp_dkey_list);
	ABT_mutex_unlock(puller->rp_lock);
free:
	if (rc != 0 && rdkey != NULL) {
		daos_iov_free(&rdkey->rd_dkey);
		D__FREE_PTR(rdkey);
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
	struct rebuild_pool_tls	*tls;
	daos_hash_out_t		hash_out;
	daos_handle_t		oh;
	daos_epoch_t		epoch = DAOS_EPOCH_MAX;
	daos_sg_list_t		dkey_sgl = { 0 };
	daos_iov_t		dkey_iov;
	daos_size_t		dkey_buf_size = 1024;
	int			rc;

	tls = rebuild_pool_tls_lookup(arg->rpt->rt_pool_uuid,
				      arg->rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	if (tls->rebuild_pool_status) {
		D__DEBUG(DB_TRACE, "rebuild status %d\n",
			 tls->rebuild_pool_status);
		return 1;
	}

	D__ALLOC(dkey_iov.iov_buf, dkey_buf_size);
	if (dkey_iov.iov_buf == NULL)
		return -DER_NOMEM;
	dkey_iov.iov_buf_len = dkey_buf_size;
	dkey_sgl.sg_nr.num = 1;
	dkey_sgl.sg_iovs = &dkey_iov;

	rc = ds_obj_open(arg->cont_hdl, oid.id_pub, epoch, DAOS_OO_RW, &oh);
	if (rc)
		D__GOTO(free, rc);

	D__DEBUG(DB_TRACE, "start rebuild obj "DF_UOID" for shard %u\n",
		DP_UOID(oid), shard);
	memset(&hash_out, 0, sizeof(hash_out));
	dc_obj_shard2anchor(&hash_out, shard);

	tls->rebuild_pool_obj_count++;
	while (!daos_hash_is_eof(&hash_out)) {
		daos_key_desc_t	kds[ITER_COUNT];
		uint32_t	num = ITER_COUNT;
		void		*ptr;
		int		i;

		rc = ds_obj_single_shard_list_dkey(oh, epoch, &num, kds,
						   &dkey_sgl, &hash_out);
		if (rc) {
			/* container might have been destroyed. Or there is
			 * no spare target left for this object see
			 * obj_grp_valid_shard_get()
			 */
			rc = (rc == -DER_NONEXIST) ? 0 : rc;
			break;
		}
		if (num == 0)
			continue;

		D__DEBUG(DB_TRACE, "rebuild list dkey %d\n", num);
		for (ptr = dkey_iov.iov_buf, i = 0;
		     i < num; ptr += kds[i].kd_key_len, i++) {
			daos_key_t dkey;

			dkey.iov_buf = ptr;
			dkey.iov_len = kds[i].kd_key_len;
			dkey.iov_buf_len = kds[i].kd_key_len;

			rc = rebuild_dkey_queue(oid, epoch, &dkey, arg);
			if (rc < 0)
				break;
		}
	}

	ds_obj_close(oh);
free:
	if (dkey_iov.iov_buf != NULL)
		D__FREE(dkey_iov.iov_buf, dkey_buf_size);
	D__DEBUG(DB_TRACE, "stop rebuild obj "DF_UOID" for shard %u rc %d\n",
		DP_UOID(oid), shard, rc);

	return rc;
}

static int
rebuild_obj_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		    daos_iov_t *val_iov, void *data)
{
	struct rebuild_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	daos_unit_oid_t			*oid = key_iov->iov_buf;
	unsigned int			*shard = val_iov->iov_buf;
	int				rc;

	D__DEBUG(DB_TRACE, "obj rebuild "DF_UUID"/"DF_UOID" %"PRIx64" start\n",
		 DP_UUID(arg->cont_uuid), DP_UOID(*oid), ih.cookie);
	D__ASSERT(arg->obj_cb != NULL);

	rc = arg->obj_cb(*oid, *shard, arg);
	if (rc) {
		D__DEBUG(DB_TRACE, "obj "DF_UOID" cb callback rc %d\n",
			 DP_UOID(*oid), rc);
		return rc;
	}

	D__DEBUG(DB_TRACE, "obj rebuild "DF_UUID"/%"PRIx64" end\n",
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

	if (rpt->rt_abort)
		return 1;

	return rc;
}

static int
rebuild_cont_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		     daos_iov_t *val_iov, void *data)
{
	struct rebuild_root		*root = val_iov->iov_buf;
	struct rebuild_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker	*rpt = arg->rpt;
	struct rebuild_pool_tls		*tls;
	daos_handle_t			coh = DAOS_HDL_INVAL;
	int rc;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	D__DEBUG(DB_TRACE, "iter cont "DF_UUID"/%"PRIx64" %"PRIx64" start\n",
		DP_UUID(arg->cont_uuid), ih.cookie, root->root_hdl.cookie);

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	/* Create dc_pool locally */
	if (daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;
		struct pool_map *map = rebuild_pool_map_get(rpt->rt_pool);

		rc = dc_pool_local_open(rpt->rt_pool_uuid, rpt->rt_poh_uuid,
					0, NULL, map, rpt->rt_svc_list, &ph);
		rebuild_pool_map_put(map);
		if (rc)
			return rc;

		tls->rebuild_pool_hdl = ph;
	}

	rc = dc_cont_local_open(arg->cont_uuid, rpt->rt_coh_uuid,
				0, tls->rebuild_pool_hdl, &coh);
	if (rc)
		return rc;
	arg->cont_hdl = coh;

	if (!dbtree_is_empty(root->root_hdl)) {
		rc = dbtree_iterate(root->root_hdl, false,
				    rebuild_obj_iter_cb, arg);
		if (rc) {
			if (tls->rebuild_pool_status == 0)
				tls->rebuild_pool_status = rc;
			D__ERROR("iterate cont "DF_UUID" failed: rc %d\n",
				DP_UUID(arg->cont_uuid), rc);
		}
	}

	dc_cont_local_close(tls->rebuild_pool_hdl, coh);
	if (rc)
		return rc;

	D__DEBUG(DB_TRACE, "iter cont "DF_UUID"/%"PRIx64" finish.\n",
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
rebuild_puller(void *arg)
{
	struct rebuild_pool_tls		*tls;
	struct rebuild_iter_arg		*iter_arg = arg;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	int				rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	if (!dbtree_is_empty(iter_arg->root_hdl)) {
		rc = dbtree_iterate(iter_arg->root_hdl, false,
				    rebuild_cont_iter_cb, iter_arg);
		if (rc) {
			D__ERROR("dbtree iterate fails %d\n", rc);
			if (tls->rebuild_pool_status == 0)
				tls->rebuild_pool_status = rc;
		}
	}

	D__FREE_PTR(iter_arg);
	rpt->rt_lead_puller_running = 0;
}


static int
rebuild_obj_hdl_get(struct rebuild_tgt_pool_tracker *rpt, daos_handle_t *hdl)
{
	struct umem_attr	uma;
	int rc;

	if (!daos_handle_is_inval(rpt->rt_local_root_hdl)) {
		*hdl = rpt->rt_local_root_hdl;
		return 0;
	}

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_NV, 0, 4, &uma,
				   &rpt->rt_local_root,
				   &rpt->rt_local_root_hdl);
	if (rc != 0) {
		D__ERROR("failed to create rebuild tree: %d\n", rc);
		return rc;
	}

	*hdl = rpt->rt_local_root_hdl;
	return 0;
}

/* Got the object list from scanner and rebuild the objects */
void
rebuild_obj_handler(crt_rpc_t *rpc)
{
	struct rebuild_objs_in		*rebuild_in;
	struct rebuild_tgt_pool_tracker *rpt = NULL;
	struct rebuild_out		*rebuild_out;
	daos_unit_oid_t			*oids;
	unsigned int			oids_count;
	uuid_t				*co_uuids;
	unsigned int			co_count;
	uint32_t			*shards;
	unsigned int			shards_count;
	daos_handle_t			btr_hdl;
	unsigned int			i;
	int				rc;

	rebuild_in = crt_req_get(rpc);
	oids = rebuild_in->roi_oids.da_arrays;
	oids_count = rebuild_in->roi_oids.da_count;
	co_uuids = rebuild_in->roi_uuids.da_arrays;
	co_count = rebuild_in->roi_uuids.da_count;
	shards = rebuild_in->roi_shards.da_arrays;
	shards_count = rebuild_in->roi_shards.da_count;

	if (co_count == 0 || oids_count == 0 || shards_count == 0 ||
	    oids_count != co_count || oids_count != shards_count) {
		D__ERROR("oids_count %u co_count %u shards_count %u\n",
			oids_count, co_count, shards_count);
		D__GOTO(out, rc = -DER_INVAL);
	}

	/* If rpt is NULL, it means the target is not prepared for
	 * rebuilding yet, i.e. it did not receive scan req to
	 * prepare rebuild yet (see rebuild_tgt_prepare()).
	 */
	rpt = rebuild_tgt_pool_tracker_lookup(rebuild_in->roi_pool_uuid,
					      rebuild_in->roi_rebuild_ver);
	if (rpt == NULL || rpt->rt_pool == NULL)
		D__GOTO(out, rc = -DER_AGAIN);

	/* Initialize the local rebuild tree */
	rc = rebuild_obj_hdl_get(rpt, &btr_hdl);
	if (rc)
		D__GOTO(out, rc);

	/* Insert these oids/conts into the local rebuild tree */
	for (i = 0; i < oids_count; i++) {
		rc = rebuild_cont_obj_insert(btr_hdl, co_uuids[i],
					     oids[i], shards[i]);
		if (rc == 1) {
			D__DEBUG(DB_TRACE, "insert local "DF_UOID" "DF_UUID
				" %u hdl %"PRIx64"\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i],
				btr_hdl.cookie);
		} else if (rc == 0) {
			struct rebuild_pool_tls *tls;

			tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
						      rpt->rt_rebuild_ver);
			D_ASSERT(tls != NULL);

			tls->rebuild_pool_obj_count++;
			D__DEBUG(DB_TRACE, ""DF_UOID" "DF_UUID" %u exist.\n",
				DP_UOID(oids[i]), DP_UUID(co_uuids[i]),
				shards[i]);
		} else if (rc < 0) {
			break;
		}
	}
	if (rc < 0)
		D__GOTO(out, rc);

	/* Check and create task to iterate the local rebuild tree */
	if (!rpt->rt_lead_puller_running) {
		struct rebuild_iter_arg *arg;

		D__ALLOC_PTR(arg);
		if (arg == NULL) {
			D__GOTO(out, rc = -DER_NOMEM);
		}
		uuid_copy(arg->pool_uuid, rebuild_in->roi_pool_uuid);
		arg->obj_cb = rebuild_obj_iterate_keys;
		arg->root_hdl = btr_hdl;
		arg->rpt = rpt;

		D__ASSERT(rpt->rt_pullers != NULL);

		rc = dss_ult_create(rebuild_puller, arg, -1, NULL);
		if (rc) {
			D__FREE_PTR(arg);
			D__GOTO(out, rc);
		}
		rpt->rt_lead_puller_running = 1;
	}
	D_EXIT;
out:
	rebuild_out = crt_reply_get(rpc);
	rebuild_out->ro_status = rc;
	crt_reply_send(rpc);
}
