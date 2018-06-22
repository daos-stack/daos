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
struct rebuild_iter_arg {
	uuid_t			cont_uuid;
	struct rebuild_tgt_pool_tracker *rpt;
	rebuild_obj_iter_cb_t	obj_cb;
	daos_handle_t		cont_hdl;
	int			yield_freq;
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

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %.*s nr %d\n",
		DP_UOID(rdone->ro_oid), rdone, (int)rdone->ro_dkey.iov_len,
		(char *)rdone->ro_dkey.iov_buf, rdone->ro_iod_num);
	rc = ds_obj_fetch(oh, rdone->ro_epoch, &rdone->ro_dkey,
			  rdone->ro_iod_num, rdone->ro_iods,
			  sgls, NULL);
	if (rc)
		return rc;

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_UPDATE_FAIL))
		return -DER_INVAL;

	rc = vos_obj_update(ds_cont->sc_hdl, rdone->ro_oid, 0,
			    rdone->ro_cookie, rdone->ro_version,
			    &rdone->ro_dkey, rdone->ro_iod_num,
			    rdone->ro_iods, sgls);
	return rc;
}

static int
rebuild_fetch_update_bulk(struct rebuild_one *rdone, daos_handle_t oh,
			  struct ds_cont *ds_cont)
{
	daos_sg_list_t	sgls[MAX_IOD_NUM];
	daos_handle_t	ioh;
	int		i;
	int		rc;

	D_ASSERT(rdone->ro_iod_num <= MAX_IOD_NUM);
	rc = vos_update_begin(ds_cont->sc_hdl, rdone->ro_oid, 0,
			      &rdone->ro_dkey, rdone->ro_iod_num,
			      rdone->ro_iods, &ioh);
	if (rc != 0) {
		D_ERROR(DF_UOID"preparing update fails: %d\n",
			DP_UOID(rdone->ro_oid), rc);
		return rc;
	}

