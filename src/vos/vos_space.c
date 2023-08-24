/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos_types.h>
#include "vos_internal.h"
#include "vos_policy.h"

#define POOL_SCM_SYS(pool)	((pool)->vp_space_sys[DAOS_MEDIA_SCM])
#define POOL_NVME_SYS(pool)	((pool)->vp_space_sys[DAOS_MEDIA_NVME])
#define POOL_SCM_HELD(pool)	((pool)->vp_space_held[DAOS_MEDIA_SCM])
#define POOL_NVME_HELD(pool)	((pool)->vp_space_held[DAOS_MEDIA_NVME])

/* Minimal seconds interval for updating VOS space metrics */
#define VOS_SPACE_METRICS_INTV	1

/* Extra space being reserved to deal with fragmentation issues */
static inline daos_size_t
get_frag_overhead(daos_size_t tot_size, int media, bool small_pool)
{
	daos_size_t	min_sz = (2ULL << 30);	/* 2GB */
	daos_size_t	max_sz = (10ULL << 30);	/* 10GB */
	daos_size_t	ovhd = (tot_size * 5) / 100;

	/*
	 * Don't reserve NVMe, if NVMe allocation failed due to fragmentations,
	 * only data coalescing in aggregation will be affected, punch and GC
	 * won't be affected.
	 */
	if (media == DAOS_MEDIA_NVME)
		return 0;

	/* If caller specified the pool is small, do not enforce a range */
	if (!small_pool) {
		if (ovhd < min_sz)
			ovhd = min_sz;
		else if (ovhd > max_sz)
			ovhd = max_sz;
	}

	return ovhd;
}

void
vos_space_sys_init(struct vos_pool *pool)
{
	daos_size_t	scm_tot = pool->vp_pool_df->pd_scm_sz;
	daos_size_t	nvme_tot = pool->vp_pool_df->pd_nvme_sz;

	POOL_SCM_SYS(pool) =
		get_frag_overhead(scm_tot, DAOS_MEDIA_SCM, pool->vp_small);
	POOL_NVME_SYS(pool) =
		get_frag_overhead(nvme_tot, DAOS_MEDIA_NVME, pool->vp_small);

	gc_reserve_space(&pool->vp_space_sys[0]);
	agg_reserve_space(&pool->vp_space_sys[0]);

	/* NVMe isn't configured */
	if (nvme_tot == 0)
		POOL_NVME_SYS(pool) = 0;

	if ((POOL_SCM_SYS(pool) * 2) > scm_tot) {
		D_WARN("Disable SCM space reserving for tiny pool:"DF_UUID" "
		       "sys["DF_U64"] > tot["DF_U64"]\n",
		       DP_UUID(pool->vp_id), POOL_SCM_SYS(pool), scm_tot);
		POOL_SCM_SYS(pool) = 0;
	}

	if ((POOL_NVME_SYS(pool) * 2) > nvme_tot) {
		D_WARN("Disable NVMe space reserving for tiny Pool:"DF_UUID" "
		       "sys["DF_U64"] > tot["DF_U64"]\n",
		       DP_UUID(pool->vp_id), POOL_NVME_SYS(pool), nvme_tot);
		POOL_NVME_SYS(pool) = 0;
	}
}

int
vos_space_sys_set(struct vos_pool *pool, daos_size_t *space_sys)
{
	daos_size_t	scm_tot = pool->vp_pool_df->pd_scm_sz;
	daos_size_t	nvme_tot = pool->vp_pool_df->pd_nvme_sz;
	daos_size_t	old_sys[DAOS_MEDIA_MAX];

	/* Save old value */
	old_sys[DAOS_MEDIA_SCM]		= POOL_SCM_SYS(pool);
	old_sys[DAOS_MEDIA_NVME]	= POOL_NVME_SYS(pool);

	vos_space_sys_init(pool);
	if (POOL_SCM_SYS(pool) + space_sys[DAOS_MEDIA_SCM] > scm_tot)
		goto error;

	if (pool->vp_vea_info &&
	    (POOL_NVME_SYS(pool) + space_sys[DAOS_MEDIA_NVME]) > nvme_tot)
		goto error;

	POOL_SCM_SYS(pool)	+= space_sys[DAOS_MEDIA_SCM];
	POOL_NVME_SYS(pool)	+= space_sys[DAOS_MEDIA_NVME];
	return 0;
error:
	D_ERROR("Pool:"DF_UUID" Too large reserved size. SCM: tot["DF_U64"], "
		"sys["DF_U64"], rsrv["DF_U64"] NVMe: tot["DF_U64"], "
		"sys["DF_U64"], rsrv["DF_U64"]\n", DP_UUID(pool->vp_id),
		scm_tot, POOL_SCM_SYS(pool), space_sys[DAOS_MEDIA_SCM],
		nvme_tot, POOL_NVME_SYS(pool), space_sys[DAOS_MEDIA_NVME]);

	/* Rollback old value */
	POOL_SCM_SYS(pool)	= old_sys[DAOS_MEDIA_SCM];
	POOL_NVME_SYS(pool)	= old_sys[DAOS_MEDIA_NVME];

	return -DER_INVAL;
}

