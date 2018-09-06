/**
 * (C) Copyright 2017-2018 Intel Corporation.
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

typedef int (*rebuild_obj_iter_cb_t)(daos_unit_oid_t oid, daos_epoch_t eph,
				     unsigned int shard, void *arg);

/* Argument for pool/container/object iteration */
struct puller_iter_arg {
	uuid_t				cont_uuid;
	struct rebuild_tgt_pool_tracker *rpt;
	rebuild_obj_iter_cb_t		obj_cb;
	daos_handle_t			cont_hdl;
	struct rebuild_root		*cont_root;
	unsigned int			yield_freq;
	unsigned int			obj_cnt;
	bool				yielded;
	bool				re_iter;
};

/* Argument for dkey/akey/record iteration */
struct rebuild_iter_obj_arg {
	uuid_t		cont_uuid;
	daos_handle_t	cont_hdl;
	daos_unit_oid_t oid;
	daos_epoch_t	epoch;
	unsigned int	shard;
	struct rebuild_tgt_pool_tracker *rpt;
};

/* Get nthream idx from idx */
static inline unsigned int
rebuild_get_nstream_idx(daos_key_t *dkey)
{
	unsigned int nstream;
	uint64_t hash;

	nstream = dss_get_threads_number();

	hash = d_hash_murmur64((unsigned char *)dkey->iov_buf,
				dkey->iov_len, 5731);
	hash %= nstream;

	return hash;
}

#define PULLER_STACK_SIZE	131072
#define MAX_IOD_NUM		16
#define MAX_BUF_SIZE		2048

static int
rebuild_fetch_update_inline(struct rebuild_one *rdone, daos_handle_t oh,
			    struct ds_cont *ds_cont)
{
	daos_sg_list_t	sgls[MAX_IOD_NUM];
	daos_iov_t	iov[MAX_IOD_NUM];
	char		iov_buf[MAX_IOD_NUM][MAX_BUF_SIZE];
	bool		fetch = false;
	int		i;
	int		rc;

	D_ASSERT(rdone->ro_iod_num <= MAX_IOD_NUM);
	for (i = 0; i < rdone->ro_iod_num; i++) {
		if (rdone->ro_sgls != NULL && rdone->ro_sgls[i].sg_nr > 0) {
			sgls[i] = rdone->ro_sgls[i];
		} else {
			sgls[i].sg_nr = 1;
			sgls[i].sg_nr_out = 1;
			daos_iov_set(&iov[i], iov_buf[i], MAX_BUF_SIZE);
			sgls[i].sg_iovs = &iov[i];
			fetch = true;
		}
	}

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %.*s nr %d eph "DF_U64
		" fetch %s\n", DP_UOID(rdone->ro_oid), rdone,
		(int)rdone->ro_dkey.iov_len, (char *)rdone->ro_dkey.iov_buf,
		rdone->ro_iod_num, rdone->ro_epoch, fetch ? "yes":"no");

	if (fetch) {
		rc = ds_obj_fetch(oh, rdone->ro_epoch, &rdone->ro_dkey,
				  rdone->ro_iod_num, rdone->ro_iods,
				  sgls, NULL);
		if (rc) {
			D_ERROR("ds_obj_fetch %d\n", rc);
			return rc;
		}
	}

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_UPDATE))
		return 0;

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_UPDATE_FAIL))
		return -DER_INVAL;

	rc = vos_obj_update(ds_cont->sc_hdl, rdone->ro_oid, rdone->ro_epoch,
			    rdone->ro_cookie, rdone->ro_version,
			    &rdone->ro_dkey, rdone->ro_iod_num,
			    rdone->ro_iods, sgls);

	return rc;
}

static int
rebuild_fetch_update_bulk(struct rebuild_one *rdone, daos_handle_t oh,
			  struct ds_cont *ds_cont)
{
	daos_sg_list_t	 sgls[MAX_IOD_NUM], *sgl;
	daos_handle_t	 ioh;
	int		 rc, i, ret, sgl_cnt = 0;

	D_ASSERT(rdone->ro_iod_num <= MAX_IOD_NUM);
	rc = vos_update_begin(ds_cont->sc_hdl, rdone->ro_oid, rdone->ro_epoch,
			      &rdone->ro_dkey, rdone->ro_iod_num,
			      rdone->ro_iods, &ioh);
	if (rc != 0) {
		D_ERROR(DF_UOID"preparing update fails: %d\n",
			DP_UOID(rdone->ro_oid), rc);
		return rc;
	}

	rc = eio_iod_prep(vos_ioh2desc(ioh));
	if (rc) {
		D_ERROR("Prepare EIOD for "DF_UOID" error: %d\n",
			DP_UOID(rdone->ro_oid), rc);
		goto end;
	}

	for (i = 0; i < rdone->ro_iod_num; i++) {
		struct eio_sglist	*esgl;

		esgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(esgl != NULL);
		sgl = &sgls[i];

		rc = eio_sgl_convert(esgl, sgl);
		if (rc)
			goto post;
		sgl_cnt++;
	}

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %.*s nr %d eph "DF_U64"\n",
		DP_UOID(rdone->ro_oid), rdone, (int)rdone->ro_dkey.iov_len,
		(char *)rdone->ro_dkey.iov_buf, rdone->ro_iod_num,
		rdone->ro_epoch);

	rc = ds_obj_fetch(oh, rdone->ro_epoch, &rdone->ro_dkey,
			  rdone->ro_iod_num, rdone->ro_iods,
			  sgls, NULL);
	if (rc)
		D_ERROR("rebuild dkey %.*s failed rc %d\n",
			(int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf, rc);
post:
	for (i = 0; i < sgl_cnt; i++) {
		sgl = &sgls[i];
		daos_sgl_fini(sgl, false);
	}

	ret = eio_iod_post(vos_ioh2desc(ioh));
	if (ret) {
		D_ERROR("Post EIOD for "DF_UOID" error: %d\n",
			DP_UOID(rdone->ro_oid), ret);
		rc = rc ? : ret;
	}

end:
	vos_update_end(ioh, rdone->ro_cookie, rdone->ro_version,
		       &rdone->ro_dkey, rc);
	return rc;
}

/**
 * Punch dkeys/akeys before rebuild.
 */
static int
rebuild_one_punch_keys(struct rebuild_tgt_pool_tracker *rpt,
		       struct rebuild_one *rdone, struct ds_cont *cont)
{
	int	i;
	int	rc = 0;