	for (i = 0; i < rdone->ro_iod_num; i++) {
		daos_sg_list_t *sgl;

		rc = vos_obj_zc_sgl_at(ioh, i, &sgl);
		if (rc)
			D_GOTO(end, rc);

		memcpy(&sgls[i], sgl, sizeof(*sgl));
	}

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %.*s nr %d\n",
		DP_UOID(rdone->ro_oid), rdone, (int)rdone->ro_dkey.iov_len,
		(char *)rdone->ro_dkey.iov_buf, rdone->ro_iod_num);

	rc = ds_obj_fetch(oh, rdone->ro_epoch, &rdone->ro_dkey,
			  rdone->ro_iod_num, rdone->ro_iods,
			  sgls, NULL);
	if (rc) {
		D_ERROR("rebuild dkey %.*s failed rc %d\n",
			(int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf, rc);
		D_GOTO(end, rc);
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
		  uuid_t *cookie, uint64_t *version)
{
	struct rebuild_puller		*puller;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	struct rebuild_one		*rdone = NULL;
	unsigned int			idx;
	unsigned int			rec_cnt = 0;
	int				i;
	int				rc;

	D_DEBUG(DB_REBUILD, "rebuild dkey %.*s iod nr %d\n",
		(int)dkey->iov_buf_len, (char *)dkey->iov_buf, iod_num);

	if (iods->iod_nr == 0)
		return 0;

	D_ALLOC_PTR(rdone);
	if (rdone == NULL)
		return -DER_NOMEM;

	rdone->ro_iod_num = iod_num;
	D_ALLOC(rdone->ro_iods, iod_num * sizeof(*rdone->ro_iods));
	if (rdone->ro_iods == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	for (i = 0; i < iod_num; i++) {
		int j;

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

		for (j = 0; j < iods[i].iod_nr; j++)
			rec_cnt += iods[i].iod_recxs[j].rx_nr;

		D_DEBUG(DB_REBUILD, "rebuild akey %.*s nr %d size "
			DF_U64" type %d\n", (int)iods[i].iod_name.iov_len,
			(char *)iods[i].iod_name.iov_buf, iods[i].iod_nr,
			iods[i].iod_size, iods[i].iod_type);
	}

	rdone->ro_rec_cnt = rec_cnt;
	rdone->ro_version = *version;
	uuid_copy(rdone->ro_cookie, *cookie);
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
	rdone->ro_epoch = DAOS_EPOCH_MAX;

	D_DEBUG(DB_REBUILD, DF_UOID"%p dkey %.*s rebuild on idx %d\n",
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
		uuid_clear(*cookie);
		*version = 0;
	}

	if (rc != 0 && rdone != NULL)
		rebuild_one_destroy(rdone);

	return rc;
}

static int
rebuild_iod_pack(daos_iod_t *iod, daos_key_t *akey, daos_key_desc_t *kds,
		 void **data, uuid_t *cookie, uint64_t *version)
{
	struct obj_enum_rec	*rec = *data;
	unsigned int		count;
	int			i;
	int			rc = 0;

	if (iod->iod_name.iov_len == 0)
		daos_iov_copy(&iod->iod_name, akey);
	else
		D_ASSERT(daos_key_match(&iod->iod_name, akey));

	count = kds->kd_key_len / sizeof(*rec);
	iod->iod_recxs = realloc(iod->iod_recxs,
				(count + iod->iod_nr) *
				 sizeof(*iod->iod_recxs));
	if (iod->iod_recxs == NULL)
		return -DER_NOMEM;

	iod->iod_eprs = realloc(iod->iod_eprs,
				(count + iod->iod_nr) *
				 sizeof(*iod->iod_eprs));
	if (iod->iod_eprs == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	/* check if the cookie is same */
	for (i = 0; i < count; i++) {
		int idx = i + iod->iod_nr;

		if (uuid_is_null(*cookie)) {
			uuid_copy(*cookie, rec[i].rec_cookie);
			*version = rec[i].rec_version;
		} else if (uuid_compare(*cookie, rec[i].rec_cookie) != 0 ||
			   *version != rec[i].rec_version) {
			D_DEBUG(DB_REBUILD, "different cookie or version"
				DF_UUIDF" "DF_UUIDF" "DF_U64" != "DF_U64"\n",
				DP_UUID(*cookie), DP_UUID(rec[i].rec_cookie),
				*version, rec[i].rec_version);
			rc = 1;
			break;
		}

		if (iod->iod_size != 0 && iod->iod_size != rec[i].rec_size)
			D_WARN("rsize "DF_U64" != "DF_U64" are different"
			       " under one akey\n", iod->iod_size,
			       rec[i].rec_size);

		iod->iod_eprs[idx] = rec[i].rec_epr;
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

	D_DEBUG(DB_REBUILD, "pack nr %d total %d cookie/version "DF_UUID"/"
		DF_U64" rc %d\n", iod->iod_nr, count, DP_UUID(*cookie),
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
			 uuid_t *cookie, uint64_t *version)
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
			if (dkey->iov_len == 0 || akey.iov_len == 0) {
				D_ERROR("invalid list buf for kds %d\n", i);
				rc = -DER_INVAL;
				break;
			}

			while (1) {
				void *data = ptr;

				/* Because vos_obj_update only accept single
				 * cookie/version, let's go through the records
				 * to check different cookie and version, and
				 * queue rebuild.
				 */
				rc = rebuild_iod_pack(&iods[*iod_idx],
						      &akey, &kds[i], &data,
						      cookie, version);
				if (rc == 0)
					/* Nice. No diff cookie and version */
					break;

				if (rc != 1)
					D_GOTO(out, rc);

				rc = rebuild_one_queue(iter_arg, oid, dkey,
						       iods, *iod_idx + 1,
						       cookie, version);
				if (rc < 0)
					D_GOTO(out, rc);
				*iod_idx = 0;
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
	uint64_t		version = 0;
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
					      &cookie, &version);
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
				       iod_idx + 1, &cookie, &version);
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
	struct rebuild_iter_arg		*iter_arg = data;
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

static int
rebuild_obj_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		    daos_iov_t *val_iov, void *data)
{
	struct rebuild_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	daos_unit_oid_t			*oid = key_iov->iov_buf;
	unsigned int			*shard = val_iov->iov_buf;
	int				rc;

	D_DEBUG(DB_REBUILD, "obj rebuild "DF_UUID"/"DF_UOID" %"PRIx64
		" start\n", DP_UUID(arg->cont_uuid), DP_UOID(*oid),
		ih.cookie);
	D_ASSERT(arg->obj_cb != NULL);

	/* NB: if rebuild for this object fail, let's continue rebuilding
	 * other objects, anyway the failure will be remembered in
	 * tls_pool_status.
	 */
	rc = arg->obj_cb(*oid, *shard, arg);
	if (rc)
		D_DEBUG(DB_REBUILD, "obj "DF_UOID" cb callback rc %d\n",
			DP_UOID(*oid), rc);

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	/* re-probe the dbtree after deletion */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	if (--arg->yield_freq == 0 || rpt->rt_abort)
		return 1;

	return 0;
}

#define DEFAULT_YIELD_FREQ 100

static int
rebuild_cont_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		     daos_iov_t *val_iov, void *data)
{
	struct rebuild_root		*root = val_iov->iov_buf;
	struct rebuild_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker	*rpt = arg->rpt;
	struct rebuild_pool_tls		*tls;
	daos_handle_t			coh = DAOS_HDL_INVAL;
	int				rc;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	D_DEBUG(DB_REBUILD, "iter cont "DF_UUID"/%"PRIx64" %"PRIx64" start\n",
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

	arg->yield_freq = DEFAULT_YIELD_FREQ;
	while (!dbtree_is_empty(root->root_hdl)) {
		rc = dbtree_iterate(root->root_hdl, false,
				    rebuild_obj_iter_cb, arg);
		if (rc) {
			if (tls->rebuild_pool_status == 0 && rc < 0)
				tls->rebuild_pool_status = rc;
			D_ERROR("iterate cont "DF_UUID" failed: rc %d\n",
				DP_UUID(arg->cont_uuid), rc);

			break;
		}

		if (rpt->rt_abort)
			break;

		if (arg->yield_freq == 0) {
			ABT_thread_yield();
			/* re-probe the dbtree */
			rc = dbtree_iter_probe(root->root_hdl,
					       BTR_PROBE_FIRST,
					       NULL, NULL);
			if (rc == -DER_NONEXIST)
				break;
			arg->yield_freq = DEFAULT_YIELD_FREQ;
		}
	}

	rc = dc_cont_local_close(tls->rebuild_pool_hdl, coh);
	if (rc)
		return rc;

	D_DEBUG(DB_REBUILD, "iter cont "DF_UUID"/%"PRIx64" finish.\n",
		DP_UUID(arg->cont_uuid), ih.cookie);

	/* Some one might insert new record to the tree let's reprobe */
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, key_iov, NULL);
	if (rc) {
		D_ASSERT(rc != -DER_NONEXIST);
		return rc;
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
rebuild_puller(void *arg)
{
	struct rebuild_pool_tls		*tls;
	struct rebuild_iter_arg		*iter_arg = arg;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	int				rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	while (!dbtree_is_empty(rpt->rt_local_root_hdl)) {
		rc = dbtree_iterate(rpt->rt_local_root_hdl, false,
				    rebuild_cont_iter_cb, iter_arg);
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
		D_ERROR("failed to create rebuild tree: %d\n", rc);
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
	struct rebuild_pool_tls		*tls;
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
	rc = rebuild_obj_hdl_get(rpt, &btr_hdl);
	if (rc)
		D_GOTO(out_put, rc);

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	/* Insert these oids/conts into the local rebuild tree */
	for (i = 0; i < oids_count; i++) {
		rc = rebuild_cont_obj_insert(btr_hdl, co_uuids[i],
					     oids[i], shards[i]);
		if (rc == 1) {
			D_DEBUG(DB_REBUILD, "insert local "DF_UOID" "DF_UUID
				" %u hdl %"PRIx64"\n", DP_UOID(oids[i]),
				DP_UUID(co_uuids[i]), shards[i],
				btr_hdl.cookie);
			rc = 0;
		} else if (rc == 0) {
			D_DEBUG(DB_REBUILD, DF_UOID" "DF_UUID" %u exist.\n",
				DP_UOID(oids[i]), DP_UUID(co_uuids[i]),
				shards[i]);
		} else if (rc < 0) {
			break;
		}
	}
	if (rc < 0)
		D_GOTO(out_put, rc);

	/* Check and create task to iterate the local rebuild tree */
	if (!rpt->rt_lead_puller_running) {
		struct rebuild_iter_arg *arg;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out_put, rc = -DER_NOMEM);

		arg->obj_cb = rebuild_obj_callback;
		rpt_get(rpt);
		arg->rpt = rpt;

		rpt->rt_lead_puller_running = 1;
		D_ASSERT(rpt->rt_pullers != NULL);
		rc = dss_ult_create(rebuild_puller, arg, -1, 0, NULL);
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
