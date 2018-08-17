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

typedef int (*rebuild_obj_iter_cb_t)(daos_unit_oid_t oid, unsigned int shard,
				     void *arg);

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
	int		i;
	int		rc;

	D_ASSERT(rdone->ro_iod_num <= MAX_IOD_NUM);
	for (i = 0; i < rdone->ro_iod_num; i++) {
		daos_iov_set(&iov[i], iov_buf[i], MAX_BUF_SIZE);
		sgls[i].sg_nr = 1;
		sgls[i].sg_nr_out = 1;
		sgls[i].sg_iovs = &iov[i];
	}

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %.*s nr %d eph "DF_U64"\n",
		DP_UOID(rdone->ro_oid), rdone, (int)rdone->ro_dkey.iov_len,
		(char *)rdone->ro_dkey.iov_buf, rdone->ro_iod_num,
		rdone->ro_epoch);
	rc = ds_obj_fetch(oh, rdone->ro_epoch, &rdone->ro_dkey,
			  rdone->ro_iod_num, rdone->ro_iods,
			  sgls, NULL);
	if (rc) {
		D_ERROR("ds_obj_fetch %d\n", rc);
		return rc;
	}

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

	data_size = daos_iods_len(rdone->ro_iods, rdone->ro_iod_num);
	D_ASSERT(data_size != (uint64_t)(-1));
	if (data_size < MAX_BUF_SIZE)
		rc = rebuild_fetch_update_inline(rdone, oh, rebuild_cont);
	else
		rc = rebuild_fetch_update_bulk(rdone, oh, rebuild_cont);

	tls->rebuild_pool_rec_count += rdone->ro_rec_cnt;
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
	D_ASSERT(d_list_empty(&rdone->ro_list));
	daos_iov_free(&rdone->ro_dkey);

	if (rdone->ro_iods) {
		int i;

		for (i = 0; i < rdone->ro_iod_num; i++) {
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

/**
 * queue dkey to the rebuild dkey list on each xstream.
 */
static int
rebuild_one_queue(struct rebuild_iter_obj_arg *iter_arg, daos_unit_oid_t oid,
		  daos_key_t *dkey, daos_iod_t *iods, int iod_num,
		  uuid_t cookie, uint32_t *version)
{
	struct rebuild_puller		*puller;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	struct rebuild_one		*rdone = NULL;
	unsigned int			idx;
	unsigned int			rec_cnt = 0;
	daos_epoch_t			min_epoch = 0;
	int				i;
	int				rc;

	D_DEBUG(DB_REBUILD, "rebuild dkey %.*s iod nr %d\n",
		(int)dkey->iov_buf_len, (char *)dkey->iov_buf, iod_num);

	if (iod_num == 0)
		return 0;

	D_ALLOC_PTR(rdone);
	if (rdone == NULL)
		return -DER_NOMEM;

	D_ALLOC(rdone->ro_iods, iod_num * sizeof(*rdone->ro_iods));
	if (rdone->ro_iods == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	for (i = 0; i < iod_num; i++) {
		int j;

		if (iods[i].iod_nr == 0)
			continue;

		rc = daos_iov_copy(&rdone->ro_iods[i].iod_name,
				   &iods[i].iod_name);
		if (rc)
			D_GOTO(free, rc);

		rdone->ro_iods[i].iod_kcsum = iods[i].iod_kcsum;
		rdone->ro_iods[i].iod_type = iods[i].iod_type;
		rdone->ro_iods[i].iod_size = iods[i].iod_size;
		rdone->ro_iods[i].iod_nr = iods[i].iod_nr;
		rdone->ro_iods[i].iod_recxs = iods[i].iod_recxs;
		rdone->ro_iods[i].iod_csums = iods[i].iod_csums;
		rdone->ro_iods[i].iod_eprs = iods[i].iod_eprs;

		for (j = 0; j < iods[i].iod_nr; j++) {
			rec_cnt += iods[i].iod_recxs[j].rx_nr;
			if (min_epoch == 0 ||
			    iods[i].iod_eprs[j].epr_lo < min_epoch)
				min_epoch = iods[i].iod_eprs[j].epr_lo;
		}

		D_DEBUG(DB_REBUILD, "idx %d akey %.*s nr %d size "
			DF_U64" type %d eph "DF_U64"/"DF_U64"\n",
			i, (int)iods[i].iod_name.iov_len,
			(char *)iods[i].iod_name.iov_buf, iods[i].iod_nr,
			iods[i].iod_size, iods[i].iod_type,
			iods[i].iod_eprs->epr_lo, iods[i].iod_eprs->epr_hi);

		rdone->ro_iod_num++;
	}

	if (rdone->ro_iod_num == 0)
		D_GOTO(free, rc = 0);

	rdone->ro_epoch = min_epoch;
	rdone->ro_rec_cnt = rec_cnt;
	rdone->ro_version = *version;
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

	rdone->ro_oid = oid;
	uuid_copy(rdone->ro_cont_uuid, iter_arg->cont_uuid);

	D_DEBUG(DB_REBUILD, DF_UOID" %p dkey %.*s rebuild on idx %d\n",
		DP_UOID(oid), rdone, (int)dkey->iov_len, (char *)dkey->iov_buf,
		idx);
	ABT_mutex_lock(puller->rp_lock);
	d_list_add_tail(&rdone->ro_list, &puller->rp_one_list);
	ABT_mutex_unlock(puller->rp_lock);
free:
	if (rc == 0) {
		/* Reset iods/cookie/version after queuing rebuild job,
		 * so in the following iods_packing, it will check different
		 * version/cookie correctly see rebuild_list_buf_process().
		 */
		for (i = 0; i < iod_num; i++)
			daos_iov_free(&iods[i].iod_name);
		memset(iods, 0, iod_num * sizeof(*iods));
		uuid_clear(cookie);
		*version = 0;
	}

	if (rc != 0 && rdone != NULL)
		rebuild_one_destroy(rdone);

	return rc;
}

static int
rebuild_iod_pack(daos_iod_t *iod, daos_key_t *akey, daos_key_desc_t *kds,
		 void **data, uuid_t cookie, uint32_t *version, int count)
{
	struct obj_enum_rec	*rec = *data;
	int			i;
	int			rc = 0;

	if (iod->iod_name.iov_len == 0)
		daos_iov_copy(&iod->iod_name, akey);
	else
		D_ASSERT(daos_key_match(&iod->iod_name, akey));

	iod->iod_recxs = realloc(iod->iod_recxs, (count + iod->iod_nr) *
						 sizeof(*iod->iod_recxs));
	if (iod->iod_recxs == NULL)
		return -DER_NOMEM;

	iod->iod_eprs = realloc(iod->iod_eprs, (count + iod->iod_nr) *
						sizeof(*iod->iod_eprs));
	if (iod->iod_eprs == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	/* check if the cookie is same */
	for (i = 0; i < count; i++) {
		int idx = i + iod->iod_nr;

		if (uuid_is_null(cookie)) {
			uuid_copy(cookie, rec[i].rec_cookie);
			*version = rec[i].rec_version;
		} else if (uuid_compare(cookie, rec[i].rec_cookie) != 0 ||
			   *version != rec[i].rec_version) {
			D_DEBUG(DB_REBUILD, "different cookie or version"
				DF_UUIDF" "DF_UUIDF" %u != %u\n",
				DP_UUID(cookie), DP_UUID(rec[i].rec_cookie),
				*version, rec[i].rec_version);
			break;
		}

		/* Iteration might return multiple single record with same
		 * dkey/akeks but different epoch. But fetch & update only
		 * allow 1 SINGLE type record per IOD. Let's put these
		 * single records in different IODs.
		 */
		if (kds->kd_val_types == VOS_ITER_SINGLE && i > 0)
			break;

		if (iod->iod_size != 0 && iod->iod_size != rec[i].rec_size)
			D_WARN("rsize "DF_U64" != "DF_U64" are different"
			       " under one akey\n", iod->iod_size,
			       rec[i].rec_size);

		iod->iod_eprs[idx] = rec[i].rec_epr;
		/* Iteration does not fill the high epoch, so let's reset
		 * the high epoch with EPOCH_MAX to make vos fetch/update happy.
		 */
		iod->iod_eprs[idx].epr_hi = DAOS_EPOCH_MAX;
		iod->iod_recxs[idx] = rec[i].rec_recx;
		if (iod->iod_size == 0)
			iod->iod_size = rec[i].rec_size;

		D_DEBUG(DB_REBUILD, "pack %d idx/nr "DF_U64"/"DF_U64
			"epr lo/hi "DF_U64"/"DF_U64" size %zd\n", i,
			iod->iod_recxs[idx].rx_idx, iod->iod_recxs[idx].rx_nr,
			iod->iod_eprs[idx].epr_lo, iod->iod_eprs[idx].epr_hi,
			iod->iod_size);
	}

	if (kds->kd_val_types == VOS_ITER_RECX)
		iod->iod_type = DAOS_IOD_ARRAY;
	else
		iod->iod_type = DAOS_IOD_SINGLE;

	iod->iod_nr = i + iod->iod_nr;

	*data = &rec[i];

	rc = i;
	D_DEBUG(DB_REBUILD, "pack nr %d total %d cookie/version "DF_UUID"/"
		"/%u packed %d\n", iod->iod_nr, count, DP_UUID(cookie),
		*version, rc);
free:
	if (rc < 0) {
		if (iod->iod_eprs)
			D_FREE(iod->iod_eprs);
		if (iod->iod_recxs)
			D_FREE(iod->iod_recxs);
	}

	return rc;
}

static int
rebuild_list_buf_process(daos_unit_oid_t oid, daos_epoch_t epoch,
			 daos_iov_t *iov, daos_key_desc_t *kds,
			 int num, struct rebuild_iter_obj_arg *iter_arg,
			 daos_key_t *dkey, daos_iod_t *iods, int *iod_idx,
			 uuid_t cookie, uint32_t *version)
{
	daos_key_t		akey = {0};
	void			*ptr;
	int			rc = 0;
	unsigned int		i;

	if (kds[0].kd_val_types != VOS_ITER_DKEY) {
		D_ERROR("the first kds type %d != DKEY\n", kds[0].kd_val_types);
		return -DER_INVAL;
	}

	ptr = iov->iov_buf;
	for (i = 0; i < num; i++) {
		D_DEBUG(DB_REBUILD, DF_UOID" process %d type %d len "DF_U64
			" total %zd\n", DP_UOID(oid), i, kds[i].kd_val_types,
			kds[i].kd_key_len, iov->iov_len);

		D_ASSERT(kds[i].kd_key_len > 0);
		if (kds[i].kd_val_types == VOS_ITER_DKEY) {
			d_iov_t	 tmp_iov;

			tmp_iov.iov_buf = ptr;
			tmp_iov.iov_buf_len = kds[i].kd_key_len;
			tmp_iov.iov_len = kds[i].kd_key_len;
			if (dkey->iov_len == 0) {
				daos_iov_copy(dkey, &tmp_iov);
			} else if (dkey->iov_len != kds[i].kd_key_len ||
				   memcmp(dkey->iov_buf, ptr,
					  dkey->iov_len) != 0) {
				rc = rebuild_one_queue(iter_arg, oid, dkey,
						       iods, *iod_idx + 1,
						       cookie, version);
				if (rc)
					break;
				*iod_idx = 0;
				daos_iov_free(dkey);
				daos_iov_copy(dkey, &tmp_iov);
			}
			D_DEBUG(DB_REBUILD, "process dkey %.*s\n",
				(int)dkey->iov_len, (char *)dkey->iov_buf);
		} else if (kds[i].kd_val_types == VOS_ITER_AKEY) {
			akey.iov_buf = ptr;
			akey.iov_buf_len = kds[i].kd_key_len;
			akey.iov_len = kds[i].kd_key_len;
			if (dkey->iov_buf == NULL) {
				D_ERROR("No dkey for akey %*.s invalid buf.\n",
				      (int)akey.iov_len, (char *)akey.iov_buf);
				rc = -DER_INVAL;
				break;
			}
			D_DEBUG(DB_REBUILD, "process akey %.*s\n",
				(int)akey.iov_len, (char *)akey.iov_buf);
			if (iods[*iod_idx].iod_name.iov_len != 0 &&
			    !daos_key_match(&iods[*iod_idx].iod_name, &akey)) {
				(*iod_idx)++;
				if (*iod_idx >= MAX_IOD_NUM) {
					rc = rebuild_one_queue(iter_arg, oid,
							       dkey, iods,
							       *iod_idx,
							       cookie, version);
					if (rc < 0)
						D_GOTO(out, rc);

					*iod_idx = 0;
				}
			}
		} else if (kds[i].kd_val_types == VOS_ITER_SINGLE ||
			   kds[i].kd_val_types == VOS_ITER_RECX) {
			int total_cnt = kds[i].kd_key_len /
					sizeof(struct obj_enum_rec);
			void *data = ptr;

			if (dkey->iov_len == 0 || akey.iov_len == 0) {
				D_ERROR("invalid list buf for kds %d\n", i);
				rc = -DER_INVAL;
				break;
			}

			while (total_cnt > 0) {
				int packed_cnt;
				/* Because vos_obj_update only accept single
				 * cookie/version, let's go through the records
				 * to check different cookie and version, and
				 * queue rebuild.
				 */
				packed_cnt = rebuild_iod_pack(&iods[*iod_idx],
							&akey, &kds[i], &data,
							cookie, version,
							total_cnt);
				if (packed_cnt < 0)
					D_GOTO(out, rc = packed_cnt);

				/* All records referred by this kds has been
				 * packed, then it does not need to send
				 * right away, might pack more next round.
				 */
				if (packed_cnt == total_cnt)
					break;

				/* Otherwise let's queue current iods, and go
				 * next round.
				 */
				rc = rebuild_one_queue(iter_arg, oid, dkey,
						       iods, *iod_idx + 1,
						       cookie, version);
				if (rc < 0)
					D_GOTO(out, rc);
				*iod_idx = 0;
				total_cnt -= packed_cnt;
			}
		} else {
			D_ERROR("unknow kds type %d\n", kds[i].kd_val_types);
			rc = -DER_INVAL;
			break;
		}
		ptr += kds[i].kd_key_len;
	}

	D_DEBUG(DB_REBUILD, "process list buf "DF_UOID" rc %d\n",
		DP_UOID(oid), rc);
out:
	return rc;
}

#define KDS_NUM		16
#define ITER_BUF_SIZE   2048

/**
 * Iterate akeys/dkeys of the object
 */
static void
rebuild_obj_ult(void *data)
{
	struct rebuild_iter_obj_arg *arg = data;
	struct rebuild_pool_tls	    *tls;
	daos_hash_out_t		hash;
	daos_hash_out_t		dkey_hash;
	daos_hash_out_t		akey_hash;
	daos_handle_t		oh;
	daos_epoch_t		epoch = DAOS_EPOCH_MAX;
	daos_sg_list_t		sgl = { 0 };
	daos_iov_t		iov = { 0 };
	daos_iod_t		iods[MAX_IOD_NUM] = { 0 };
	int			iod_idx = 0;
	daos_key_t		dkey = { 0 };
	char			buf[ITER_BUF_SIZE];
	uuid_t			cookie;
	uint32_t		version = 0;
	int			rc;

	tls = rebuild_pool_tls_lookup(arg->rpt->rt_pool_uuid,
				      arg->rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	rc = ds_obj_open(arg->cont_hdl, arg->oid.id_pub, epoch, DAOS_OO_RW,
			 &oh);
	if (rc)
		D_GOTO(free, rc);

	D_DEBUG(DB_REBUILD, "start rebuild obj "DF_UOID" for shard %u\n",
		DP_UOID(arg->oid), arg->shard);
	memset(&hash, 0, sizeof(hash));
	memset(&dkey_hash, 0, sizeof(dkey_hash));
	memset(&akey_hash, 0, sizeof(akey_hash));
	uuid_clear(cookie);
	dc_obj_shard2anchor(&hash, arg->shard);

	while (1) {
		daos_key_desc_t	kds[KDS_NUM] = { 0 };
		uint32_t	num = KDS_NUM;
		daos_size_t	size;

		memset(buf, 0, ITER_BUF_SIZE);
		iov.iov_len = 0;
		iov.iov_buf = buf;
		iov.iov_buf_len = ITER_BUF_SIZE;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;

		rc = ds_obj_list_obj(oh, epoch, NULL, NULL, &size, &num, kds,
				     &sgl, &hash, &dkey_hash, &akey_hash);
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
		rc = rebuild_list_buf_process(arg->oid, epoch, &iov, kds, num,
					      arg, &dkey, iods, &iod_idx,
					      cookie, &version);
		if (rc) {
			D_ERROR("rebuild "DF_UOID" failed: %d\n",
				DP_UOID(arg->oid), rc);
			break;
		}

		if (daos_hash_is_eof(&dkey_hash))
			break;
	}

	if (iods[0].iod_nr > 0) {
		rc = rebuild_one_queue(arg, arg->oid, &dkey, iods,
				       iod_idx + 1, cookie, &version);
		if (rc < 0)
			D_GOTO(free, rc);
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
rebuild_obj_callback(daos_unit_oid_t oid, unsigned int shard, void *data)
{
	struct puller_iter_arg		*iter_arg = data;
	struct rebuild_iter_obj_arg	*obj_arg;
	unsigned int			stream_id;
	int				rc;

	D_ALLOC_PTR(obj_arg);
	if (obj_arg == NULL)
		return -DER_NOMEM;

	obj_arg->oid = oid;
	obj_arg->shard = shard;
	obj_arg->cont_hdl = iter_arg->cont_hdl;
	uuid_copy(obj_arg->cont_uuid, iter_arg->cont_uuid);
	rpt_get(iter_arg->rpt);
	obj_arg->rpt = iter_arg->rpt;
	obj_arg->rpt->rt_rebuilding_objs++;

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
	daos_unit_oid_t			*oid = key_iov->iov_buf;
	unsigned int			*shard = val_iov->iov_buf;
	bool				 scheduled = false;
	int				 rc;

	D_DEBUG(DB_REBUILD, "obj rebuild "DF_UUID"/"DF_UOID" %"PRIx64
		" start\n", DP_UUID(arg->cont_uuid), DP_UOID(*oid),
		ih.cookie);
	D_ASSERT(arg->obj_cb != NULL);

	/* NB: if rebuild for this obj fail, let's continue rebuilding
	 * other objs, and rebuild this obj again later.
	 */
	rc = arg->obj_cb(*oid, *shard, arg);
	if (rc == 0) {
		scheduled = true;
		--arg->yield_freq;
	} else {
		D_ERROR("obj "DF_UOID" cb callback rc %d\n",
			DP_UOID(*oid), rc);
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
				daos_unit_oid_t oid, unsigned int shard,
				unsigned int *cnt, int ref)
{
	struct rebuilt_oid	*roid;
	struct rebuilt_oid	roid_tmp;
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
	/* Finally look up the object under the container tree */
	daos_iov_set(&key_iov, &oid, sizeof(oid));
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
	co_uuids = rebuild_in->roi_uuids.ca_arrays;
	co_count = rebuild_in->roi_uuids.ca_count;
	shards = rebuild_in->roi_shards.ca_arrays;
	shards_count = rebuild_in->roi_shards.ca_count;

	if (co_count == 0 || oids_count == 0 || shards_count == 0 ||
	    oids_count != co_count || oids_count != shards_count) {
		D_ERROR("oids_count %u co_count %u shards_count %u\n",
			oids_count, co_count, shards_count);
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
					     oids[i], shards[i],
					     &rpt->rt_rebuilt_obj_cnt, 1,
					     rebuild_scheduled_obj_insert_cb);
		if (rc == 0) {
			D_DEBUG(DB_REBUILD, "already rebuilt "DF_UOID" "DF_UUID
				" shard %u.\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i]);
			continue;
		} else if (rc < 0) {
			D_ERROR("insert "DF_UOID" "DF_UUID" shard %u to rebuilt"
				" tree failed, rc %d.\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i], rc);
			break;
		}
		D_ASSERT(rc == 1);

		/* for un-rebuilt objs insert to to-be-rebuilt tree */
		rc = rebuild_cont_obj_insert(btr_hdl, co_uuids[i],
					     oids[i], shards[i], NULL, 0,
					     rebuild_obj_insert_cb);
		if (rc == 1) {
			D_DEBUG(DB_REBUILD, "insert local "DF_UOID" "DF_UUID
				" %u hdl %"PRIx64"\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i],
				btr_hdl.cookie);
			rc = 0;
		} else if (rc == 0) {
			D_DEBUG(DB_REBUILD, DF_UOID" "DF_UUID", shard %u "
				"exist.\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i]);
		} else {
			D_ASSERT(rc < 0);
			/* rollback the ref in rebuilt tree taken above */
			rebuild_cont_obj_insert(rebuilt_btr_hdl, co_uuids[i],
					oids[i], shards[i],
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