	/* Punch dkey */
	if (rdone->ro_max_eph != DAOS_EPOCH_MAX) {
		D_DEBUG(DB_REBUILD, DF_UOID" punch dkey %.*s eph "DF_U64"\n",
			DP_UOID(rdone->ro_oid), (int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf, rdone->ro_max_eph);
		rc = vos_obj_punch(cont->sc_hdl, rdone->ro_oid,
				   rdone->ro_max_eph, rpt->rt_coh_uuid,
				   rpt->rt_rebuild_ver, VOS_OF_REPLAY_PC,
				   &rdone->ro_dkey, 0, NULL);
		if (rc) {
			D_ERROR(DF_UOID" punch dkey failed: rc %d\n",
				DP_UOID(rdone->ro_oid), rc);
			return rc;
		}
	}

	if (rdone->ro_ephs == NULL)
		return 0;

	/* Punch akeys */
	for (i = 0; i < rdone->ro_ephs_num; i++) {
		D_DEBUG(DB_REBUILD, DF_UOID" punch dkey %.*s akey %.*s"
			" eph "DF_U64"\n", DP_UOID(rdone->ro_oid),
			(int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf,
			(int)rdone->ro_ephs_keys[i].iov_len,
			(char *)rdone->ro_ephs_keys[i].iov_buf,
			rdone->ro_ephs[i]);
		D_ASSERT(rdone->ro_ephs[i] != DAOS_EPOCH_MAX);
		rc = vos_obj_punch(cont->sc_hdl, rdone->ro_oid,
				   rdone->ro_ephs[i], rpt->rt_coh_uuid,
				   rpt->rt_rebuild_ver, VOS_OF_REPLAY_PC,
				   &rdone->ro_dkey, 1, &rdone->ro_ephs_keys[i]);
		if (rc) {
			D_ERROR(DF_UOID" punch akey failed: rc %d\n",
				DP_UOID(rdone->ro_oid), rc);
			return rc;
		}
	}

	return rc;
}

static int
rebuild_one(struct rebuild_tgt_pool_tracker *rpt,
	    struct rebuild_one *rdone)
{
	struct rebuild_pool_tls	*tls;
	struct ds_cont		*rebuild_cont;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	daos_handle_t		oh;
	daos_size_t		data_size;
	int			rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	if (daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;
		struct pool_map *map = rebuild_pool_map_get(rpt->rt_pool);

		rc = dc_pool_local_open(rpt->rt_pool_uuid, rpt->rt_poh_uuid,
					0, NULL, map, rpt->rt_svc_list, &ph);
		rebuild_pool_map_put(map);
		if (rc)
			D_GOTO(free, rc);

		tls->rebuild_pool_hdl = ph;
	}

	/* Open client dc handle */
	rc = dc_cont_local_open(rdone->ro_cont_uuid, rpt->rt_coh_uuid,
				0, tls->rebuild_pool_hdl, &coh);
	if (rc)
		D_GOTO(free, rc);

	rc = ds_obj_open(coh, rdone->ro_oid.id_pub, rdone->ro_epoch, DAOS_OO_RW,
			 &oh);
	if (rc)
		D_GOTO(cont_close, rc);

	rc = ds_cont_lookup(rpt->rt_pool_uuid, rdone->ro_cont_uuid,
			    &rebuild_cont);
	if (rc)
		D_GOTO(obj_close, rc);

	rc = rebuild_one_punch_keys(rpt, rdone, rebuild_cont);
	if (rc)
		D_GOTO(cont_put, rc);

	data_size = daos_iods_len(rdone->ro_iods, rdone->ro_iod_num);
	D_ASSERT(data_size != (uint64_t)(-1));

	/* DAOS_REBUILD_TGT_NO_REBUILD are for testing purpose */
	if (data_size > 0 && !DAOS_FAIL_CHECK(DAOS_REBUILD_NO_REBUILD)) {
		if (data_size < MAX_BUF_SIZE)
			rc = rebuild_fetch_update_inline(rdone, oh, rebuild_cont);
		else
			rc = rebuild_fetch_update_bulk(rdone, oh, rebuild_cont);
	}

	tls->rebuild_pool_rec_count += rdone->ro_rec_num;
cont_put:
	ds_cont_put(rebuild_cont);
obj_close:
	ds_obj_close(oh);
cont_close:
	dc_cont_local_close(tls->rebuild_pool_hdl, coh);
free:
	return rc;
}

void
rebuild_one_destroy(struct rebuild_one *rdone)
{
	int i;

	D_ASSERT(d_list_empty(&rdone->ro_list));
	daos_iov_free(&rdone->ro_dkey);

	if (rdone->ro_iods) {
		for (i = 0; i < rdone->ro_iod_alloc_num; i++) {
			daos_iov_free(&rdone->ro_iods[i].iod_name);

			if (rdone->ro_iods[i].iod_recxs)
				D_FREE(rdone->ro_iods[i].iod_recxs);

			if (rdone->ro_iods[i].iod_eprs)
				D_FREE(rdone->ro_iods[i].iod_eprs);

			if (rdone->ro_iods[i].iod_csums)
				D_FREE(rdone->ro_iods[i].iod_csums);
		}
		D_FREE(rdone->ro_iods);
	}

	if (rdone->ro_ephs) {
		for (i = 0; i < rdone->ro_ephs_num; i++)
			daos_iov_free(&rdone->ro_ephs_keys[i]);
		D_FREE(rdone->ro_ephs);
	}

	if (rdone->ro_sgls) {
		for (i = 0; i < rdone->ro_iod_alloc_num; i++)
			daos_sgl_fini(&rdone->ro_sgls[i], true);
		D_FREE(rdone->ro_sgls);
	}

	D_FREE_PTR(rdone);
}

static void
rebuild_one_ult(void *arg)
{
	struct rebuild_pool_tls		*tls;
	struct rebuild_tgt_pool_tracker *rpt = arg;
	struct rebuild_puller		*puller;
	unsigned int			idx;

	while (daos_fail_check(DAOS_REBUILD_TGT_REBUILD_HANG))
		ABT_thread_yield();

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	D_ASSERT(rpt->rt_pullers != NULL);
	idx = dss_get_module_info()->dmi_tid;
	puller = &rpt->rt_pullers[idx];
	puller->rp_ult_running = 1;
	while (1) {
		struct rebuild_one	*rdone;
		struct rebuild_one	*tmp;
		d_list_t		rebuild_list;
		int			rc = 0;

		D_INIT_LIST_HEAD(&rebuild_list);
		ABT_mutex_lock(puller->rp_lock);
		d_list_for_each_entry_safe(rdone, tmp, &puller->rp_one_list,
					   ro_list) {
			d_list_move(&rdone->ro_list, &rebuild_list);
			puller->rp_inflight++;
		}
		ABT_mutex_unlock(puller->rp_lock);

		d_list_for_each_entry_safe(rdone, tmp, &rebuild_list, ro_list) {
			d_list_del_init(&rdone->ro_list);
			if (!rpt->rt_abort) {
				rc = rebuild_one(rpt, rdone);
				D_DEBUG(DB_REBUILD, DF_UOID" rebuild dkey %.*s "
					"rc %d tag %d\n",
					DP_UOID(rdone->ro_oid),
					(int)rdone->ro_dkey.iov_len,
					(char *)rdone->ro_dkey.iov_buf, rc,
					idx);
			}

			D_ASSERT(puller->rp_inflight > 0);
			puller->rp_inflight--;

			/* Ignore nonexistent error because puller could race
			 * with user's container destroy:
			 * - puller got the container+oid from a remote scanner
			 * - user destroyed the container
			 * - puller try to open container or pulling data
			 *   (nonexistent)
			 * This is just a workaround...
			 */
			if (tls->rebuild_pool_status == 0 && rc != 0 &&
			    rc != -DER_NONEXIST) {
				tls->rebuild_pool_status = rc;
				rpt->rt_abort = 1;
			}
			/* XXX If rebuild fails, Should we add this back to
			 * dkey list
			 */
			rebuild_one_destroy(rdone);
		}

		/* check if it should exist */
		ABT_mutex_lock(puller->rp_lock);
		if (d_list_empty(&puller->rp_one_list) && rpt->rt_finishing) {
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
	rpt_put(rpt);
}

/*
 * Queue dkey to the rebuild dkey list on each xstream. Note that this function
 * steals the memory of the recx, csum, and epr arrays from iods.
 */
static int
rebuild_one_queue(struct rebuild_iter_obj_arg *iter_arg, daos_unit_oid_t *oid,
		  daos_key_t *dkey, daos_epoch_t dkey_eph, daos_iod_t *iods,
		  daos_epoch_t *akey_ephs, int iod_eph_total,
		  daos_sg_list_t *sgls, uuid_t cookie, uint32_t version)
{
	struct rebuild_puller		*puller;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	struct rebuild_one		*rdone = NULL;
	unsigned int			idx;
	unsigned int			iod_cnt = 0;
	unsigned int			ephs_cnt = 0;
	unsigned int			rec_cnt = 0;
	daos_epoch_t			min_epoch = 0;
	int				i;
	int				rc;

	D_DEBUG(DB_REBUILD, "rebuild dkey %.*s iod nr %d\n",
		(int)dkey->iov_buf_len, (char *)dkey->iov_buf, iod_eph_total);

	if (iod_eph_total == 0)
		return 0;

	D_ALLOC_PTR(rdone);
	if (rdone == NULL)
		return -DER_NOMEM;

	D_ALLOC(rdone->ro_iods, iod_eph_total * sizeof(*rdone->ro_iods));
	if (rdone->ro_iods == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	D_ALLOC(rdone->ro_ephs, iod_eph_total * sizeof(*rdone->ro_ephs));
	D_ALLOC(rdone->ro_ephs_keys, iod_eph_total *
				     sizeof(*rdone->ro_ephs_keys));
	if (rdone->ro_iods == NULL || rdone->ro_ephs == NULL ||
	    rdone->ro_ephs_keys == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	rdone->ro_iod_alloc_num = iod_eph_total;
	for (i = 0; i < iod_eph_total; i++) {
		int j;

		if (akey_ephs[i] != DAOS_EPOCH_MAX) {
			/* Pack punched epoch here */
			rdone->ro_ephs[ephs_cnt] = akey_ephs[i];
			rc = daos_iov_copy(&rdone->ro_ephs_keys[ephs_cnt],
					   &iods[i].iod_name);
			if (rc)
				D_GOTO(free, rc);

			ephs_cnt++;
			D_DEBUG(DB_REBUILD, "punched iod idx %d akey %.*s"
				" ephs "DF_U64" ephs_cnt %d\n", i,
				(int)iods[i].iod_name.iov_len,
				(char *)iods[i].iod_name.iov_buf,
				akey_ephs[i], ephs_cnt);
		}

		if (iods[i].iod_nr > 0) {
			rc = daos_iov_copy(&rdone->ro_iods[iod_cnt].iod_name,
					   &iods[i].iod_name);
			if (rc)
				D_GOTO(free, rc);

			rdone->ro_iods[iod_cnt].iod_kcsum = iods[i].iod_kcsum;
			rdone->ro_iods[iod_cnt].iod_type = iods[i].iod_type;
			rdone->ro_iods[iod_cnt].iod_size = iods[i].iod_size;
			rdone->ro_iods[iod_cnt].iod_nr = iods[i].iod_nr;
			rdone->ro_iods[iod_cnt].iod_recxs = iods[i].iod_recxs;
			rdone->ro_iods[iod_cnt].iod_csums = iods[i].iod_csums;
			rdone->ro_iods[iod_cnt].iod_eprs = iods[i].iod_eprs;

			for (j = 0; j < iods[i].iod_nr; j++) {
				rec_cnt += iods[i].iod_recxs[j].rx_nr;
				if (min_epoch == 0 ||
				    iods[i].iod_eprs[j].epr_lo < min_epoch)
					min_epoch = iods[i].iod_eprs[j].epr_lo;
			}

			D_DEBUG(DB_REBUILD, "idx %d akey %.*s nr %d size "
				DF_U64" type %d eph "DF_U64"/"DF_U64" ephs "
				DF_U64"\n", i, (int)iods[i].iod_name.iov_len,
				(char *)iods[i].iod_name.iov_buf,
				iods[i].iod_nr, iods[i].iod_size,
				iods[i].iod_type, iods[i].iod_eprs->epr_lo,
				iods[i].iod_eprs->epr_hi, akey_ephs[i]);

			/* Check if data has been retrieved by iteration */
			if (sgls[i].sg_nr > 0) {
				if (rdone->ro_sgls == NULL) {
					D_ALLOC(rdone->ro_sgls,
						iod_eph_total *
						sizeof(*rdone->ro_sgls));
					if (rdone->ro_sgls == NULL)
						D_GOTO(free, rc = -DER_NOMEM);
				}

				rc = daos_sgl_alloc_copy_data(
					&rdone->ro_sgls[iod_cnt], &sgls[i]);
				if (rc)
					D_GOTO(free, rc);
			}

			iod_cnt++;
			iods[i].iod_recxs = NULL;
			iods[i].iod_csums = NULL;
			iods[i].iod_eprs = NULL;
		}
	}

	rdone->ro_iod_num = iod_cnt;
	rdone->ro_ephs_num = ephs_cnt;
	rdone->ro_max_eph = dkey_eph;
	rdone->ro_rec_num = rec_cnt;
	rdone->ro_version = version;
	rdone->ro_epoch = min_epoch;
	uuid_copy(rdone->ro_cookie, cookie);
	idx = rebuild_get_nstream_idx(dkey);
	puller = &rpt->rt_pullers[idx];
	if (puller->rp_ult == NULL) {
		/* Create puller ULT thread, and destroy ULT until
		 * rebuild finish in rebuild_fini().
		 */
		D_ASSERT(puller->rp_ult_running == 0);
		D_DEBUG(DB_REBUILD, "create rebuild dkey ult %d\n", idx);
		rpt_get(rpt);
		rc = dss_ult_create(rebuild_one_ult, rpt, idx,
				    PULLER_STACK_SIZE, &puller->rp_ult);
		if (rc) {
			rpt_put(rpt);
			D_GOTO(free, rc);
		}
	}

	D_INIT_LIST_HEAD(&rdone->ro_list);
	rc = daos_iov_copy(&rdone->ro_dkey, dkey);
	if (rc != 0)
		D_GOTO(free, rc);

	rdone->ro_oid = *oid;
	uuid_copy(rdone->ro_cont_uuid, iter_arg->cont_uuid);

	D_DEBUG(DB_REBUILD, DF_UOID" %p dkey %.*s rebuild on idx %d max eph "
		DF_U64" iod_num %d\n", DP_UOID(rdone->ro_oid), rdone,
		(int)dkey->iov_len, (char *)dkey->iov_buf, idx,
		rdone->ro_max_eph, rdone->ro_iod_num);

	ABT_mutex_lock(puller->rp_lock);
	d_list_add_tail(&rdone->ro_list, &puller->rp_one_list);
	ABT_mutex_unlock(puller->rp_lock);

free:
	if (rc != 0 && rdone != NULL)
		rebuild_one_destroy(rdone);

	return rc;
}

#if 1 /* TODO: Move to dss. */
static int
grow_array(void **arrayp, size_t elem_size, int old_len, int new_len)
{
	void *p;

	D_ASSERTF(old_len < new_len, "%d < %d\n", old_len, new_len);
	D_REALLOC(p, *arrayp, elem_size * new_len);
	if (p == NULL)
		return -DER_NOMEM;
	/* Until D_REALLOC does this, zero the new segment. */
	memset(p + elem_size * old_len, 0, elem_size * (new_len - old_len));
	*arrayp = p;
	return 0;
}

/* Parse recxs in <*data, len> and append them to iod and sgl. */
static int
unpack_recxs(daos_iod_t *iod, int *recxs_cap, daos_sg_list_t *sgl,
	     daos_key_t *akey, daos_key_desc_t *kds, void **data,
	     daos_size_t len, uuid_t cookie, uint32_t *version)
{
	int rc = 0;

	if (iod->iod_name.iov_len == 0)
		daos_iov_copy(&iod->iod_name, akey);
	else
		D_ASSERT(daos_key_match(&iod->iod_name, akey));

	if (kds == NULL)
		return 0;

	while (len > 0) {
		struct obj_enum_rec *rec = *data;

		D_DEBUG(DB_REBUILD, "data %p len "DF_U64"\n", *data, len);

		/* Every recx begins with an obj_enum_rec. */
		if (len < sizeof(*rec)) {
			D_ERROR("invalid recxs: <%p, %zu>\n", *data, len);
			rc = -DER_INVAL;
			break;
		}

		/* Check if the cookie or the version is changing. */
		if (uuid_is_null(cookie)) {
			uuid_copy(cookie, rec->rec_cookie);
			*version = rec->rec_version;
		} else if (uuid_compare(cookie, rec->rec_cookie) != 0 ||
			   *version != rec->rec_version) {
			D_DEBUG(DB_REBUILD, "different cookie or version"
				DF_UUIDF" "DF_UUIDF" %u != %u\n",
				DP_UUID(cookie), DP_UUID(rec->rec_cookie),
				*version, rec->rec_version);
			rc = 1;
			break;
		}

		/* Iteration might return multiple single record with same
		 * dkey/akeks but different epoch. But fetch & update only
		 * allow 1 SINGLE type record per IOD. Let's put these
		 * single records in different IODs.
		 */
		if (kds->kd_val_types == VOS_ITER_SINGLE && iod->iod_nr > 0) {
			rc = 1;
			break;
		}

		if (iod->iod_size != 0 && iod->iod_size != rec->rec_size)
			D_WARN("rsize "DF_U64" != "DF_U64" are different"
			       " under one akey\n", iod->iod_size,
			       rec->rec_size);

		/* If the arrays are full, grow them as if all the remaining
		 * recxs have no inline data.
		 */
		if (iod->iod_nr + 1 > *recxs_cap) {
			int cap = *recxs_cap + len / sizeof(*rec);

			rc = grow_array((void **)&iod->iod_recxs,
					sizeof(*iod->iod_recxs), *recxs_cap,
					cap);
			if (rc != 0)
				break;
			rc = grow_array((void **)&iod->iod_eprs,
					sizeof(*iod->iod_eprs), *recxs_cap,
					cap);
			if (rc != 0)
				break;
			if (sgl != NULL) {
				rc = grow_array((void **)&sgl->sg_iovs,
						sizeof(*sgl->sg_iovs),
						*recxs_cap, cap);
				if (rc != 0)
					break;
			}
			/* If we execute any of the three breaks above,
			 * *recxs_cap will be < the real capacities of some of
			 * the arrays. This is harmless, as it only causes the
			 * diff segment to be copied and zeroed unnecessarily
			 * next time we grow them.
			 */
			*recxs_cap = cap;
		}

		/* Append one more recx. */
		iod->iod_eprs[iod->iod_nr] = rec->rec_epr;
		/* Iteration does not fill the high epoch, so let's reset
		 * the high epoch with EPOCH_MAX to make vos fetch/update happy.
		 */
		iod->iod_eprs[iod->iod_nr].epr_hi = DAOS_EPOCH_MAX;
		iod->iod_recxs[iod->iod_nr] = rec->rec_recx;
		iod->iod_nr++;
		if (iod->iod_size == 0)
			iod->iod_size = rec->rec_size;
		*data += sizeof(*rec);
		len -= sizeof(*rec);

		/* Append the data, if inline. */
		if (sgl != NULL) {
			daos_iov_t *iov = &sgl->sg_iovs[sgl->sg_nr];

			if (rec->rec_flags & RECX_INLINE) {
				daos_iov_set(iov, *data,
					     rec->rec_size *
					     rec->rec_recx.rx_nr);
				D_DEBUG(DB_TRACE, "set data iov %p len %d\n",
					iov, (int)iov->iov_len);
			} else {
				daos_iov_set(iov, NULL, 0);
			}
			sgl->sg_nr++;
			D_ASSERTF(sgl->sg_nr == iod->iod_nr, "%u == %u\n",
				  sgl->sg_nr, iod->iod_nr);
			*data += iov->iov_len;
			len -= iov->iov_len;
		}

		D_DEBUG(DB_REBUILD, "pack %p idx/nr "DF_U64"/"DF_U64
			" epr lo/hi "DF_U64"/"DF_U64" size %zd inline %zu\n",
			*data, iod->iod_recxs[iod->iod_nr - 1].rx_idx,
			iod->iod_recxs[iod->iod_nr - 1].rx_nr,
			iod->iod_eprs[iod->iod_nr - 1].epr_lo,
			iod->iod_eprs[iod->iod_nr - 1].epr_hi, iod->iod_size,
			sgl != NULL ? sgl->sg_iovs[sgl->sg_nr - 1].iov_len : 0);
	}

	if (kds->kd_val_types == VOS_ITER_RECX)
		iod->iod_type = DAOS_IOD_ARRAY;
	else
		iod->iod_type = DAOS_IOD_SINGLE;

	D_DEBUG(DB_REBUILD, "pack nr %d cookie/version "DF_UUID"/%u rc %d\n",
		iod->iod_nr, DP_UUID(cookie), *version, rc);
	return rc;
}

/**
 * Initialize \a io with \a iods[\a iods_cap], \a recxs_caps[\a iods_cap], and
 * \a sgls[\a iods_cap].
 *
 * \param[in,out]	io		I/O descriptor
 * \param[in]		iods		daos_iod_t array
 * \param[in]		recxs_caps	recxs capacity array
 * \param[in]		sgls		optional sgl array for inline recxs
 * \param[in]		ephs		epoch array
 * \param[in]		iods_cap	maximal number of elements in \a iods,
 *					\a recxs_caps, \a sgls, and \a ephs
 */
void
dss_enum_unpack_io_init(struct dss_enum_unpack_io *io, daos_iod_t *iods,
			int *recxs_caps, daos_sg_list_t *sgls,
			daos_epoch_t *ephs, int iods_cap)
{
	int i;

	memset(io, 0, sizeof(*io));

	io->ui_dkey_eph = DAOS_EPOCH_MAX;

	D_ASSERTF(iods_cap > 0, "%d\n", iods_cap);
	io->ui_iods_cap = iods_cap;

	D_ASSERT(iods != NULL);
	memset(iods, 0, sizeof(*iods) * iods_cap);
	io->ui_iods = iods;

	D_ASSERT(recxs_caps != NULL);
	memset(recxs_caps, 0, sizeof(*recxs_caps) * iods_cap);
	io->ui_recxs_caps = recxs_caps;

	if (sgls != NULL) {
		memset(sgls, 0, sizeof(*sgls) * iods_cap);
		io->ui_sgls = sgls;
	}

	for (i = 0; i < iods_cap; i++)
		ephs[i] = DAOS_EPOCH_MAX;

	io->ui_akey_ephs = ephs;
	uuid_clear(io->ui_cookie);
}

static void
clear_iod(daos_iod_t *iod, daos_sg_list_t *sgl, int *recxs_cap)
{
	daos_iov_free(&iod->iod_name);
	if (iod->iod_recxs != NULL)
		D_FREE(iod->iod_recxs);
	if (iod->iod_eprs != NULL)
		D_FREE(iod->iod_eprs);
	memset(iod, 0, sizeof(*iod));

	if (sgl != NULL) {
		if (sgl->sg_iovs != NULL)
			D_FREE(sgl->sg_iovs);
		memset(sgl, 0, sizeof(*sgl));
	}

	*recxs_cap = 0;
}

/**
 * Clear the iods/sgls in \a io.
 *
 * \param[in]	io	I/O descriptor
 */
void
dss_enum_unpack_io_clear(struct dss_enum_unpack_io *io)
{
	int i;

	for (i = 0; i < io->ui_iods_len; i++) {
		daos_sg_list_t *sgl = NULL;

		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[i];
		clear_iod(&io->ui_iods[i], sgl, &io->ui_recxs_caps[i]);
		if (io->ui_akey_ephs)
			io->ui_akey_ephs[i] = DAOS_EPOCH_MAX;
	}

	io->ui_dkey_eph = DAOS_EPOCH_MAX;
	io->ui_iods_len = 0;
	uuid_clear(io->ui_cookie);
	io->ui_version = 0;
}

/**
 * Finalize \a io. All iods/sgls must have already been cleard.
 *
 * \param[in]	io	I/O descriptor
 */
void
dss_enum_unpack_io_fini(struct dss_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_len == 0, "%d\n", io->ui_iods_len);
	daos_iov_free(&io->ui_dkey);
}

/*
 * Close the current iod (i.e., io->ui_iods[io->ui_iods_len]). If it contains
 * recxs, append it to io by incrementing io->ui_iods_len. If it doesn't
 * contain any recx, clear it.
 */
static void
close_iod(struct dss_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_cap > 0, "%d > 0\n", io->ui_iods_cap);
	D_ASSERTF(io->ui_iods_len < io->ui_iods_cap, "%d < %d\n",
		  io->ui_iods_len, io->ui_iods_cap);
	if (io->ui_iods[io->ui_iods_len].iod_nr > 0) {
		io->ui_iods_len++;
	} else {
		daos_sg_list_t *sgl = NULL;

		D_DEBUG(DB_TRACE, "iod without recxs: %d\n", io->ui_iods_len);
		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[io->ui_iods_len];
		clear_iod(&io->ui_iods[io->ui_iods_len], sgl,
			  &io->ui_recxs_caps[io->ui_iods_len]);
	}
}

/* Close io, pass it to cb, and clear it. */
static int
complete_io(struct dss_enum_unpack_io *io, dss_enum_unpack_cb_t cb, void *arg)
{
	int rc = 0;

	if (io->ui_iods_len == 0) {
		D_DEBUG(DB_TRACE, "io empty\n");
		goto out;
	}
	rc = cb(io, arg);
out:
	dss_enum_unpack_io_clear(io);
	return rc;
}

/**
 * Unpack the result of a dss_enum_pack enumeration into \a io, which can then
 * be used to issue a VOS update. \a arg->*_anchor are ignored currently. \a cb
 * will be called, for the caller to consume the recxs accumulated in \a io.
 *
 * \param[in]		type	enumeration type
 * \param[in]		arg	enumeration argument
 * \param[in]		cb	callback
 * \param[in]		cb_arg	callback argument
 */
int
dss_enum_unpack(vos_iter_type_t type, struct dss_enum_arg *arg,
		dss_enum_unpack_cb_t cb, void *cb_arg)
{
	struct dss_enum_unpack_io	io;
	daos_iod_t			iods[MAX_IOD_NUM];
	int				recxs_caps[MAX_IOD_NUM];
	daos_epoch_t			ephs[MAX_IOD_NUM];
	daos_sg_list_t			sgls[MAX_IOD_NUM];
	daos_key_t			akey = {0};
	daos_epoch_range_t		*eprs = arg->eprs;
	void				*ptr;
	unsigned int			i;
	int				rc = 0;

	/* Currently, this function is only for unpacking recursive
	 * enumerations from arg->kds and arg->sgl.
	 */
	D_ASSERT(arg->recursive && !arg->fill_recxs);

	D_ASSERT(arg->kds_len > 0);
	D_ASSERT(arg->kds != NULL);
	if (arg->kds[0].kd_val_types != type) {
		D_ERROR("the first kds type %d != %d\n",
			arg->kds[0].kd_val_types, type);
		return -DER_INVAL;
	}

	dss_enum_unpack_io_init(&io, iods, recxs_caps, sgls, ephs,
				MAX_IOD_NUM);
	if (type > VOS_ITER_OBJ)
		io.ui_oid = arg->param.ip_oid;

	D_ASSERTF(arg->sgl->sg_nr > 0, "%u\n", arg->sgl->sg_nr);
	D_ASSERT(arg->sgl->sg_iovs != NULL);
	ptr = arg->sgl->sg_iovs[0].iov_buf;

	for (i = 0; i < arg->kds_len; i++) {
		D_DEBUG(DB_REBUILD, "process %d type %d ptr %p len "DF_U64
			" total %zd\n", i, arg->kds[i].kd_val_types, ptr,
			arg->kds[i].kd_key_len, arg->sgl->sg_iovs[0].iov_len);

		D_ASSERT(arg->kds[i].kd_key_len > 0);
		if (arg->kds[i].kd_val_types == VOS_ITER_OBJ) {
			daos_unit_oid_t *oid = ptr;

			if (arg->kds[i].kd_key_len != sizeof(*oid)) {
				D_ERROR("Invalid object ID size: "DF_U64
					" != %zu\n", arg->kds[i].kd_key_len,
					sizeof(*oid));
				rc = -DER_INVAL;
				break;
			}
			if (daos_unit_oid_is_null(io.ui_oid)) {
				io.ui_oid = *oid;
			} else if (daos_unit_oid_compare(io.ui_oid, *oid) !=
				   0) {
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc != 0)
					break;
				daos_iov_free(&io.ui_dkey);
				io.ui_oid = *oid;
			}
			D_DEBUG(DB_REBUILD, "process obj "DF_UOID"\n",
				DP_UOID(io.ui_oid));
		} else if (arg->kds[i].kd_val_types == VOS_ITER_DKEY) {
			daos_key_t tmp_key;

			tmp_key.iov_buf = ptr;
			tmp_key.iov_buf_len = arg->kds[i].kd_key_len;
			tmp_key.iov_len = arg->kds[i].kd_key_len;
			if (eprs != NULL)
				io.ui_dkey_eph = eprs[i].epr_lo;

			if (io.ui_dkey.iov_len == 0) {
				daos_iov_copy(&io.ui_dkey, &tmp_key);
			} else if (!daos_key_match(&io.ui_dkey, &tmp_key) ||
				   (eprs != NULL &&
				    io.ui_dkey_eph != eprs[i].epr_lo)) {
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc != 0)
					break;

				if (!daos_key_match(&io.ui_dkey, &tmp_key)) {
					daos_iov_free(&io.ui_dkey);
					daos_iov_copy(&io.ui_dkey, &tmp_key);
				}
			}

			D_DEBUG(DB_REBUILD, "process dkey %.*s eph "DF_U64"\n",
				(int)io.ui_dkey.iov_len,
				(char *)io.ui_dkey.iov_buf,
				eprs ? io.ui_dkey_eph : 0);
		} else if (arg->kds[i].kd_val_types == VOS_ITER_AKEY) {
			daos_key_t *iod_akey;

			akey.iov_buf = ptr;
			akey.iov_buf_len = arg->kds[i].kd_key_len;
			akey.iov_len = arg->kds[i].kd_key_len;
			if (io.ui_dkey.iov_buf == NULL) {
				D_ERROR("No dkey for akey %*.s invalid buf.\n",
				      (int)akey.iov_len, (char *)akey.iov_buf);
				rc = -DER_INVAL;
				break;
			}
			D_DEBUG(DB_REBUILD, "process akey %.*s\n",
				(int)akey.iov_len, (char *)akey.iov_buf);

			if (io.ui_iods_len >= io.ui_iods_cap) {
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc < 0)
					goto out;
			}

			/* If there are no records for akey(punched akey rec),
			 * then ui_iods_len might still point to the last dkey,
			 * i.e. close_iod are not being called.
			 */
			iod_akey = &io.ui_iods[io.ui_iods_len].iod_name;
			if (iod_akey->iov_len != 0 &&
			    !daos_key_match(iod_akey, &akey))
				io.ui_iods_len++;

			rc = unpack_recxs(&io.ui_iods[io.ui_iods_len],
					  NULL, NULL, &akey, NULL, NULL,
					  0, NULL, NULL);
			if (rc < 0)
				goto out;

			if (eprs)
				io.ui_akey_ephs[io.ui_iods_len] =
							eprs[i].epr_lo;
		} else if (arg->kds[i].kd_val_types == VOS_ITER_SINGLE ||
			   arg->kds[i].kd_val_types == VOS_ITER_RECX) {
			void *data = ptr;

			if (io.ui_dkey.iov_len == 0 || akey.iov_len == 0) {
				D_ERROR("invalid list buf for kds %d\n", i);
				rc = -DER_INVAL;
				break;
			}

			while (1) {
				daos_size_t	len;
				int		j = io.ui_iods_len;

				/* Because vos_obj_update only accept single
				 * cookie/version, let's go through the records
				 * to check different cookie and version, and
				 * queue rebuild.
				 */
				len = ptr + arg->kds[i].kd_key_len - data;
				rc = unpack_recxs(&io.ui_iods[j],
						  &io.ui_recxs_caps[j],
						  io.ui_sgls == NULL ?
						  NULL : &io.ui_sgls[j], &akey,
						  &arg->kds[i], &data, len,
						  io.ui_cookie,
						  &io.ui_version);
				if (rc < 0)
					goto out;

				/* All records referred by this kds has been
				 * packed, then it does not need to send
				 * right away, might pack more next round.
				 */
				if (rc == 0)
					break;

				/* Otherwise let's complete current io, and go
				 * next round.
				 */
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc < 0)
					goto out;
			}
		} else {
			D_ERROR("unknow kds type %d\n",
				arg->kds[i].kd_val_types);
			rc = -DER_INVAL;
			break;
		}
		ptr += arg->kds[i].kd_key_len;
	}