int
vos_space_query(struct vos_pool *pool, struct vos_pool_space *vps, bool slow)
{
	struct vos_pool_df	*df = pool->vp_pool_df;
	struct vea_attr		*attr = &vps->vps_vea_attr;
	struct vea_stat		*stat = slow ? &vps->vps_vea_stat : NULL;
	daos_size_t		 scm_used;
	int			 rc;

	SCM_TOTAL(vps) = df->pd_scm_sz;
	NVME_TOTAL(vps) = df->pd_nvme_sz;
	SCM_SYS(vps) = POOL_SCM_SYS(pool);
	NVME_SYS(vps) = POOL_NVME_SYS(pool);

	/* Query SCM used space */
	rc = umempobj_get_heapusage(pool->vp_umm.umm_pool, &scm_used);
	if (rc) {
		rc = umem_tx_errno(rc);
		D_ERROR("Query pool:"DF_UUID" SCM space failed. "DF_RC"\n",
			DP_UUID(pool->vp_id), DP_RC(rc));
		return rc;
	}

	/*
	 * FIXME: pmemobj_ctl_get() sometimes return an insane large value, it
	 * could be a PMDK defect.
	 */
	if (SCM_TOTAL(vps) < scm_used) {
		D_CRIT("scm_sz:"DF_U64" < scm_used:"DF_U64"\n",
		       SCM_TOTAL(vps), scm_used);
		SCM_FREE(vps) = 0;
	} else {
		SCM_FREE(vps) = SCM_TOTAL(vps) - scm_used;
	}

	/* NVMe isn't configured for this VOS pool */
	if (pool->vp_vea_info == NULL) {
		NVME_TOTAL(vps) = 0;
		NVME_FREE(vps) = 0;
		NVME_SYS(vps) = 0;
		return 0;
	}

	/* Query NVMe free space */
	rc = vea_query(pool->vp_vea_info, attr, stat);
	if (rc) {
		D_ERROR("Query pool:"DF_UUID" NVMe space failed. "DF_RC"\n",
			DP_UUID(pool->vp_id), DP_RC(rc));
		return rc;
	}

	D_ASSERT(attr->va_blk_sz != 0);
	NVME_FREE(vps) = attr->va_blk_sz * attr->va_free_blks;

	D_ASSERTF(NVME_FREE(vps) <= NVME_TOTAL(vps),
		  "nvme_free:"DF_U64", nvme_sz:"DF_U64", blk_sz:%u\n",
		  NVME_FREE(vps), NVME_TOTAL(vps), attr->va_blk_sz);
	return 0;
}

static daos_size_t
estimate_space_key(struct umem_instance *umm, daos_key_t *key)
{
	struct vos_rec_bundle	rbund;
	struct dcs_csum_info	csum = { 0 };
	daos_size_t		size;

	rbund.rb_iov = key;
	rbund.rb_csum = &csum;

	/* Key record */
	size = vos_krec_size(&rbund);
	/* Add ample space assuming one tree node is added.  We could refine this later */
	size += 1024;

	return size;
}

/*
 * Estimate how much space will be consumed by an update request. This
 * conservative estimation always assumes new object, dkey, akey will be
 * created for the update.
 */
