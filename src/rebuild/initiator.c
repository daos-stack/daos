/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
#include <daos_srv/dtx_srv.h>
#include "rpc.h"
#include "rebuild_internal.h"

typedef int (*rebuild_obj_iter_cb_t)(daos_unit_oid_t oid, daos_epoch_t eph,
				     unsigned int shard, unsigned int tgt_idx,
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
	daos_epoch_t	epoch;
	unsigned int	shard;
	unsigned int	tgt_idx;
	struct rebuild_tgt_pool_tracker *rpt;
};

#define PULLER_STACK_SIZE	131072
#define MAX_BUF_SIZE		2048

static int
rebuild_fetch_update_inline(struct rebuild_one *rdone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont)
{
	d_sg_list_t	sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t	iov[DSS_ENUM_UNPACK_MAX_IODS];
	int		iod_cnt = 0;
	int		start;
	char		iov_buf[DSS_ENUM_UNPACK_MAX_IODS][MAX_BUF_SIZE];
	bool		fetch = false;
	int		i;
	int		rc;

	D_ASSERT(rdone->ro_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	for (i = 0; i < rdone->ro_iod_num; i++) {
		if (rdone->ro_iods[i].iod_size == 0)
			continue;

		if (rdone->ro_sgls != NULL && rdone->ro_sgls[i].sg_nr > 0) {
			sgls[i] = rdone->ro_sgls[i];
		} else {
			sgls[i].sg_nr = 1;
			sgls[i].sg_nr_out = 1;
			d_iov_set(&iov[i], iov_buf[i], MAX_BUF_SIZE);
			sgls[i].sg_iovs = &iov[i];
			fetch = true;
		}
	}

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %d %s nr %d eph "DF_U64
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

	for (i = 0, start = 0; i < rdone->ro_iod_num; i++) {
		if (rdone->ro_iods[i].iod_size > 0) {
			iod_cnt++;
			continue;
		} else {
			/* skip empty record */
			if (iod_cnt == 0) {
				D_DEBUG(DB_REBUILD, "i %d iod_size = 0\n", i);
				continue;
			}

			D_DEBUG(DB_REBUILD, "update start %d cnt %d\n",
				start, iod_cnt);
			rc = vos_obj_update(ds_cont->sc_hdl, rdone->ro_oid,
					    rdone->ro_epoch, rdone->ro_version,
					    &rdone->ro_dkey, iod_cnt,
					    &rdone->ro_iods[start],
					    &sgls[start]);
			if (rc) {
				D_ERROR("rebuild failed: rc %d\n", rc);
				break;
			}
			iod_cnt = 0;
			start = i + 1;
		}
	}

	if (iod_cnt > 0)
		rc = vos_obj_update(ds_cont->sc_hdl, rdone->ro_oid,
				    rdone->ro_epoch, rdone->ro_version,
				    &rdone->ro_dkey, iod_cnt,
				    &rdone->ro_iods[start], &sgls[start]);

	return rc;
}

static int
rebuild_fetch_update_bulk(struct rebuild_one *rdone, daos_handle_t oh,
			  struct ds_cont_child *ds_cont)
{
	d_sg_list_t	 sgls[DSS_ENUM_UNPACK_MAX_IODS], *sgl;
	daos_handle_t	 ioh;
	int		 rc, i, ret, sgl_cnt = 0;

	D_ASSERT(rdone->ro_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	rc = vos_update_begin(ds_cont->sc_hdl, rdone->ro_oid, rdone->ro_epoch,
			      &rdone->ro_dkey, rdone->ro_iod_num,
			      rdone->ro_iods, &ioh, NULL);
	if (rc != 0) {
		D_ERROR(DF_UOID"preparing update fails: %d\n",
			DP_UOID(rdone->ro_oid), rc);
		return rc;
	}

	rc = bio_iod_prep(vos_ioh2desc(ioh));
	if (rc) {
		D_ERROR("Prepare EIOD for "DF_UOID" error: %d\n",
			DP_UOID(rdone->ro_oid), rc);
		goto end;
	}

	for (i = 0; i < rdone->ro_iod_num; i++) {
		struct bio_sglist	*bsgl;

		bsgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(bsgl != NULL);
		sgl = &sgls[i];

		rc = bio_sgl_convert(bsgl, sgl);
		if (rc)
			goto post;
		sgl_cnt++;
	}

	D_DEBUG(DB_REBUILD, DF_UOID" rdone %p dkey %d %s nr %d eph "DF_U64"\n",
		DP_UOID(rdone->ro_oid), rdone, (int)rdone->ro_dkey.iov_len,
		(char *)rdone->ro_dkey.iov_buf, rdone->ro_iod_num,
		rdone->ro_epoch);

	rc = ds_obj_fetch(oh, rdone->ro_epoch, &rdone->ro_dkey,
			  rdone->ro_iod_num, rdone->ro_iods,
			  sgls, NULL);
	if (rc)
		D_ERROR("rebuild dkey %d %s failed rc %d\n",
			(int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf, rc);
post:
	for (i = 0; i < sgl_cnt; i++) {
		sgl = &sgls[i];
		daos_sgl_fini(sgl, false);
	}

	ret = bio_iod_post(vos_ioh2desc(ioh));
	if (ret) {
		D_ERROR("Post EIOD for "DF_UOID" error: %d\n",
			DP_UOID(rdone->ro_oid), ret);
		rc = rc ? : ret;
	}

end:
	vos_update_end(ioh, rdone->ro_version, &rdone->ro_dkey, rc, NULL);
	return rc;
}

/**
 * Punch dkeys/akeys before rebuild.
 */
static int
rebuild_one_punch_keys(struct rebuild_tgt_pool_tracker *rpt,
		       struct rebuild_one *rdone, struct ds_cont_child *cont)
{
	int	i;
	int	rc = 0;

	/* Punch dkey */
	if (rdone->ro_max_eph != DAOS_EPOCH_MAX) {
		D_DEBUG(DB_REBUILD, DF_UOID" punch dkey %d %s eph "DF_U64"\n",
			DP_UOID(rdone->ro_oid), (int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf, rdone->ro_max_eph);
		rc = vos_obj_punch(cont->sc_hdl, rdone->ro_oid,
				   rdone->ro_max_eph, rpt->rt_rebuild_ver,
				   VOS_OF_REPLAY_PC, &rdone->ro_dkey, 0, NULL,
				   NULL);
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
		D_DEBUG(DB_REBUILD, DF_UOID" rdone %p punch dkey %d %s akey"
			" %d %s  eph "DF_U64"\n", DP_UOID(rdone->ro_oid),
			rdone, (int)rdone->ro_dkey.iov_len,
			(char *)rdone->ro_dkey.iov_buf,
			(int)rdone->ro_ephs_keys[i].iov_len,
			(char *)rdone->ro_ephs_keys[i].iov_buf,
			rdone->ro_ephs[i]);
		D_ASSERT(rdone->ro_ephs[i] != DAOS_EPOCH_MAX);
		rc = vos_obj_punch(cont->sc_hdl, rdone->ro_oid,
				   rdone->ro_ephs[i], rpt->rt_rebuild_ver,
				   VOS_OF_REPLAY_PC, &rdone->ro_dkey, 1,
				   &rdone->ro_ephs_keys[i], NULL);
		if (rc) {
			D_ERROR(DF_UOID" punch akey failed: rc %d\n",
				DP_UOID(rdone->ro_oid), rc);
			return rc;
		}
	}

	/* punch records */
	if (rdone->ro_punch_iod_num > 0) {
		rc = vos_obj_update(cont->sc_hdl, rdone->ro_oid,
				    rdone->ro_epoch, rdone->ro_version,
				    &rdone->ro_dkey, rdone->ro_punch_iod_num,
				    rdone->ro_punch_iods, NULL);
		D_DEBUG(DB_REBUILD, DF_UOID" rdone %p punch %d records: %d\n",
			DP_UOID(rdone->ro_oid), rdone, rdone->ro_punch_iod_num,
			rc);
	}

	return rc;
}

static int
rebuild_dkey(struct rebuild_tgt_pool_tracker *rpt,
	     struct rebuild_one *rdone)
{
	struct rebuild_pool_tls	*tls;
	struct ds_cont_child *rebuild_cont;
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

	rc = ds_obj_open(coh, rdone->ro_oid.id_pub, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(cont_close, rc);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_TGT_NOSPACE))
		D_GOTO(obj_close, rc = -DER_NOSPACE);

	rc = ds_cont_child_lookup(rpt->rt_pool_uuid, rdone->ro_cont_uuid,
				  &rebuild_cont);
	if (rc)
		D_GOTO(obj_close, rc);

	rc = rebuild_one_punch_keys(rpt, rdone, rebuild_cont);
	if (rc)
		D_GOTO(cont_put, rc);

	data_size = daos_iods_len(rdone->ro_iods, rdone->ro_iod_num);

	D_DEBUG(DB_REBUILD, "data size is "DF_U64"\n", data_size);
	/* DAOS_REBUILD_TGT_NO_REBUILD are for testing purpose */
	if ((data_size > 0 || data_size == (daos_size_t)(-1)) &&
	    !DAOS_FAIL_CHECK(DAOS_REBUILD_NO_REBUILD)) {
		if (data_size < MAX_BUF_SIZE || data_size == (daos_size_t)(-1))
			rc = rebuild_fetch_update_inline(rdone, oh,
							 rebuild_cont);
		else
			rc = rebuild_fetch_update_bulk(rdone, oh, rebuild_cont);
	}

	tls->rebuild_pool_rec_count += rdone->ro_rec_num;
cont_put:
	ds_cont_child_put(rebuild_cont);
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

	if (rdone->ro_iods)
		daos_iods_free(rdone->ro_iods, rdone->ro_iod_alloc_num, true);

	if (rdone->ro_punch_iods)
		daos_iods_free(rdone->ro_punch_iods, rdone->ro_iod_alloc_num,
			       true);

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

	D_FREE(rdone);
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
	idx = dss_get_module_info()->dmi_tgt_id;
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
			d_list_move_tail(&rdone->ro_list, &rebuild_list);
			puller->rp_inflight++;
		}
		ABT_mutex_unlock(puller->rp_lock);

		d_list_for_each_entry_safe(rdone, tmp, &rebuild_list, ro_list) {
			d_list_del_init(&rdone->ro_list);
			if (!rpt->rt_abort) {
				rc = rebuild_dkey(rpt, rdone);
				D_DEBUG(DB_REBUILD, DF_UOID" rebuild dkey %d %s"
					" rc %d tag %d rpt %p\n",
					DP_UOID(rdone->ro_oid),
					(int)rdone->ro_dkey.iov_len,
					(char *)rdone->ro_dkey.iov_buf, rc,
					idx, rpt);
			}

			D_ASSERT(puller->rp_inflight > 0);
			puller->rp_inflight--;

			if (rc == -DER_NOSPACE) {
				/* If there are no space on current VOS, let's
				 * hang the rebuild ULT on the current xstream,
				 * and waitting for the space is reclaimed or
				 * the drive is replaced.
				 *
				 * If the space is reclaimed, then it will
				 * resume the rebuild ULT.
				 * If the drive is replaced, then it will
				 * abort the current rebuild by other process.
				 */
				rebuild_hang();
				ABT_thread_yield();
				D_DEBUG(DB_REBUILD, "%p rebuild got back.\n",
					rpt);
				rc = 0;
				/* Added it back to rdone */
				ABT_mutex_lock(puller->rp_lock);
				d_list_add_tail(&rdone->ro_list,
						&puller->rp_one_list);
				ABT_mutex_unlock(puller->rp_lock);
				continue;
			}

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

static int
rw_iod_pack(struct rebuild_one *rdone, daos_iod_t *iod, d_sg_list_t *sgls)
{
	int idx = rdone->ro_iod_num;
	int rec_cnt = 0;
	int i;
	int rc;

	D_ASSERT(iod->iod_size > 0);

	rc = daos_iod_copy(&rdone->ro_iods[idx], iod);
	if (rc)
		return rc;

	for (i = 0; i < iod->iod_nr; i++) {
		rec_cnt += iod->iod_recxs[i].rx_nr;
		if (rdone->ro_epoch == 0 ||
		    iod->iod_eprs[i].epr_lo < rdone->ro_epoch)
			rdone->ro_epoch = iod->iod_eprs[i].epr_lo;
	}

	D_DEBUG(DB_REBUILD, "idx %d akey %d %s nr %d size "DF_U64" type %d eph "
		DF_U64"/"DF_U64"\n", idx, (int)iod->iod_name.iov_len,
		(char *)iod->iod_name.iov_buf, iod->iod_nr, iod->iod_size,
		iod->iod_type, iod->iod_eprs->epr_lo, iod->iod_eprs->epr_hi);

	/* Check if data has been retrieved by iteration */
	if (sgls) {
		if (rdone->ro_sgls == NULL) {
			D_ASSERT(rdone->ro_iod_alloc_num > 0);
			D_ALLOC_ARRAY(rdone->ro_sgls, rdone->ro_iod_alloc_num);
			if (rdone->ro_sgls == NULL)
				return -DER_NOMEM;
		}

		rc = daos_sgl_alloc_copy_data(&rdone->ro_sgls[idx], sgls);
		if (rc)
			D_GOTO(out, rc);
	}

	rdone->ro_iod_num++;
	rdone->ro_rec_num += rec_cnt;
	iod->iod_recxs = NULL;
	iod->iod_csums = NULL;
	iod->iod_eprs = NULL;

out:
	return 0;
}

static int
punch_iod_pack(struct rebuild_one *rdone, daos_iod_t *iod)
{
	int idx = rdone->ro_punch_iod_num;
	int rc;

	D_ASSERT(iod->iod_size == 0);

	if (rdone->ro_punch_iods == NULL) {
		D_ALLOC_ARRAY(rdone->ro_punch_iods, rdone->ro_iod_alloc_num);
		if (rdone->ro_punch_iods == NULL)
			return -DER_NOMEM;
	}

	rc = daos_iod_copy(&rdone->ro_punch_iods[idx], iod);
	if (rc)
		return rc;

	rdone->ro_punch_iod_num++;
	iod->iod_recxs = NULL;
	iod->iod_csums = NULL;
	iod->iod_eprs = NULL;
	return 0;
}

/*
 * Queue dkey to the rebuild dkey list on each xstream. Note that this function
 * steals the memory of the recx, csum, and epr arrays from iods.
 */
static int
rebuild_one_queue(struct rebuild_iter_obj_arg *iter_arg, daos_unit_oid_t *oid,
		  daos_key_t *dkey, daos_epoch_t dkey_eph, daos_iod_t *iods,
		  daos_epoch_t *akey_ephs, int iod_eph_total,
		  d_sg_list_t *sgls, uint32_t version)
{
	struct rebuild_puller		*puller;
	struct rebuild_tgt_pool_tracker *rpt = iter_arg->rpt;
	struct rebuild_one		*rdone = NULL;
	unsigned int			ephs_cnt = 0;
	bool				inline_copy = true;
	int				i;
	int				rc;

	D_DEBUG(DB_REBUILD, "rebuild dkey %d %s iod nr %d dkey_eph "DF_U64"\n",
		(int)dkey->iov_buf_len, (char *)dkey->iov_buf, iod_eph_total,
		dkey_eph);

	if (iod_eph_total == 0 || rpt->rt_rebuild_ver <= version) {
		D_DEBUG(DB_REBUILD, "No need rebuild eph_total %d version %u"
			" rebuild ver %u\n", iod_eph_total, version,
			rpt->rt_rebuild_ver);
		return 0;
	}

	D_ALLOC_PTR(rdone);
	if (rdone == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(rdone->ro_iods, iod_eph_total);
	if (rdone->ro_iods == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(rdone->ro_ephs, iod_eph_total);
	D_ALLOC_ARRAY(rdone->ro_ephs_keys, iod_eph_total);
	if (rdone->ro_iods == NULL || rdone->ro_ephs == NULL ||
	    rdone->ro_ephs_keys == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	rdone->ro_iod_alloc_num = iod_eph_total;
	/* only do the copy below when each with inline recx data */
	for (i = 0; i < iod_eph_total; i++) {
		int j;

		if (sgls[i].sg_nr == 0 || sgls[i].sg_iovs == NULL) {
			inline_copy = false;
			break;
		}

		for (j = 0; j < sgls[i].sg_nr; j++) {
			if (sgls[i].sg_iovs[j].iov_len == 0 ||
			    sgls[i].sg_iovs[j].iov_buf == NULL) {
				inline_copy = false;
				break;
			}
		}

		if (!inline_copy)
			break;
	}

	for (i = 0; i < iod_eph_total; i++) {
		if (akey_ephs[i] != DAOS_EPOCH_MAX) {
			/* Pack punched epoch here */
			rdone->ro_ephs[ephs_cnt] = akey_ephs[i];
			rc = daos_iov_copy(&rdone->ro_ephs_keys[ephs_cnt],
					   &iods[i].iod_name);
			if (rc)
				D_GOTO(free, rc);

			ephs_cnt++;
			D_DEBUG(DB_REBUILD, "punched iod idx %d akey %d %s"
				" ephs "DF_U64" ephs_cnt %d\n", i,
				(int)iods[i].iod_name.iov_len,
				(char *)iods[i].iod_name.iov_buf,
				akey_ephs[i], ephs_cnt);
		}

		if (iods[i].iod_nr == 0)
			continue;

		if (iods[i].iod_size == 0)
			rc = punch_iod_pack(rdone, &iods[i]);
		else
			rc = rw_iod_pack(rdone, &iods[i],
					 inline_copy ? &sgls[i] : NULL);
	}

	rdone->ro_ephs_num = ephs_cnt;
	rdone->ro_max_eph = dkey_eph;
	rdone->ro_version = version;
	puller = &rpt->rt_pullers[iter_arg->tgt_idx];
	if (puller->rp_ult == NULL) {
		/* Create puller ULT thread, and destroy ULT until
		 * rebuild finish in rebuild_fini().
		 */
		D_ASSERT(puller->rp_ult_running == 0);
		D_DEBUG(DB_REBUILD, "create rebuild dkey ult %d\n",
			iter_arg->tgt_idx);
		rpt_get(rpt);
		rc = dss_ult_create(rebuild_one_ult, rpt, DSS_ULT_REBUILD,
				    iter_arg->tgt_idx, PULLER_STACK_SIZE,
				    &puller->rp_ult);
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

	D_DEBUG(DB_REBUILD, DF_UOID" %p dkey %d %s rebuild on idx %d max eph"
		" "DF_U64" iod_num %d\n", DP_UOID(rdone->ro_oid), rdone,
		(int)dkey->iov_len, (char *)dkey->iov_buf, iter_arg->tgt_idx,
		rdone->ro_max_eph, rdone->ro_iod_num);

	ABT_mutex_lock(puller->rp_lock);
	d_list_add_tail(&rdone->ro_list, &puller->rp_one_list);
	ABT_mutex_unlock(puller->rp_lock);

free:
	if (rc != 0 && rdone != NULL)
		rebuild_one_destroy(rdone);

	return rc;
}

static int
rebuild_one_queue_cb(struct dss_enum_unpack_io *io, void *arg)
{
	return rebuild_one_queue(arg, &io->ui_oid, &io->ui_dkey,
				 io->ui_dkey_eph, io->ui_iods,
				 io->ui_akey_ephs, io->ui_iods_len,
				 io->ui_sgls, io->ui_version);
}

static int
rebuild_obj_punch_one(void *data)
{
	struct rebuild_iter_obj_arg	*arg = data;
	struct ds_cont_child		*cont;
	int		rc;

	D_DEBUG(DB_REBUILD, "punch "DF_UOID"\n", DP_UOID(arg->oid));
	rc = ds_cont_child_lookup(arg->rpt->rt_pool_uuid, arg->cont_uuid,
				  &cont);
	D_ASSERT(rc == 0);

	rc = vos_obj_punch(cont->sc_hdl, arg->oid, arg->epoch,
			   arg->rpt->rt_rebuild_ver, VOS_OF_REPLAY_PC,
			   NULL, 0, NULL, NULL);
	ds_cont_child_put(cont);
	if (rc)
		D_ERROR(DF_UOID" rebuild punch failed rc %d\n",
			DP_UOID(arg->oid), rc);

	return rc;
}

static int
rebuild_obj_punch(struct rebuild_iter_obj_arg *arg)
{
	return dss_task_collective(rebuild_obj_punch_one, arg, 0);
}

#define KDS_NUM		16
#define ITER_BUF_SIZE   2048

/**
 * Iterate akeys/dkeys of the object
 */
static void
rebuild_obj_ult(void *data)
{
	struct rebuild_iter_obj_arg	*arg = data;
	struct rebuild_pool_tls		*tls;
	daos_anchor_t			 anchor;
	daos_anchor_t			 dkey_anchor;
	daos_anchor_t			 akey_anchor;
	daos_handle_t			 oh;
	d_sg_list_t			 sgl = { 0 };
	d_iov_t			 iov = { 0 };
	char				 stack_buf[ITER_BUF_SIZE];
	char				*buf = NULL;
	daos_size_t			 buf_len;
	struct dss_enum_arg		 enum_arg;
	int				 rc;

	tls = rebuild_pool_tls_lookup(arg->rpt->rt_pool_uuid,
				      arg->rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	if (arg->epoch != DAOS_EPOCH_MAX) {
		rc = rebuild_obj_punch(arg);
		if (rc)
			D_GOTO(free, rc);
	}

	rc = ds_obj_open(arg->cont_hdl, arg->oid.id_pub, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(free, rc);

	D_DEBUG(DB_REBUILD, "start rebuild obj "DF_UOID" for shard %u\n",
		DP_UOID(arg->oid), arg->shard);
	memset(&anchor, 0, sizeof(anchor));
	memset(&dkey_anchor, 0, sizeof(dkey_anchor));
	memset(&akey_anchor, 0, sizeof(akey_anchor));
	dc_obj_shard2anchor(&dkey_anchor, arg->shard);
	daos_anchor_set_flags(&dkey_anchor,
			      DAOS_ANCHOR_FLAGS_TO_LEADER);

	/* Initialize enum_arg for VOS_ITER_DKEY. */
	memset(&enum_arg, 0, sizeof(enum_arg));
	enum_arg.oid = arg->oid;
	enum_arg.chk_key2big = true;

	buf = stack_buf;
	buf_len = ITER_BUF_SIZE;
	while (1) {
		daos_key_desc_t	kds[KDS_NUM] = { 0 };
		daos_epoch_range_t eprs[KDS_NUM];
		uint32_t	num = KDS_NUM;
		daos_size_t	size;

		memset(buf, 0, buf_len);
		iov.iov_len = 0;
		iov.iov_buf = buf;
		iov.iov_buf_len = buf_len;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;

		rc = ds_obj_list_obj(oh, arg->epoch, NULL, NULL, &size,
				     &num, kds, eprs, &sgl, &anchor,
				     &dkey_anchor, &akey_anchor);

		if (rc == -DER_KEY2BIG) {
			D_DEBUG(DB_REBUILD, "rebuild obj "DF_UOID" got "
				"-DER_KEY2BIG, key_len "DF_U64"\n",
				DP_UOID(arg->oid), kds[0].kd_key_len);
			buf_len = roundup(kds[0].kd_key_len * 2, 8);
			if (buf != stack_buf)
				D_FREE(buf);
			D_ALLOC(buf, buf_len);
			if (buf == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			continue;
		} else if (rc) {
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
	if (buf != NULL && buf != stack_buf)
		D_FREE(buf);
	if (arg->epoch == DAOS_EPOCH_MAX)
		tls->rebuild_pool_obj_count++;
	if (tls->rebuild_pool_status == 0 && rc < 0)
		tls->rebuild_pool_status = rc;
	D_DEBUG(DB_REBUILD, "stop rebuild obj "DF_UOID" for shard %u rc %d\n",
		DP_UOID(arg->oid), arg->shard, rc);
	rpt_put(arg->rpt);
	D_FREE(arg);
}

static int
rebuild_obj_callback(daos_unit_oid_t oid, daos_epoch_t eph, unsigned int shard,
		     unsigned int tgt_idx, void *data)
{
	struct puller_iter_arg		*iter_arg = data;
	struct rebuild_iter_obj_arg	*obj_arg;
	int				rc;

	D_ALLOC_PTR(obj_arg);
	if (obj_arg == NULL)
		return -DER_NOMEM;

	obj_arg->oid = oid;
	obj_arg->epoch = eph;
	obj_arg->shard = shard;
	obj_arg->tgt_idx = tgt_idx;
	obj_arg->cont_hdl = iter_arg->cont_hdl;
	uuid_copy(obj_arg->cont_uuid, iter_arg->cont_uuid);
	rpt_get(iter_arg->rpt);
	obj_arg->rpt = iter_arg->rpt;
	if (eph == DAOS_EPOCH_MAX)
		obj_arg->rpt->rt_toberb_objs++;

	/* Let's iterate the object on different xstream */
	rc = dss_ult_create(rebuild_obj_ult, obj_arg, DSS_ULT_REBUILD,
			    oid.id_pub.lo % dss_tgt_nr,
			    PULLER_STACK_SIZE, NULL);
	if (rc) {
		rpt_put(iter_arg->rpt);
		D_FREE(obj_arg);
	}

	return rc;
}

#define DEFAULT_YIELD_FREQ			128
static int
puller_obj_iter_cb(daos_handle_t ih, d_iov_t *key_iov,
		   d_iov_t *val_iov, void *data)
{
	struct puller_iter_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	struct rebuild_obj_key		*key = key_iov->iov_buf;
	daos_unit_oid_t			oid = key->oid;
	daos_epoch_t			epoch = key->eph;
	unsigned int			tgt_idx = key->tgt_idx;
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
	rc = arg->obj_cb(oid, epoch, *shard, tgt_idx, arg);
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
		rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_REBUILD,
				       NULL, NULL);
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
puller_cont_iter_cb(daos_handle_t ih, d_iov_t *key_iov,
		    d_iov_t *val_iov, void *data)
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
		rc = dbtree_iterate(root->root_hdl, DAOS_INTENT_REBUILD, false,
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
		rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, DAOS_INTENT_REBUILD,
				       key_iov, NULL);
		if (rc) {
			D_ASSERT(rc != -DER_NONEXIST);
			return rc;
		}
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_REBUILD,
			       NULL, NULL);
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
		rc = dbtree_iterate(rpt->rt_tobe_rb_root_hdl,
				    DAOS_INTENT_REBUILD, false,
				    puller_cont_iter_cb, iter_arg);
		if (rc) {
			D_ERROR("dbtree iterate fails %d\n", rc);
			if (tls->rebuild_pool_status == 0)
				tls->rebuild_pool_status = rc;
			break;
		}
	}

	D_FREE(iter_arg);
	rpt->rt_lead_puller_running = 0;
	rpt_put(rpt);
}

static int
rebuilt_btr_destory_cb(daos_handle_t ih, d_iov_t *key_iov,
		       d_iov_t *val_iov, void *data)
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

	rc = dbtree_iterate(btr_hdl, DAOS_INTENT_REBUILD, false,
			    rebuilt_btr_destory_cb, NULL);
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
				unsigned int shard, unsigned int tgt_idx,
				unsigned int *cnt, int ref)
{
	struct rebuilt_oid	*roid;
	struct rebuilt_oid	roid_tmp;
	struct rebuild_obj_key	key = { 0 };
	uint32_t		req_cnt;
	d_iov_t		key_iov;
	d_iov_t		val_iov;
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
	key.tgt_idx = tgt_idx;
	/* Finally look up the object under the container tree */
	d_iov_set(&key_iov, &key, sizeof(key));
	d_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(cont_root->root_hdl, &key_iov, &val_iov);
	D_DEBUG(DB_REBUILD, "lookup "DF_UOID" in cont "DF_UUID" eph "
		DF_U64" tgt_idx %d rc %d\n", DP_UOID(oid), DP_UUID(co_uuid),
		eph, tgt_idx, rc);
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
		d_iov_set(&val_iov, &roid_tmp, sizeof(roid_tmp));
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
	struct rebuild_in		*rebuild_in;
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

	if (rebuild_in->roi_tgt_idx >= dss_tgt_nr) {
		D_ERROR("Wrong tgt idx %d\n", rebuild_in->roi_tgt_idx);
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
		D_GOTO(out, rc);

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
				      rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	/* Insert these oids/conts into the local rebuild tree */
	for (i = 0; i < oids_count; i++) {
		/* firstly insert/check rebuilt tree */
		rc = rebuild_cont_obj_insert(rebuilt_btr_hdl, co_uuids[i],
					     oids[i], ephs[i], shards[i],
					     rebuild_in->roi_tgt_idx,
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
					     oids[i], ephs[i], shards[i],
					     rebuild_in->roi_tgt_idx,
					     NULL, 0, rebuild_obj_insert_cb);
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
					rebuild_in->roi_tgt_idx,
					&rpt->rt_rebuilt_obj_cnt, -1,
					rebuild_scheduled_obj_insert_cb);
			break;
		}
	}
	if (rc < 0)
		D_GOTO(out, rc);

	/* Check and create task to iterate the to-be-rebuilt tree */
	if (!rpt->rt_lead_puller_running) {
		struct puller_iter_arg *arg;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		arg->obj_cb = rebuild_obj_callback;
		rpt_get(rpt);
		arg->rpt = rpt;

		rpt->rt_lead_puller_running = 1;
		D_ASSERT(rpt->rt_pullers != NULL);
		rc = dss_ult_create(rebuild_puller_ult, arg, DSS_ULT_REBUILD,
				    DSS_TGT_SELF, 0, NULL);
		if (rc) {
			rpt_put(rpt);
			D_FREE(arg);
			rpt->rt_lead_puller_running = 0;
			D_GOTO(out, rc);
		}
	}
out:
	if (rpt)
		rpt_put(rpt);
	rebuild_out = crt_reply_get(rpc);
	rebuild_out->roo_status = rc;
	dss_rpc_reply(rpc, DAOS_REBUILD_DROP_OBJ);
}