	if (io.ui_iods[0].iod_nr > 0) {
		close_iod(&io);
		rc = complete_io(&io, cb, cb_arg);
	}

	D_DEBUG(DB_REBUILD, "process list buf "DF_UOID" rc %d\n",
		DP_UOID(io.ui_oid), rc);

out:
	dss_enum_unpack_io_fini(&io);
	return rc;
}
#endif

static int
rebuild_one_queue_cb(struct dss_enum_unpack_io *io, void *arg)
{
	return rebuild_one_queue(arg, &io->ui_oid, &io->ui_dkey,
				 io->ui_dkey_eph, io->ui_iods,
				 io->ui_akey_ephs, io->ui_iods_len,
				 io->ui_sgls, io->ui_cookie, io->ui_version);
}

static int
rebuild_obj_punch_one(void *data)
{
	struct rebuild_iter_obj_arg *arg = data;
	struct ds_cont	*cont;
	int		rc;

	D_DEBUG(DB_REBUILD, "punch "DF_UOID"\n", DP_UOID(arg->oid));
	rc = ds_cont_lookup(arg->rpt->rt_pool_uuid, arg->cont_uuid, &cont);
	D_ASSERT(rc == 0);

	rc = vos_obj_punch(cont->sc_hdl, arg->oid, arg->epoch,
			   arg->rpt->rt_coh_uuid, arg->rpt->rt_rebuild_ver,
			   0, NULL, 0, NULL);
	ds_cont_put(cont);
	if (rc)
		D_ERROR(DF_UOID" rebuild punch failed rc %d\n",
			DP_UOID(arg->oid), rc);

	return rc;
}