static void
estimate_space(struct vos_pool *pool, daos_key_t *dkey, unsigned int iod_nr,
	       daos_iod_t *iods, struct dcs_iod_csums *iods_csums,
	       daos_size_t *space_est)
{
	struct umem_instance	*umm = vos_pool2umm(pool);
	struct dcs_csum_info	*csums, *recx_csum;
	daos_iod_t		*iod;
	daos_recx_t		*recx;
	uint16_t		 media;
	daos_size_t		 size, scm, nvme = 0 /* in blk */;
	int			 i, j;

	/* Object record */
	scm = D_ALIGNUP(sizeof(struct vos_obj_df), 32);
	/* Assume one more object tree node created */
	scm += 1024;

	/* Dkey */
	scm += estimate_space_key(umm, dkey);

	for (i = 0; i < iod_nr; i++) {
		iod = &iods[i];

		/* Akey */
		scm += estimate_space_key(umm, &iod->iod_name);

		csums = vos_csum_at(iods_csums, i);
		/* Single value */
		if (iod->iod_type == DAOS_IOD_SINGLE) {
			size = iod->iod_size;
			media = vos_policy_media_select(pool, iod->iod_type,
							size, VOS_IOS_GENERIC);

			/* Single value record */
			if (media == DAOS_MEDIA_SCM) {
				scm += vos_recx2irec_size(size, csums);
			} else {
				scm += vos_recx2irec_size(0, csums);
				if (iod->iod_size != 0)
					nvme += vos_byte2blkcnt(iod->iod_size);
			}
			/* Assume one more SV tree node created */
			scm += 256;
			continue;
		}

		/* Array value */
		for (j = 0; j < iod->iod_nr; j++) {
			recx = &iod->iod_recxs[j];
			recx_csum = recx_csum_at(csums, j, iod);

			size = recx->rx_nr * iod->iod_size;
			media = vos_policy_media_select(pool, iod->iod_type,
							size, VOS_IOS_GENERIC);

			/* Extent */
			if (media == DAOS_MEDIA_SCM)
				scm += size;
			else if (size != 0)
				nvme += vos_byte2blkcnt(size);
			/* EVT desc */
			scm += 256;
			/* Checksum */
			scm += recx_csum_len(recx, recx_csum, iod->iod_size);
			/* Assume one more evtree node created */
			scm += 1024;
		}
	}

	space_est[DAOS_MEDIA_SCM] = scm;
	space_est[DAOS_MEDIA_NVME] = nvme * VOS_BLK_SZ;
}

int
vos_space_hold(struct vos_pool *pool, uint64_t flags, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods,
	       struct dcs_iod_csums *iods_csums, daos_size_t *space_hld)
{
	struct vos_pool_space	vps = { 0 };
	daos_size_t		space_est[DAOS_MEDIA_MAX] = { 0, 0 };
	daos_size_t		scm_left, nvme_left, rb_reserve;
	int			rc;

	rc = vos_space_query(pool, &vps, false);
	if (rc) {
		D_ERROR("Query pool:"DF_UUID" space failed. "DF_RC"\n",
			DP_UUID(pool->vp_id), DP_RC(rc));
		return rc;
	}

	estimate_space(pool, dkey, iod_nr, iods, iods_csums, &space_est[0]);

	/* if this is a critical update, skip SCM and NVMe sys/held checks */
	if (flags & VOS_OF_CRIT)
		goto success;

	scm_left = SCM_FREE(&vps);
	if (scm_left < SCM_SYS(&vps))
		goto error;

	scm_left -= SCM_SYS(&vps);
	if (scm_left < POOL_SCM_HELD(pool))
		goto error;

	scm_left -= POOL_SCM_HELD(pool);
	if (scm_left < space_est[DAOS_MEDIA_SCM])
		goto error;

	/* If NVMe is configured and this update uses NVMe space */
	if (pool->vp_vea_info != NULL && space_est[DAOS_MEDIA_NVME] != 0) {
		nvme_left = NVME_FREE(&vps);

		if (nvme_left < NVME_SYS(&vps))
			goto error;

		nvme_left -= NVME_SYS(&vps);
		/* 'NVMe held' has already been excluded from 'NVMe free' */

		if (nvme_left < space_est[DAOS_MEDIA_NVME])
			goto error;
	}