static int
rebuild_obj_punch(struct rebuild_iter_obj_arg *arg)
{
	return dss_task_collective(rebuild_obj_punch_one, arg);
}

#define KDS_NUM		16
#define ITER_BUF_SIZE   2048

/**
 * Iterate akeys/dkeys of the object
 */
static void
rebuild_obj_ult(void *data)
{
	struct rebuild_iter_obj_arg    *arg = data;
	struct rebuild_pool_tls	       *tls;
	daos_anchor_t			anchor;
	daos_anchor_t			dkey_anchor;
	daos_anchor_t			akey_anchor;
	daos_handle_t			oh;
	daos_sg_list_t			sgl = { 0 };
	daos_iov_t			iov = { 0 };
	char				buf[ITER_BUF_SIZE];
	struct dss_enum_arg		enum_arg;
	int				rc;

	tls = rebuild_pool_tls_lookup(arg->rpt->rt_pool_uuid,
				      arg->rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	if (arg->epoch != DAOS_EPOCH_MAX) {
		rc = rebuild_obj_punch(arg);
		if (rc)
			D_GOTO(free, rc);
	}

	rc = ds_obj_open(arg->cont_hdl, arg->oid.id_pub, arg->epoch,
			 DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(free, rc);

	D_DEBUG(DB_REBUILD, "start rebuild obj "DF_UOID" for shard %u\n",
		DP_UOID(arg->oid), arg->shard);
	memset(&anchor, 0, sizeof(anchor));
	memset(&dkey_anchor, 0, sizeof(dkey_anchor));
	memset(&akey_anchor, 0, sizeof(akey_anchor));
	dc_obj_shard2anchor(&anchor, arg->shard);

	/* Initialize enum_arg for VOS_ITER_DKEY. */
	memset(&enum_arg, 0, sizeof(enum_arg));
	enum_arg.param.ip_hdl = arg->cont_hdl;
	enum_arg.param.ip_oid = arg->oid;
	enum_arg.recursive = true;

	while (1) {
		daos_key_desc_t	kds[KDS_NUM] = { 0 };
		daos_epoch_range_t eprs[KDS_NUM];
		uint32_t	num = KDS_NUM;
		daos_size_t	size;

		memset(buf, 0, ITER_BUF_SIZE);
		iov.iov_len = 0;
		iov.iov_buf = buf;
		iov.iov_buf_len = ITER_BUF_SIZE;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;

		rc = ds_obj_list_obj(oh, arg->epoch, NULL, NULL, &size,
				     &num, kds, eprs, &sgl, &anchor,
				     &dkey_anchor, &akey_anchor);
		if (rc) {
			/* container might have been destroyed. Or there is
			 * no spare target left for this object see
			 * obj_grp_valid_shard_get()
			 */
			rc = (rc == -DER_NONEXIST) ? 0 : rc;
			break;
		}
		if (num == 0)
			break;

		iov.iov_len = size;

		enum_arg.kds = kds;
		enum_arg.kds_cap = KDS_NUM;
		enum_arg.kds_len = num;
		enum_arg.sgl = &sgl;
		enum_arg.sgl_idx = 1;
		enum_arg.eprs = eprs;
		enum_arg.eprs_cap = KDS_NUM;
		enum_arg.eprs_len = num;
		rc = dss_enum_unpack(VOS_ITER_DKEY, &enum_arg,
				     rebuild_one_queue_cb, arg);
		if (rc) {
			D_ERROR("rebuild "DF_UOID" failed: %d\n",
				DP_UOID(arg->oid), rc);
			break;
		}

		if (daos_anchor_is_eof(&dkey_anchor))
			break;
	}

	ds_obj_close(oh);
free:
	tls->rebuild_pool_obj_count++;
	if (tls->rebuild_pool_status == 0 && rc < 0)
		tls->rebuild_pool_status = rc;
	D_DEBUG(DB_REBUILD, "stop rebuild obj "DF_UOID" for shard %u rc %d\n",
		DP_UOID(arg->oid), arg->shard, rc);
	rpt_put(arg->rpt);
	D_FREE_PTR(arg);
}

static int
rebuild_obj_callback(daos_unit_oid_t oid, daos_epoch_t eph, unsigned int shard,
		     void *data)
{
	struct puller_iter_arg		*iter_arg = data;
	struct rebuild_iter_obj_arg	*obj_arg;
	unsigned int			stream_id;
	int				rc;

	D_ALLOC_PTR(obj_arg);
	if (obj_arg == NULL)
		return -DER_NOMEM;

	obj_arg->oid = oid;
	obj_arg->epoch = eph;
	obj_arg->shard = shard;
	obj_arg->cont_hdl = iter_arg->cont_hdl;
	uuid_copy(obj_arg->cont_uuid, iter_arg->cont_uuid);
	rpt_get(iter_arg->rpt);
	obj_arg->rpt = iter_arg->rpt;
	obj_arg->rpt->rt_toberb_objs++;

	/* Let's iterate the object on different xstream */
	stream_id = oid.id_pub.lo % dss_get_threads_number();
	rc = dss_ult_create(rebuild_obj_ult, obj_arg, stream_id,
			    PULLER_STACK_SIZE, NULL);
	if (rc) {
		rpt_put(iter_arg->rpt);
		D_FREE_PTR(obj_arg);
	}

	return rc;
}

#define DEFAULT_YIELD_FREQ			128
static int
puller_obj_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		   daos_iov_t *val_iov, void *data)
{
	struct puller_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	struct rebuild_obj_key		*key = key_iov->iov_buf;
	daos_unit_oid_t			oid = key->oid;
	daos_epoch_t			epoch = key->eph;
	unsigned int			*shard = val_iov->iov_buf;
	bool				scheduled = false;
	int				rc;

	D_DEBUG(DB_REBUILD, "obj rebuild "DF_UUID"/"DF_UOID" %"PRIx64
		" eph "DF_U64" start\n", DP_UUID(arg->cont_uuid), DP_UOID(oid),
		ih.cookie, epoch);
	D_ASSERT(arg->obj_cb != NULL);

	/* NB: if rebuild for this obj fail, let's continue rebuilding
	 * other objs, and rebuild this obj again later.
	 */
	rc = arg->obj_cb(oid, epoch, *shard, arg);
	if (rc == 0) {
		scheduled = true;
		--arg->yield_freq;
	} else {
		D_ERROR("obj "DF_UOID" cb callback rc %d\n",
			DP_UOID(oid), rc);
	}

	/* possibly get more req in case of reply lost */
	if (scheduled) {
		rc = dbtree_iter_delete(ih, NULL);
		if (rc)
			return rc;

		if (arg->yield_freq == 0) {
			arg->yield_freq = DEFAULT_YIELD_FREQ;
			ABT_thread_yield();
			arg->yielded = true;
			if (arg->cont_root->count > arg->obj_cnt) {
				arg->obj_cnt = arg->cont_root->count;
				/* re-iterate after new oid inserted */
				arg->re_iter = true;
				return 1;
			}
		}

		/* re-probe the dbtree after deletion */
		rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
		if (rc == 0) {
			arg->re_iter = true;
			return 0;
		} else if (rc == -DER_NONEXIST) {
			arg->re_iter = false;
			return 1;
		} else {
			return rc;
		}
	}

	if (rpt->rt_abort) {
		arg->re_iter = false;
		return 1;
	}

	return 0;
}

static int
puller_cont_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		    daos_iov_t *val_iov, void *data)
{
	struct rebuild_root		*root = val_iov->iov_buf;
	struct puller_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker	*rpt = arg->rpt;
	struct rebuild_pool_tls		*tls;
	daos_handle_t			coh = DAOS_HDL_INVAL;
	int				rc;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	D_DEBUG(DB_REBUILD, "iter cont "DF_UUID"/%"PRIx64" %"PRIx64" start\n",
		DP_UUID(arg->cont_uuid), ih.cookie, root->root_hdl.cookie);

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
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

	arg->cont_hdl	= coh;
	arg->yield_freq	= DEFAULT_YIELD_FREQ;
	arg->obj_cnt	= root->count;
	arg->cont_root	= root;
	arg->yielded	= false;

	do {
		arg->re_iter = false;
		rc = dbtree_iterate(root->root_hdl, false,
				    puller_obj_iter_cb, arg);
		if (rc) {
			if (tls->rebuild_pool_status == 0 && rc < 0)
				tls->rebuild_pool_status = rc;
			D_ERROR("iterate cont "DF_UUID" failed: rc %d\n",
				DP_UUID(arg->cont_uuid), rc);
			break;
		}
	} while (arg->re_iter);

	rc = dc_cont_local_close(tls->rebuild_pool_hdl, coh);
	if (rc)
		return rc;

	D_DEBUG(DB_REBUILD, "iter cont "DF_UUID"/%"PRIx64" finish.\n",
		DP_UUID(arg->cont_uuid), ih.cookie);

	if (arg->yielded) {
		/* Some one might insert new record to the tree let's reprobe */
		rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, key_iov, NULL);
		if (rc) {
			D_ASSERT(rc != -DER_NONEXIST);
			return rc;
		}
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST || rpt->rt_abort)
		return 1;

	return rc;
}

static void
rebuild_puller_ult(void *arg)
{
	struct puller_iter_arg		*iter_arg = arg;
	struct rebuild_pool_tls		*tls;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	int				rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	while (!dbtree_is_empty(rpt->rt_tobe_rb_root_hdl)) {
		rc = dbtree_iterate(rpt->rt_tobe_rb_root_hdl, false,
				    puller_cont_iter_cb, iter_arg);
		if (rc) {
			D_ERROR("dbtree iterate fails %d\n", rc);
			if (tls->rebuild_pool_status == 0)
				tls->rebuild_pool_status = rc;
			break;
		}
	}

	D_FREE_PTR(iter_arg);
	rpt->rt_lead_puller_running = 0;
	rpt_put(rpt);
}

static int
rebuilt_btr_destory_cb(daos_handle_t ih, daos_iov_t *key_iov,
		       daos_iov_t *val_iov, void *data)
{
	struct rebuild_root		*root = val_iov->iov_buf;
	int				rc;

	rc = dbtree_destroy(root->root_hdl);
	if (rc)
		D_ERROR("dbtree_destroy, cont "DF_UUID" failed, rc %d.\n",
			DP_UUID(*(uuid_t *)key_iov->iov_buf), rc);

	return rc;
}

int
rebuilt_btr_destroy(daos_handle_t btr_hdl)
{
	int	rc;

	rc = dbtree_iterate(btr_hdl, false, rebuilt_btr_destory_cb, NULL);
	if (rc) {
		D_ERROR("dbtree iterate fails %d\n", rc);
		goto out;
	}

	rc = dbtree_destroy(btr_hdl);

out:
	return rc;
}