	/* Check space reserve for rebuild */
	if (!(flags & VOS_OF_REBUILD) && pool->vp_space_rb != 0) {
		rb_reserve = SCM_TOTAL(&vps) * pool->vp_space_rb / 100;

		if (SCM_FREE(&vps) < (rb_reserve + POOL_SCM_HELD(pool) +
				      space_est[DAOS_MEDIA_SCM])) {
			D_ERROR("Insufficient SCM space due to check "DF_U64" bytes (%u percent) "
				"reserved for rebuild.\n", rb_reserve, pool->vp_space_rb);
			goto error;
		}

		if (pool->vp_vea_info == NULL || space_est[DAOS_MEDIA_NVME] == 0)
			goto success;

		rb_reserve = NVME_TOTAL(&vps) * pool->vp_space_rb / 100;
		/* 'NVMe held' has already been excluded from 'NVMe free' */
		if (NVME_FREE(&vps) < (rb_reserve + space_est[DAOS_MEDIA_NVME])) {
			D_ERROR("Insufficient NVMe space due to check "DF_U64" bytes (%u percent) "
				"reserved for rebuild.\n", rb_reserve, pool->vp_space_rb);
			goto error;
		}
	}

success:
	space_hld[DAOS_MEDIA_SCM]	= space_est[DAOS_MEDIA_SCM];
	space_hld[DAOS_MEDIA_NVME]	= space_est[DAOS_MEDIA_NVME];
	POOL_SCM_HELD(pool)		+= space_hld[DAOS_MEDIA_SCM];
	POOL_NVME_HELD(pool)		+= space_hld[DAOS_MEDIA_NVME];

	return 0;
error:
	D_ERROR("Pool:"DF_UUID" is full. space_rb:%u\n", DP_UUID(pool->vp_id), pool->vp_space_rb);
	D_ERROR("SCM:  free["DF_U64"/"DF_U64"], sys["DF_U64"], hld["DF_U64"], est["DF_U64"]\n",
		SCM_FREE(&vps), SCM_TOTAL(&vps), SCM_SYS(&vps), POOL_SCM_HELD(pool),
		space_est[DAOS_MEDIA_SCM]);
	D_ERROR("NVMe: free["DF_U64"/"DF_U64"], sys["DF_U64"], hld["DF_U64"], est["DF_U64"]\n",
		NVME_FREE(&vps), NVME_TOTAL(&vps), NVME_SYS(&vps), POOL_NVME_HELD(pool),
		space_est[DAOS_MEDIA_NVME]);

	return -DER_NOSPACE;
}

void
vos_space_unhold(struct vos_pool *pool, daos_size_t *space_hld)
{
	D_ASSERTF(POOL_SCM_HELD(pool) >= space_hld[DAOS_MEDIA_SCM],
		  "SCM tot_hld:"DF_U64" < hld:"DF_U64"\n",
		  POOL_SCM_HELD(pool), space_hld[DAOS_MEDIA_SCM]);
	D_ASSERTF(POOL_NVME_HELD(pool) >= space_hld[DAOS_MEDIA_NVME],
		  "NVMe tot_hld:"DF_U64" < hld:"DF_U64"\n",
		  POOL_NVME_HELD(pool), space_hld[DAOS_MEDIA_NVME]);

	POOL_SCM_HELD(pool)	-= space_hld[DAOS_MEDIA_SCM];
	POOL_NVME_HELD(pool)	-= space_hld[DAOS_MEDIA_NVME];
}

void
vos_space_update_metrics(struct vos_pool *pool)
{
	struct vos_pool_metrics	*vpm;
	uint64_t		 now;

	vpm = pool->vp_metrics;
	if (!vpm)
		return;

	now = daos_gettime_coarse();
	if (now < vpm->vp_space_metrics.vsm_last_update_ts + VOS_SPACE_METRICS_INTV) {
		return;
	}
	vpm->vp_space_metrics.vsm_last_update_ts = now;

	if (vpm->vp_space_metrics.vsm_scm_used) {
		daos_size_t	scm_used;
		int		rc;
		rc = umempobj_get_heapusage(pool->vp_umm.umm_pool, &scm_used);
		if (rc) {
			rc = umem_tx_errno(rc);
			D_ERROR("Query pool:"DF_UUID" SCM space failed. "DF_RC"\n",
				DP_UUID(pool->vp_id), DP_RC(rc));
		} else {
			d_tm_set_gauge(vpm->vp_space_metrics.vsm_scm_used, scm_used);
		}
	}

	if (vpm->vp_space_metrics.vsm_nvme_used && pool->vp_vea_info) {
		struct vea_attr	va = { 0 };
		daos_size_t	nvme_used;
		int		rc;

		rc = vea_query(pool->vp_vea_info, &va, NULL);
		if (rc) {
			D_ERROR("Query Pool:"DF_UUID" NVMe space failed. "DF_RC"\n",
				DP_UUID(pool->vp_id), DP_RC(rc));
		}

		nvme_used = (va.va_tot_blks - va.va_free_blks) * va.va_blk_sz;
		d_tm_set_gauge(vpm->vp_space_metrics.vsm_nvme_used, nvme_used);
	}
}