static int
rebuild_btr_hdl_get(struct rebuild_tgt_pool_tracker *rpt, daos_handle_t *hdl,
		    daos_handle_t *rebuilt_hdl)
{
	struct umem_attr	uma;
	int			rc;

	if (daos_handle_is_inval(rpt->rt_tobe_rb_root_hdl)) {
		memset(&uma, 0, sizeof(uma));
		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_NV, 0, 4, &uma,
					   &rpt->rt_tobe_rb_root,
					   &rpt->rt_tobe_rb_root_hdl);
		if (rc != 0) {
			D_ERROR("failed to create rebuild tree: %d\n", rc);
			return rc;
		}
	}
	*hdl = rpt->rt_tobe_rb_root_hdl;

	if (daos_handle_is_inval(rpt->rt_rebuilt_root_hdl)) {
		memset(&uma, 0, sizeof(uma));
		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_NV, 0, 4, &uma,
					   &rpt->rt_rebuilt_root,
					   &rpt->rt_rebuilt_root_hdl);
		if (rc != 0) {
			D_ERROR("failed to create rebuild tree: %d\n", rc);
			return rc;
		}
	}
	*rebuilt_hdl = rpt->rt_rebuilt_root_hdl;

	return 0;
}

/* keep at most 512K rebuilt OID records per rpt as memory limit */
#define REBUILT_MAX_OIDS_KEPT		(1024 << 9)

/* the per oid record in rebuilt btree */
struct rebuilt_oid {
	uint32_t	ro_shard;
	/*
	 * ro_req_expect - the number of pending REBUILD_OBJECTS reqs expected
	 * from alive replicas of the oid.
	 * ro_req_recv - the number of received REBUILD_OBJECTS, When it reaches
	 * ro_req_expect the record can be deleted from btree.
	 */
	uint32_t	ro_req_expect:15,
			ro_req_recv:15;
};

static int
rebuild_scheduled_obj_insert_cb(struct rebuild_root *cont_root, uuid_t co_uuid,
				daos_unit_oid_t oid, daos_epoch_t eph,
				unsigned int shard, unsigned int *cnt, int ref)
{
	struct rebuilt_oid	*roid;
	struct rebuilt_oid	roid_tmp;
	struct rebuild_obj_key	key;
	uint32_t		req_cnt;
	daos_iov_t		key_iov;
	daos_iov_t		val_iov;
	int			rc;

	/* ignore the DAOS_OBJ_REPL_MAX case for now */
	req_cnt = daos_oclass_grp_size(daos_oclass_attr_find(oid.id_pub));
	D_ASSERT(req_cnt >= 2);
	req_cnt--; /* reduce the failed one */
	if (req_cnt == 1) {
		D_DEBUG(DB_REBUILD, "ignore "DF_UOID" in cont "DF_UUID
			", total objs %d\n",
			DP_UOID(oid), DP_UUID(co_uuid), *cnt);
		return 1;
	}

	oid.id_shard = shard;
	key.oid = oid;
	key.eph = eph;
	/* Finally look up the object under the container tree */
	daos_iov_set(&key_iov, &key, sizeof(key));
	daos_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(cont_root->root_hdl, &key_iov, &val_iov);
	D_DEBUG(DB_REBUILD, "lookup "DF_UOID" in cont "DF_UUID" rc %d\n",
		DP_UOID(oid), DP_UUID(co_uuid), rc);
	if (rc == 0) {
		roid = val_iov.iov_buf;
		D_ASSERT(roid != NULL);
		D_ASSERTF(roid->ro_shard == shard, "obj "DF_UOID"/"DF_UUID
			  "shard %d mismatch with shard in tree %d.\n",
			  DP_UOID(oid), DP_UUID(co_uuid), shard,
			  roid->ro_shard);
		D_ASSERT(*cnt >= 1);
		roid->ro_req_recv += ref;
		/* possible get more req due to reply lost */
		if (roid->ro_req_recv >= roid_tmp.ro_req_expect ||
		    roid->ro_req_recv == 0) {
			rc = dbtree_delete(cont_root->root_hdl,
					   &key_iov, NULL);
			if (rc == 0) {
				*cnt -= 1;
				D_DEBUG(DB_REBUILD, "deleted "DF_UOID
					" in cont "DF_UUID", total objs %d\n",
					DP_UOID(oid), DP_UUID(co_uuid),
					*cnt);
			} else {
				D_ERROR("delete "DF_UOID" in cont "
					DF_UUID" failed rc %d.\n",
					DP_UOID(oid), DP_UUID(co_uuid),
					rc);
			}
		}
	} else if (rc == -DER_NONEXIST) {
		/* when rollback the ref, possibly no record existed
		 * for example only one alive replica.
		 */
		if (ref < 0)
			return 0;

		/* if exceed limit just ignore it - this object possibly
		 * be rebuilt multiple times.
		 */
		if (*cnt >= REBUILT_MAX_OIDS_KEPT) {
			D_DEBUG(DB_REBUILD, "ignore "DF_UOID
				" in cont "DF_UUID", total objs %d\n",
				DP_UOID(oid), DP_UUID(co_uuid), *cnt);
			return 1;
		}
		roid_tmp.ro_req_expect = req_cnt;
		roid_tmp.ro_req_recv = 1;
		roid_tmp.ro_shard = shard;
		daos_iov_set(&val_iov, &roid_tmp, sizeof(roid_tmp));
		rc = dbtree_update(cont_root->root_hdl, &key_iov, &val_iov);
		if (rc < 0) {
			D_ERROR("failed to insert "DF_UOID": rc %d\n",
				DP_UOID(oid), rc);
			D_GOTO(out, rc);
		}
		*cnt += 1;
		D_DEBUG(DB_REBUILD, "update "DF_UOID"/"DF_UUID
			", total count %d\n", DP_UOID(oid),
			DP_UUID(co_uuid), *cnt);
		return 1;
	}

out:
	return rc;
}

/* Got the object list from scanner and rebuild the objects */
void
rebuild_obj_handler(crt_rpc_t *rpc)
{
	struct rebuild_objs_in		*rebuild_in;
	struct rebuild_tgt_pool_tracker *rpt = NULL;
	struct rebuild_pool_tls		*tls;
	struct rebuild_out		*rebuild_out;
	daos_unit_oid_t			*oids;
	unsigned int			oids_count;
	daos_epoch_t			*ephs;
	unsigned int			ephs_count;
	uuid_t				*co_uuids;
	unsigned int			co_count;
	uint32_t			*shards;
	unsigned int			shards_count;
	daos_handle_t			btr_hdl;
	daos_handle_t			rebuilt_btr_hdl;
	unsigned int			i;
	int				rc;

	rebuild_in = crt_req_get(rpc);
	oids = rebuild_in->roi_oids.ca_arrays;
	oids_count = rebuild_in->roi_oids.ca_count;
	ephs = rebuild_in->roi_ephs.ca_arrays;
	ephs_count = rebuild_in->roi_ephs.ca_count;
	co_uuids = rebuild_in->roi_uuids.ca_arrays;
	co_count = rebuild_in->roi_uuids.ca_count;
	shards = rebuild_in->roi_shards.ca_arrays;
	shards_count = rebuild_in->roi_shards.ca_count;

	if (co_count == 0 || oids_count == 0 || shards_count == 0 ||
	    ephs_count == 0 || oids_count != co_count ||
	    oids_count != shards_count || oids_count != ephs_count) {
		D_ERROR("oids %u cont %u shards %u ephs %d\n",
			oids_count, co_count, shards_count, ephs_count);
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* If rpt is NULL, it means the target is not prepared for
	 * rebuilding yet, i.e. it did not receive scan req to
	 * prepare rebuild yet (see rebuild_tgt_prepare()).
	 */
	rpt = rpt_lookup(rebuild_in->roi_pool_uuid,
			 rebuild_in->roi_rebuild_ver);
	if (rpt == NULL || rpt->rt_pool == NULL)
		D_GOTO(out, rc = -DER_AGAIN);

	/* Initialize the local rebuild tree */
	rc = rebuild_btr_hdl_get(rpt, &btr_hdl, &rebuilt_btr_hdl);
	if (rc)
		D_GOTO(out_put, rc);

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	/* Insert these oids/conts into the local rebuild tree */
	for (i = 0; i < oids_count; i++) {
		/* firstly insert/check rebuilt tree */
		rc = rebuild_cont_obj_insert(rebuilt_btr_hdl, co_uuids[i],
					     oids[i], ephs[i], shards[i],
					     &rpt->rt_rebuilt_obj_cnt, 1,
					     rebuild_scheduled_obj_insert_cb);
		if (rc == 0) {
			D_DEBUG(DB_REBUILD, "already rebuilt "DF_UOID" "DF_UUID
				" shard %u.\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i]);
			continue;
		} else if (rc < 0) {
			D_ERROR("insert "DF_UOID"/"DF_U64" "DF_UUID
				" shard %u to rebuilt tree failed, rc %d.\n",
				DP_UOID(oids[i]), ephs[i],
				DP_UUID(co_uuids[i]), shards[i], rc);
			break;
		}
		D_ASSERT(rc == 1);

		/* for un-rebuilt objs insert to to-be-rebuilt tree */
		rc = rebuild_cont_obj_insert(btr_hdl, co_uuids[i],
					     oids[i], ephs[i], shards[i], NULL,
					     0, rebuild_obj_insert_cb);
		if (rc == 1) {
			D_DEBUG(DB_REBUILD, "insert local "DF_UOID"/"DF_U64" "
				DF_UUID" %u hdl %"PRIx64"\n", DP_UOID(oids[i]),
				ephs[i], DP_UUID(co_uuids[i]), shards[i],
				btr_hdl.cookie);
			rc = 0;
		} else if (rc == 0) {
			D_DEBUG(DB_REBUILD, DF_UOID"/"DF_U64" "DF_UUID
				", shard %u exist.\n", DP_UOID(oids[i]),
				ephs[i], DP_UUID(co_uuids[i]), shards[i]);
		} else {
			D_ASSERT(rc < 0);
			/* rollback the ref in rebuilt tree taken above */
			rebuild_cont_obj_insert(rebuilt_btr_hdl, co_uuids[i],
					oids[i], ephs[i], shards[i],
					&rpt->rt_rebuilt_obj_cnt, -1,
					rebuild_scheduled_obj_insert_cb);
			break;
		}
	}
	if (rc < 0)
		D_GOTO(out_put, rc);

	/* Check and create task to iterate the to-be-rebuilt tree */
	if (!rpt->rt_lead_puller_running) {
		struct puller_iter_arg *arg;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out_put, rc = -DER_NOMEM);

		arg->obj_cb = rebuild_obj_callback;
		rpt_get(rpt);
		arg->rpt = rpt;

		rpt->rt_lead_puller_running = 1;
		D_ASSERT(rpt->rt_pullers != NULL);
		rc = dss_ult_create(rebuild_puller_ult, arg, -1, 0, NULL);
		if (rc) {
			rpt_put(rpt);
			D_FREE_PTR(arg);
			rpt->rt_lead_puller_running = 0;
			D_GOTO(out_put, rc);
		}
	}
out_put:
	rpt_put(rpt);
out:
	rebuild_out = crt_reply_get(rpc);
	rebuild_out->ro_status = rc;
	dss_rpc_reply(rpc, DAOS_REBUILD_DROP_OBJ);
}
