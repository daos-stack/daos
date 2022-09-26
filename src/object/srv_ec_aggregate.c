/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * DAOS server erasure-coded object aggregation.
 *
 * src/object/srv_ec_aggregate.c
 *
 * Iterates over replica extents for objects with this target a leader.
 *
 * Processes each EC stripe with replica(s) present.
 *
 * If replicas fill the stripe, the parity is regenerated from the local
 * extents.
 *	- The parity for peer parity extents is transferred.
 *	- Replicas for the stripe are removed from parity targets.
 *
 * If replicas are partial, and prior parity exists:
 *	- If less than half cells are updated (have replicas, parity is updated:
 *		- Old data cells for cells with replica data are fetched from
 *		  data targets (old, since fetched at epoch of existing parity).
 *		- Peer parity is fetched.
 *		- Parity is incrementally updated.
 *		- Updated parity is transferred to peer parity target(s).
 *	- If half or more of the cells are update by replicas:
 *		- All cells not filled by local replicas are fetched.
 *		- New parity is generated from entire stripe.
 *		- Updated parity is transferred to peer parity target(s).
 *	- Replicas for the stripe are removed from parity targets.
 *
 * If the stripe contains holes later than the parity:
 *	- Valid ranges in the stripe are pulled from the data targets and
 *	  written to local VOS, and peer parity VOS, as replicas.
 *	- Parity is removed for latest parity epoch in local VOS,
 *	  and from VOS on peer parity targets.
 *
 * If replicas exist that are older than the latest parity, they are removed
 * from parity targets.
 *
 * If checksums are supported for the container, checksums are verified for
 * all read data, and they are calculated for generated parity. Re-replicated
 * data is stored with the checksums from the fetch verification.
 *
 */

#define D_LOGFAC	DD_FAC(object)

#include <stddef.h>
#include <stdio.h>
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/srv_obj_ec.h>
#include "obj_ec.h"
#include "srv_internal.h"

#define EC_AGG_ITERATION_MAX	2048

/* Pool/container info. Shared handle UUIDs, and service list are initialized
 * in system Xstream.
 */
struct ec_agg_pool_info {
	uuid_t		 api_pool_uuid;		/* open pool, check leader    */
	uuid_t		 api_poh_uuid;		/* pool handle uuid           */
	uuid_t		 api_cont_uuid;		/* container uuid             */
	uuid_t		 api_coh_uuid;		/* container handle uuid      */
	daos_handle_t	 api_cont_hdl;		/* container handle, returned by
						 * container open
						 */
	struct cont_props api_props;		/* container properties */
	daos_handle_t	 api_pool_hdl;		/* pool handle */
	d_rank_list_t	*api_svc_list;		/* service list               */
	struct ds_pool	*api_pool;		/* Used for IV fetch          */
};

/* Local parity extent for the stripe undergoing aggregation. Stores the
 * information returned by the iterator.
 */
struct ec_agg_par_extent {
	daos_recx_t	ape_recx;	/* recx for the parity extent */
	daos_epoch_t	ape_epoch;	/* epoch of the parity extent */
};

/* Represents the current stripe undergoing aggregation.
 */
struct ec_agg_stripe {
	daos_off_t	as_stripenum;   /* ordinal of stripe, offset/(k*len) */
	daos_epoch_t	as_hi_epoch;    /* highest epoch  in stripe          */
	d_list_t	as_dextents;    /* list of stripe's data extents     */
	daos_off_t	as_stripe_fill; /* amount of stripe covered by data  */
	uint64_t	as_offset;      /* start offset in stripe            */
	unsigned int	as_extent_cnt;  /* number of replica extents         */
	bool		as_has_holes;   /* stripe includes holes             */
};

/* Struct used to drive offloaded stripe update.
 * (asu_update == 1) means update replica/parity exts to parity shards
 * oid		entry->ae_oid
 * iod		stripe_ud->asu_ud_iod,
 * sgl		stripe_ud->asu_ud_sgl,
 * epoch	entry->ae_cur_stripe.as_hi_epoch
 *
 * (asu_remove == 1) means remove replica/parity exts from parity shards
 * oid		entry->ae_oid
 * iod		stripe_ud->asu_remove_iod
 * epoch	remove on epoch range [agg_param->ap_epr.epr_lo, entry->ae_cur_stripe.as_hi_epoch]
 */
struct ec_agg_stripe_ud {
	struct ec_agg_entry	*asu_agg_entry;	/* Associated entry      */
	daos_iod_t		 asu_ud_iod;	/* iod for update replica/parity */
	daos_recx_t		*asu_ud_recxs;	/* recx of update replica/parity */
	d_sg_list_t		 asu_ud_sgl;	/* sgl of update replica/parity */
	uint8_t			*asu_bit_map;	/* Bitmap of cells       */
	unsigned int		 asu_cell_cnt;	/* Count of cells        */
	bool			 asu_recalc;	/* Should recalc parity  */
	daos_iod_t		 asu_remove_iod;/* iod for array remove */
	daos_recx_t		 asu_remove_recx;/* recx for array remove */
	ABT_eventual		 asu_eventual;	/* Eventual for offload  */
	uint32_t		 asu_update:1,
				 asu_remove:1;
};

/* Aggregation state for an object.
 */
struct ec_agg_entry {
	daos_unit_oid_t		 ae_oid;	 /* OID of iteration entry    */
	struct daos_oclass_attr	 ae_oca;	 /* Object class of object    */
	struct obj_ec_codec	*ae_codec;	 /* Encode/decode for oclass  */
	struct ec_agg_stripe_ud	 ae_stripe_ud;	 /* Stripe update struct */
	d_sg_list_t		 ae_sgl;	 /* Mem for entry processing  */
	daos_handle_t		 ae_thdl;	 /* Iterator handle           */
	daos_key_t		 ae_dkey;	 /* Current dkey              */
	uint64_t		 ae_dkey_hash;
	daos_key_t		 ae_akey;	 /* Current akey              */
	daos_size_t		 ae_rsize;	 /* Record size of cur array  */
	struct ec_agg_stripe	 ae_cur_stripe;  /* Struct for current stripe */
	struct ec_agg_par_extent ae_par_extent;	 /* Parity extent             */
	daos_handle_t		 ae_obj_hdl;	 /* Object handle for cur obj */
	struct pl_obj_layout	*ae_obj_layout;
	uint32_t		 ae_rotate_parity:1; /* ec parity rotation or not */
};

/* Parameters used to drive iterate all.
 */
struct ec_agg_param {
	struct ec_agg_pool_info	 ap_pool_info;	 /* pool/cont info            */
	struct ec_agg_entry	 ap_agg_entry;	 /* entry used for each OID   */
	daos_epoch_range_t	 ap_epr;	 /* hi/lo extent threshold    */
	daos_epoch_t		 ap_filter_eph;	 /* Aggregatable filter epoch */
	daos_handle_t		 ap_cont_handle; /* VOS container handle */
	int			(*ap_yield_func)(void *arg); /* yield function*/
	void			*ap_yield_arg;   /* yield argument            */
	uint32_t		 ap_credits_max; /* # of tight loops to yield */
	uint32_t		 ap_credits;     /* # of tight loops          */
	uint32_t		 ap_initialized:1, /* initialized flag */
				 ap_obj_skipped:1; /* skipped obj during aggregation */
};

/* Represents an replicated data extent.
 */
struct ec_agg_extent {
	d_list_t	ae_link;        /* for extents list   */
	daos_recx_t	ae_recx;        /* idx, nr for extent */
	daos_epoch_t	ae_epoch;       /* epoch for extent   */
	bool		ae_hole;        /* extent is a hole   */
};

/* return EC(K) in # records */
static inline unsigned int
ec_age2k(struct ec_agg_entry *age)
{
	return age->ae_oca.u.ec.e_k;
}

/* return EC(P) in # records */
static inline unsigned int
ec_age2p(struct ec_agg_entry *age)
{
	return age->ae_oca.u.ec.e_p;
}

/* return cell size in # records */
static inline unsigned int
ec_age2cs(struct ec_agg_entry *age)
{
	return age->ae_oca.u.ec.e_len;
}

/* return cell size in # bytes */
static inline unsigned int
ec_age2cs_b(struct ec_agg_entry *age)
{
	return ec_age2cs(age) * age->ae_rsize;
}

/* return stripe size in # records */
static inline daos_size_t
ec_age2ss(struct ec_agg_entry *age)
{
	return obj_ec_stripe_rec_nr(&age->ae_oca);
}

static inline uint32_t
ec_age2shard(struct ec_agg_entry *entry)
{
	return entry->ae_oid.id_shard;
}

/* return parity index [0, p - 1] */
static inline uint32_t
ec_age2pidx(struct ec_agg_entry *entry)
{
	uint32_t shard;

	shard = ec_age2shard(entry) % obj_ec_tgt_nr(&entry->ae_oca);

	D_ASSERT(is_ec_parity_shard(shard, entry->ae_dkey_hash, &entry->ae_oca));
	return (obj_ec_shard_off(entry->ae_dkey_hash, &entry->ae_oca, shard) -
		obj_ec_data_tgt_nr(&entry->ae_oca));
}

#define EC_AGE_EPOCH_NO_PARITY		((daos_epoch_t)(~(0ULL)))

/* set the aggregate entry as no parity, before vos_iterate the parity space */
static inline void
ec_age_set_no_parity(struct ec_agg_entry *age)
{
	age->ae_par_extent.ape_epoch = EC_AGE_EPOCH_NO_PARITY;
}

/* check if parity ext exist, after vos_iterate the parity space */
static inline bool
ec_age_with_parity(struct ec_agg_entry *age)
{
	return (age->ae_par_extent.ape_epoch != EC_AGE_EPOCH_NO_PARITY);
}

/* check if existed parity's epoch is higher than all replica exts' epoch */
static inline bool
ec_age_parity_higher(struct ec_agg_entry *age)
{
	return (age->ae_par_extent.ape_epoch >= age->ae_cur_stripe.as_hi_epoch);
}

/* check if hole extent exist, after vos_iterate the replica space */
static inline bool
ec_age_with_hole(struct ec_agg_entry *age)
{
	return age->ae_cur_stripe.as_has_holes;
}

/* Determines if the extent carries over into the next stripe.
 */
static uint64_t
agg_carry_over(struct ec_agg_entry *entry, struct ec_agg_extent *agg_extent)
{
	unsigned int	stripe_size = ec_age2ss(entry);
	daos_off_t	start_stripe = agg_extent->ae_recx.rx_idx / stripe_size;
	daos_off_t	end_stripe = (agg_extent->ae_recx.rx_idx +
				agg_extent->ae_recx.rx_nr - 1) / stripe_size;
	uint64_t	tail_size = 0;

	if (end_stripe > start_stripe) {
		D_ASSERTF(end_stripe - start_stripe == 1,
			  DF_UOID ", recx "DF_RECX" SS="DF_U64", ES="DF_U64
			  ", EC_OCA(k=%d, p=%d, cs=%d)\n",
			  DP_UOID(entry->ae_oid), DP_RECX(agg_extent->ae_recx),
			  start_stripe, end_stripe, entry->ae_oca.u.ec.e_k,
			  entry->ae_oca.u.ec.e_p, entry->ae_oca.u.ec.e_len);

		tail_size = DAOS_RECX_END(agg_extent->ae_recx) -
			    end_stripe * stripe_size;
		/* What if an extent carries over, and the tail is the only
		 * extent in the next stripe? (Answer: we retain it, but this
		 * is okay, since in this case the carryover is a valid
		 * replica for the next stripe)
		 */
	}

	D_DEBUG(DB_TRACE, DF_UOID", recx "DF_RECX" tail_size "DF_U64"\n",
		DP_UOID(entry->ae_oid), DP_RECX(agg_extent->ae_recx),
		tail_size);
	return tail_size;
}

static void
agg_clear_stripe_ud(struct ec_agg_stripe_ud *stripe_ud)
{
	if (stripe_ud->asu_ud_recxs)
		D_FREE(stripe_ud->asu_ud_recxs);
	memset(stripe_ud, 0, sizeof(*stripe_ud));
}

/* Clears the extent list of all extents completed for the processed stripe.
 * Extents that carry over to the next stripe have the prior-stripe prefix
 * trimmed.
 */
static void
agg_clear_extents(struct ec_agg_entry *entry)
{
	struct ec_agg_extent	*extent, *ext_tmp;
	uint64_t		 tail;
	bool			 carry_is_hole = false;

	if (entry->ae_cur_stripe.as_extent_cnt == 0)
		return;

	d_list_for_each_entry_safe(extent, ext_tmp,
				   &entry->ae_cur_stripe.as_dextents,
				   ae_link) {
		/* Check for carry-over extent. */
		tail = agg_carry_over(entry, extent);
		if (extent->ae_hole && tail)
			carry_is_hole = true;

		d_list_del(&extent->ae_link);
		entry->ae_cur_stripe.as_extent_cnt--;
		D_FREE(extent);
	}

	entry->ae_cur_stripe.as_offset = 0U;
	D_ASSERT(entry->ae_cur_stripe.as_extent_cnt == 0);
	entry->ae_cur_stripe.as_hi_epoch = 0UL;
	entry->ae_cur_stripe.as_stripe_fill = 0;
	entry->ae_cur_stripe.as_has_holes = carry_is_hole ? true : false;

	agg_clear_stripe_ud(&entry->ae_stripe_ud);
}

/* Returns the stripe number for the stripe containing ex_lo.
 */
static inline daos_off_t
agg_stripenum(struct ec_agg_entry *entry, daos_off_t ex_lo)
{
	return ex_lo / ec_age2ss(entry);
}

/* Call back for the nested iterator used to find the parity for a stripe.
 */
static int
agg_recx_iter_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		     vos_iter_type_t type, vos_iter_param_t *param,
		     void *cb_arg, unsigned int *acts)
{
	struct ec_agg_entry	*age = (struct ec_agg_entry *)cb_arg;

	D_ASSERT(type == VOS_ITER_RECX);
	D_ASSERT(entry->ie_recx.rx_idx == (PARITY_INDICATOR |
		(age->ae_cur_stripe.as_stripenum * ec_age2cs(age))));
	age->ae_par_extent.ape_recx = entry->ie_recx;
	age->ae_par_extent.ape_epoch = entry->ie_epoch;
	return 0;
}

enum agg_iov_entry {
	AGG_IOV_DATA	= 0,
	AGG_IOV_ODATA,
	AGG_IOV_PARITY,
	AGG_IOV_DIFF,
	AGG_IOV_CNT,
};

/* Allocates an sgl iov_buf at iov_entry offset in the array.
 */
static int
agg_alloc_buf(d_sg_list_t *sgl, size_t ent_buf_len, unsigned int iov_entry,
	      bool align_data)
{
	void	*buf = NULL;
	int	 rc = 0;

	if (align_data) {
		D_ALIGNED_ALLOC(buf, 32, ent_buf_len);
		if (buf == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		D_FREE(sgl->sg_iovs[iov_entry].iov_buf);
		sgl->sg_iovs[iov_entry].iov_buf = buf;
	} else {
		D_REALLOC_NZ(buf, sgl->sg_iovs[iov_entry].iov_buf, ent_buf_len);
		 if (buf == NULL) {
			rc = -DER_NOMEM;
			goto out;
		 }
		sgl->sg_iovs[iov_entry].iov_buf = buf;
	}
	sgl->sg_iovs[iov_entry].iov_len = ent_buf_len;
	sgl->sg_iovs[iov_entry].iov_buf_len = ent_buf_len;
out:
	return rc;
}

/* Prepares the SGL used for VOS I/O and peer target I/O.
 *
 * This function is a no-op if entry's sgl is sufficient for the current
 * object class.
 *
 */
static int
agg_prep_sgl(struct ec_agg_entry *entry)
{
	size_t		 data_buf_len, par_buf_len;
	unsigned int	 len = ec_age2cs(entry);
	unsigned int	 k = ec_age2k(entry);
	unsigned int	 p = ec_age2p(entry);
	int		 rc = 0;

	if (entry->ae_sgl.sg_nr == 0) {
		D_ALLOC_ARRAY(entry->ae_sgl.sg_iovs, AGG_IOV_CNT);
		if (entry->ae_sgl.sg_iovs == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		entry->ae_sgl.sg_nr = AGG_IOV_CNT;
	}
	D_ASSERT(entry->ae_sgl.sg_nr == AGG_IOV_CNT);
	data_buf_len = len * k * entry->ae_rsize;
	if (entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf_len < data_buf_len) {
		rc = agg_alloc_buf(&entry->ae_sgl, data_buf_len, AGG_IOV_DATA,
				   true);
		if (rc)
			goto out;
	}
	if (entry->ae_sgl.sg_iovs[AGG_IOV_ODATA].iov_buf_len < data_buf_len) {
		rc = agg_alloc_buf(&entry->ae_sgl, data_buf_len, AGG_IOV_ODATA,
				   true);
		if (rc)
			goto out;
	}
	if (entry->ae_sgl.sg_iovs[AGG_IOV_DIFF].iov_buf_len <
						len * entry->ae_rsize) {
		rc = agg_alloc_buf(&entry->ae_sgl, len * entry->ae_rsize,
				   AGG_IOV_DIFF, true);
		if (rc)
			goto out;
	}
	par_buf_len = len * p * entry->ae_rsize;
	if (entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf_len < par_buf_len) {
		rc = agg_alloc_buf(&entry->ae_sgl, par_buf_len, AGG_IOV_PARITY,
				   false);
		if (rc)
			goto out;
	}
	return 0;
out:
	d_sgl_fini(&entry->ae_sgl, true);
	return rc;
}

/* Determines if an extent overlaps a cell.
 */
static bool
agg_overlap(uint64_t estart, uint64_t elen, unsigned int cell_idx,
	    unsigned int k, unsigned int len, uint64_t stripenum)
{
	daos_recx_t	recx, cell_recx;

	recx.rx_idx		= estart + k * len * stripenum;
	recx.rx_nr		= elen;
	cell_recx.rx_idx	= k * len * stripenum + len * cell_idx;
	cell_recx.rx_nr		= len;

	return DAOS_RECX_PTR_OVERLAP(&recx, &cell_recx);
}

static unsigned int
agg_count_cells(uint8_t *fcbit_map, uint8_t *tbit_map, uint64_t estart,
		uint64_t elen, unsigned int k, uint64_t len,
		uint64_t stripenum, unsigned int *full_cell_cnt)
{
	uint64_t	i;
	unsigned int	cell_cnt = 0;

	for (i = 0; i < k; i++) {
		if (i * len >= estart &&  estart + elen >= (i + 1) * len) {
			setbit(tbit_map, i);
			if (full_cell_cnt) {
				setbit(fcbit_map, i);
				(*full_cell_cnt)++;
			}
			cell_cnt++;
		} else if (agg_overlap(estart, elen, i, k, len, stripenum)) {
			if (!isset(tbit_map, i)) {
				setbit(tbit_map, i);
				cell_cnt++;
			}
		}
	}

	return cell_cnt;
}

/* Initializes the object handle of for the object represented by the entry.
 * No way to do this until pool handle uuid and container handle uuid are
 * initialized and share to other servers at higher(pool/container) layer.
 */
static int
agg_get_obj_handle(struct ec_agg_entry *entry)
{
	struct ec_agg_param	*agg_param;
	int			rc;

	if (daos_handle_is_valid(entry->ae_obj_hdl))
		return 0;

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	/* NB: entry::ae_obj_hdl will be closed externally */
	rc = dsc_obj_open(agg_param->ap_pool_info.api_cont_hdl,
			  entry->ae_oid.id_pub, DAOS_OO_RW,
			  &entry->ae_obj_hdl);
	return rc;
}

/* Fetches the old data for the cells in the stripe undergoing a partial parity
 * update, or a parity recalculation. For update, the bit_map indicates the
 * cells that are present as replicas. In this case the parity epoch is used
 * for the fetch. For recalc, the bit_map indicates the cells that are not fully
 * populated as replicas. In this case, the highest replica epoch is used.
 */
static int
agg_fetch_odata_cells(struct ec_agg_entry *entry, uint8_t *bit_map,
		      unsigned int cell_cnt, bool is_recalc)
{
	daos_iod_t		 iod = { 0 };
	d_sg_list_t		 sgl = { 0 };
	daos_epoch_t		 epoch = { 0 };	/* epoch used for data fetch */
	daos_recx_t		*recxs = NULL;
	struct ec_agg_stripe	*stripe = &entry->ae_cur_stripe;
	unsigned char		*buf;
	uint64_t		 cell_b = ec_age2cs_b(entry);
	unsigned int		 len = ec_age2cs(entry);
	unsigned int		 k = ec_age2k(entry);
	unsigned int		 i, j;
	int			 rc = 0;

	D_ALLOC_ARRAY(recxs, cell_cnt);
	if (recxs == NULL)
		return -DER_NOMEM;

	for (i = 0, j = 0; i < k; i++) {
		if (!isset(bit_map, i))
			continue;

		recxs[j].rx_idx = stripe->as_stripenum * k * len + i * len;
		recxs[j++].rx_nr = len;
	}
	D_ASSERT(j == cell_cnt);

	iod.iod_name	= entry->ae_akey;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= entry->ae_rsize;
	iod.iod_nr	= cell_cnt;
	iod.iod_recxs	= recxs;

	D_ALLOC_ARRAY(sgl.sg_iovs, cell_cnt);
	if (sgl.sg_iovs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	sgl.sg_nr = cell_cnt;
	buf = entry->ae_sgl.sg_iovs[AGG_IOV_ODATA].iov_buf;
	for (i = 0; i < cell_cnt; i++)
		d_iov_set(&sgl.sg_iovs[i], &buf[i * cell_b], cell_b);

	rc = agg_get_obj_handle(entry);
	if (rc) {
		D_ERROR("Failed to open object: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	epoch = is_recalc ? stripe->as_hi_epoch :
		entry->ae_par_extent.ape_epoch;
	rc = dsc_obj_fetch(entry->ae_obj_hdl, epoch, &entry->ae_dkey, 1, &iod,
			   &sgl, NULL, DIOF_FOR_EC_AGG, NULL, NULL);
	if (rc)
		D_ERROR("dsc_obj_fetch failed: "DF_RC"\n", DP_RC(rc));

out:
	D_FREE(recxs);
	D_FREE(sgl.sg_iovs);
	return rc;
}

/* Fetches the full data stripe (called when replicas form a full stripe).
 */
static int
agg_fetch_data_stripe(struct ec_agg_entry *entry)
{
	daos_iod_t		 iod = { 0 };
	daos_recx_t		 recx = { 0 };
	struct ec_agg_param	*agg_param;
	unsigned int		 len = ec_age2cs(entry);
	unsigned int		 k = ec_age2k(entry);
	int			 rc = 0;

	rc = agg_prep_sgl(entry);
	if (rc)
		goto out;

	recx.rx_idx = entry->ae_cur_stripe.as_stripenum * k * len;
	recx.rx_nr = k * len;
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	entry->ae_sgl.sg_nr = 1;
	entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_len =
						len * k * entry->ae_rsize;

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = vos_obj_fetch(agg_param->ap_cont_handle, entry->ae_oid,
			   entry->ae_cur_stripe.as_hi_epoch, 0, &entry->ae_dkey,
			   1, &iod, &entry->ae_sgl);
	if (rc)
		D_ERROR(DF_UOID" vos_obj_fetch "DF_RECX" failed: "DF_RC"\n",
			DP_UOID(entry->ae_oid), DP_RECX(recx), DP_RC(rc));
	entry->ae_sgl.sg_nr = AGG_IOV_CNT;
out:
	return rc;
}

/* Xstream offload function for encoding new parity from full stripe of
 * replicas.
 */
static void
agg_encode_full_stripe_ult(void *arg)
{
	struct ec_agg_stripe_ud	*stripe_ud =
					(struct ec_agg_stripe_ud *)arg;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	unsigned int		 k = ec_age2k(entry);
	unsigned int		 p = ec_age2p(entry);
	unsigned int		 cell_bytes = ec_age2cs_b(entry);
	unsigned char		*data[OBJ_EC_MAX_K];
	unsigned char		*parity_bufs[OBJ_EC_MAX_P];
	unsigned char		*buf;
	int			 i, rc = 0;

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	for (i = 0; i < k; i++)
		data[i] = &buf[i * cell_bytes];

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	for (i = 0; i < p; i++)
		parity_bufs[i] = &buf[i * cell_bytes];

	ec_encode_data(cell_bytes, k, p, entry->ae_codec->ec_gftbls, data,
		       parity_bufs);

	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

/* Encodes a full stripe. Called when replicas form a full stripe.
 */
static int
agg_encode_full_stripe(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud		*stripe_ud = &entry->ae_stripe_ud;
	int				*status;
	int				tid, rc = 0;

	stripe_ud->asu_agg_entry = entry;
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = ABT_eventual_create(sizeof(*status), &stripe_ud->asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	rc = dss_ult_create(agg_encode_full_stripe_ult, stripe_ud,
			    DSS_XS_OFFLOAD, tid, 0, NULL);
	if (rc)
		goto ev_out;

	rc = ABT_eventual_wait(stripe_ud->asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		rc = *status;
	else
		rc = 0;

ev_out:
	ABT_eventual_free(&stripe_ud->asu_eventual);
out:
	return rc;
}

/* Driver function for full_stripe encode. Fetches the data and then invokes
 * second function to encode the parity.
 */
static int
agg_encode_local_parity(struct ec_agg_entry *entry)
{
	int rc = 0;

	rc = agg_fetch_data_stripe(entry);
	if (rc)
		goto out;
	agg_encode_full_stripe(entry);
out:
	return rc;
}

/* True if all extents within the stripe are at a higher epoch than
 * the parity for the stripe.
 */
static bool
ec_age_data_is_newer(struct ec_agg_entry *entry)
{
	struct ec_agg_extent	*agg_extent;

	d_list_for_each_entry(agg_extent, &entry->ae_cur_stripe.as_dextents,
			      ae_link) {
		if (agg_extent->ae_epoch <= entry->ae_par_extent.ape_epoch)
			return false;
	}
	return true;
}

/* Determines if the replicas present for the current stripe of object entry
 * constitute a full stripe. If parity exists for the stripe, the replicas
 * making up the full stripe must be a higher epoch than the parity.
 */
static bool
ec_age_stripe_full(struct ec_agg_entry *entry, bool has_parity)
{
	bool	is_filled;

	D_ASSERT(entry->ae_cur_stripe.as_stripe_fill <= ec_age2ss(entry));
	is_filled = (entry->ae_cur_stripe.as_stripe_fill == ec_age2ss(entry));

	return is_filled && (!has_parity || ec_age_data_is_newer(entry));
}

/* Retrieves the local replica extents from VOS, for the cells indicated
 * by the bit_map.
 */
static int
agg_fetch_local_extents(struct ec_agg_entry *entry, uint8_t *bit_map,
			unsigned int cell_cnt, bool is_recalc)
{
	daos_iod_t		 iod = { 0 };
	d_sg_list_t		 sgl = { 0 };
	daos_recx_t		*recxs = NULL;
	unsigned char		*buf = NULL;
	struct ec_agg_param	*agg_param;
	uint64_t		 cell_bytes = ec_age2cs_b(entry);
	uint32_t		 len = ec_age2cs(entry);
	uint32_t		 k = ec_age2k(entry);
	uint32_t		 pidx = ec_age2pidx(entry);
	uint32_t		 i, j;
	int			 rc = 0;

	D_ALLOC_ARRAY(recxs, is_recalc ? cell_cnt : cell_cnt + 1);
	if (recxs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	for (i = 0, j = 0; i < k; i++)
		if (isset(bit_map, i)) {
			recxs[j].rx_idx =
			  entry->ae_cur_stripe.as_stripenum * k * len + i * len;
			recxs[j++].rx_nr = len;
		}
	D_ASSERT(j == cell_cnt);

	/* Parity is either updated (existing parity is updated),
	 * or recalculated (generated from the entire stripe.
	 *
	 * Only need to fetch local parity if not recalculating it.
	 */
	if (!is_recalc) {
		recxs[cell_cnt].rx_idx = PARITY_INDICATOR |
				(entry->ae_cur_stripe.as_stripenum * len);
		recxs[cell_cnt].rx_nr = len;
	}

	D_ALLOC_ARRAY(sgl.sg_iovs, cell_cnt + 1);
	if (sgl.sg_iovs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	sgl.sg_nr =  is_recalc ? cell_cnt : cell_cnt + 1;
	buf = entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	for (i = 0; i < cell_cnt; i++)
		d_iov_set(&sgl.sg_iovs[i], &buf[i * cell_bytes], cell_bytes);

	/* fetch the local parity */
	if (!is_recalc) {
		buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
		d_iov_set(&sgl.sg_iovs[cell_cnt], &buf[pidx * cell_bytes],
			  cell_bytes);
	}

	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = is_recalc ? cell_cnt : cell_cnt + 1;
	iod.iod_recxs = recxs;
	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = vos_obj_fetch(agg_param->ap_cont_handle, entry->ae_oid,
			   entry->ae_cur_stripe.as_hi_epoch, 0,
			   &entry->ae_dkey, 1, &iod, &sgl);
	if (rc)
		D_ERROR("vos_obj_fetch failed: "DF_RC"\n", DP_RC(rc));

out:
	D_FREE(recxs);
	D_FREE(sgl.sg_iovs);
	return rc;
}

/* Fetch parity cell for the stripe from the peer parity node.
 */
static int
agg_fetch_remote_parity(struct ec_agg_entry *entry)
{
	daos_iod_t	 iod = { };
	daos_recx_t	 recx = { };
	d_sg_list_t	 sgl = { };
	d_iov_t		 iov = { };
	unsigned char	*buf = NULL;
	uint32_t	 len = ec_age2cs(entry);
	uint64_t	 cell_b = ec_age2cs_b(entry);
	uint32_t	 shard  = ec_age2shard(entry);
	uint32_t	 pidx, peer_shard;
	int		 i, rc = 0;

	/* Only called when p > 1. */
	pidx = ec_age2pidx(entry);

	/* set up the iod */
	recx.rx_idx = (entry->ae_cur_stripe.as_stripenum * len)
							| PARITY_INDICATOR;
	recx.rx_nr = len;
	iod.iod_recxs = &recx;
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = 1;

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	for (i = 0; i < obj_ec_parity_tgt_nr(&entry->ae_oca); i++) {
		if (i == pidx)
			continue;
		d_iov_set(&iov, &buf[i * cell_b], cell_b);
		peer_shard = obj_ec_parity_shard(entry->ae_dkey_hash,
						 &entry->ae_oca,
						 shard / obj_ec_tgt_nr(&entry->ae_oca), i);
		rc = dsc_obj_fetch(entry->ae_obj_hdl,
				   entry->ae_par_extent.ape_epoch,
				   &entry->ae_dkey, 1, &iod, &sgl, NULL,
				   DIOF_TO_SPEC_SHARD | DIOF_FOR_EC_AGG,
				   &peer_shard, NULL);
		D_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE, DF_UOID
			 " fetch parity from peer shard %d, "DF_RC".\n",
			 DP_UOID(entry->ae_oid), peer_shard, DP_RC(rc));
		if (rc)
			goto out;
	}

out:
	return rc;
}

/** Pre-process the diff data to zero the non-existed replica extends */
static void
agg_diff_preprocess(struct ec_agg_entry *entry, unsigned char *diff,
		    unsigned int cell_idx)
{
	struct ec_agg_extent	*extent;
	unsigned int		 len = ec_age2cs(entry);
	unsigned int		 k = ec_age2k(entry);
	uint64_t		 ss, estart, eend, elen;
	uint64_t		 cell_start, cell_end;
	uint64_t		 rsize = entry->ae_rsize;
	uint64_t		 hole_off, hole_end;

	ss = k * len * entry->ae_cur_stripe.as_stripenum;
	cell_start = (uint64_t)cell_idx * len;
	cell_end = cell_start + len;
	hole_off = 0;
	d_list_for_each_entry(extent, &entry->ae_cur_stripe.as_dextents,
			      ae_link) {
		D_ASSERT(!extent->ae_hole);
		if (extent->ae_epoch <= entry->ae_par_extent.ape_epoch)
			continue;
		D_ASSERT(extent->ae_recx.rx_idx >= ss);
		estart = extent->ae_recx.rx_idx - ss;
		elen = extent->ae_recx.rx_nr;
		eend = estart + elen;
		if (estart >= cell_end)
			break;
		if (eend <= cell_start)
			continue;
		hole_end = cell_start + hole_off;
		if (estart > hole_end) {
			memset(diff + hole_off * rsize, 0,
			       (estart - hole_end) * rsize);
			D_DEBUG(DB_TRACE, DF_UOID" zero [off "DF_U64
				", len "DF_U64"]\n", DP_UOID(entry->ae_oid),
				hole_off, estart - hole_end);
		}
		hole_off = eend - cell_start;
	}
	if (hole_off > 0 && hole_off < len) {
		memset(diff + hole_off * rsize, 0,
		       (len - hole_off) * rsize);
		D_DEBUG(DB_TRACE, DF_UOID" zero [off "DF_U64", len"DF_U64"]\n",
			DP_UOID(entry->ae_oid), hole_off, len - hole_off);
	}
}

/* Performs an incremental update of the existing parity for the stripe.
 */
static int
agg_update_parity(struct ec_agg_entry *entry, uint8_t *bit_map,
		  unsigned int cell_cnt)
{
	unsigned int	 k = ec_age2k(entry);
	unsigned int	 p = ec_age2p(entry);
	unsigned int	 cell_bytes = ec_age2cs_b(entry);
	unsigned char	*parity_bufs[OBJ_EC_MAX_P];
	unsigned char	*vects[3];
	unsigned char	*buf;
	unsigned char	*obuf;
	unsigned char	*old;
	unsigned char	*new;
	unsigned char	*diff;
	int		 i, j, rc = 0;

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	for (i = 0; i < p; i++)
		parity_bufs[i] = &buf[i * cell_bytes];

	obuf = entry->ae_sgl.sg_iovs[AGG_IOV_ODATA].iov_buf;
	buf  = entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	diff = entry->ae_sgl.sg_iovs[AGG_IOV_DIFF].iov_buf;

	for (i = 0, j = 0; i < cell_cnt; i++) {
		old = &obuf[i * cell_bytes];
		new = &buf[i * cell_bytes];
		vects[0] = old;
		vects[1] = new;
		vects[2] = diff;
		rc = xor_gen(3, cell_bytes, (void **)vects);
		if (rc)
			goto out;
		while (!isset(bit_map, j))
			j++;
		agg_diff_preprocess(entry, diff, j);
		ec_encode_data_update(cell_bytes, k, p, j,
				      entry->ae_codec->ec_gftbls, diff,
				      parity_bufs);
	}
out:
	return rc;
}

/* Recalculates new parity for partial stripe updates. Used when replica
 * fill the majority of the cells.
 */
static void
agg_recalc_parity(struct ec_agg_entry *entry, uint8_t *bit_map,
		  unsigned cell_cnt)
{
	unsigned int	 k = ec_age2k(entry);
	unsigned int	 p = ec_age2p(entry);
	unsigned int	 cell_bytes = ec_age2cs_b(entry);
	unsigned char	*parity_bufs[OBJ_EC_MAX_P];
	unsigned char	*data[OBJ_EC_MAX_K];
	unsigned char	*buf;
	unsigned char	*rbuf = entry->ae_sgl.sg_iovs[AGG_IOV_ODATA].iov_buf;
	unsigned char	*lbuf = entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	int		 i, r, l = 0;

	for (i = 0, r = 0; i < k; i++) {
		if (isset(bit_map, i))
			data[i] = &rbuf[r++ * cell_bytes];
		 else
			data[i] = &lbuf[l++ * cell_bytes];
	}
	D_ASSERT(r == cell_cnt);
	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	D_ASSERT(p > 0);
	for (i = 0; i < p; i++)
		parity_bufs[i] = &buf[i * cell_bytes];

	ec_encode_data(cell_bytes, k, p, entry->ae_codec->ec_gftbls, data,
		       parity_bufs);
}

/* Xstream offload function for partial stripe update. Fetches the old data
 * from the data target(s) and updates the parity.
 */
static void
agg_process_partial_stripe_ult(void *arg)
{
	struct ec_agg_stripe_ud	*stripe_ud = (struct ec_agg_stripe_ud *)arg;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	uint8_t			*bit_map = stripe_ud->asu_bit_map;
	unsigned int		 p = ec_age2p(entry);
	unsigned int		 cell_cnt = stripe_ud->asu_cell_cnt;
	int			 rc = 0;

	/* Fetch the data cells on other shards. For parity update,
	 * the bitmap is set for the same cells as are replicated.
	 */
	rc = agg_fetch_odata_cells(entry, bit_map, cell_cnt,
				   stripe_ud->asu_recalc);
	if (rc)
		goto out;

	if (p > 1 && !stripe_ud->asu_recalc) {
		rc = agg_fetch_remote_parity(entry);
		if (rc)
			goto out;
	}

	if (stripe_ud->asu_recalc)
		agg_recalc_parity(entry, bit_map, cell_cnt);
	else
		rc = agg_update_parity(entry, bit_map, cell_cnt);

out:
	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

/* Driver function for partial stripe update. Fetches the data and then invokes
 * second function to update the parity.
 */
static int
agg_process_partial_stripe(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud	*stripe_ud = &entry->ae_stripe_ud;
	struct ec_agg_extent	*extent;
	int			*status;
	uint8_t			*bit_map = NULL;
	uint8_t			 fcbit_map[OBJ_TGT_BITMAP_LEN] = {0};
	uint8_t			 tbit_map[OBJ_TGT_BITMAP_LEN] = {0};
	unsigned int		 len = ec_age2cs(entry);
	unsigned int		 k = ec_age2k(entry);
	unsigned int		 i, full_cell_cnt = 0;
	unsigned int		 cell_cnt = 0;
	uint64_t		 ss;
	uint64_t		 estart, elen = 0;
	uint64_t		 eend = 0;
	bool			 has_old_replicas = false;
	int			 tid, rc = 0;

	/* For each contiguous extent, constructable from the extent list,
	 * determine how many full cells, and how many cells overall,
	 * are contained in the constructed extent.
	 */
	ss = k * len * entry->ae_cur_stripe.as_stripenum;
	estart = entry->ae_cur_stripe.as_offset;
	d_list_for_each_entry(extent, &entry->ae_cur_stripe.as_dextents,
			      ae_link) {
		D_ASSERT(!extent->ae_hole);
		if (extent->ae_epoch <= entry->ae_par_extent.ape_epoch) {
			has_old_replicas = true;
			continue;
		}
		if (estart == extent->ae_recx.rx_idx - ss) {
			eend = estart + extent->ae_recx.rx_nr;
			elen = extent->ae_recx.rx_nr;
			continue;
		}
		if (extent->ae_recx.rx_idx - ss > eend) {
			cell_cnt += agg_count_cells(fcbit_map, tbit_map, estart,
						    elen, k, len, entry->ae_cur_stripe.as_stripenum,
						    &full_cell_cnt);
			estart = extent->ae_recx.rx_idx - ss;
			elen = 0;
		}
		elen += extent->ae_recx.rx_nr;
		eend += extent->ae_recx.rx_nr;
	}
	cell_cnt += agg_count_cells(fcbit_map, tbit_map, estart, elen, k, len,
				    entry->ae_cur_stripe.as_stripenum,
				    &full_cell_cnt);

	if (full_cell_cnt >= k / 2 || cell_cnt == k || has_old_replicas) {
		stripe_ud->asu_recalc = true;
		cell_cnt = full_cell_cnt;
		bit_map = fcbit_map;
	} else
		bit_map = tbit_map;

	rc = agg_prep_sgl(entry);
	if (rc)
		goto out;
	/* cell_cnt is zero if all cells are partial filled */
	if (cell_cnt)
		rc = agg_fetch_local_extents(entry, bit_map, cell_cnt,
					     stripe_ud->asu_recalc);
	if (rc)
		goto out;

	if (stripe_ud->asu_recalc) {
		for (i = 0; i < k; i++) {
			if (isset(bit_map, i))
				clrbit(bit_map, i);
			else
				setbit(bit_map, i);
		}
		cell_cnt = k - cell_cnt;
	}

	stripe_ud->asu_agg_entry = entry;
	stripe_ud->asu_bit_map = bit_map;
	stripe_ud->asu_cell_cnt = cell_cnt;

	rc = ABT_eventual_create(sizeof(*status), &stripe_ud->asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_create(agg_process_partial_stripe_ult, stripe_ud,
			    DSS_XS_IOFW, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud->asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0) {
		rc = *status;
		goto ev_out;
	}

ev_out:
	ABT_eventual_free(&stripe_ud->asu_eventual);

out:
	return rc;
}

/* Invokes helper function to send the generated parity and the stripe number
 * to the peer parity target.
 */
static int
agg_stripe_set_write_parity(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud	*stripe_ud = &entry->ae_stripe_ud;
	daos_iod_t		*iod = &stripe_ud->asu_ud_iod;
	d_sg_list_t		*sgl = &stripe_ud->asu_ud_sgl;
	uint32_t		 len = ec_age2cs(entry);
	uint32_t		 i, p = ec_age2p(entry);
	int			 rc = 0;

	D_ASSERT(entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf);

	stripe_ud->asu_agg_entry = entry;
	D_ALLOC_ARRAY(stripe_ud->asu_ud_recxs, p);
	if (stripe_ud->asu_ud_recxs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	iod->iod_name	= entry->ae_akey;
	iod->iod_type	= DAOS_IOD_ARRAY;
	iod->iod_size	= entry->ae_rsize;
	iod->iod_nr	= p;
	iod->iod_recxs	= stripe_ud->asu_ud_recxs;
	for (i = 0; i < p; i++) {
		iod->iod_recxs[i].rx_idx = (entry->ae_cur_stripe.as_stripenum * len) |
					   PARITY_INDICATOR;
		iod->iod_recxs[i].rx_nr = len;
	}
	sgl->sg_nr	= 1;
	sgl->sg_iovs	= &entry->ae_sgl.sg_iovs[AGG_IOV_PARITY];
	sgl->sg_iovs->iov_len = ec_age2cs_b(entry) * p;
	sgl->sg_iovs->iov_buf_len = sgl->sg_iovs->iov_len;
	stripe_ud->asu_update = 1;

out:
	return rc;
}

/* fetch non-punched replica exts for agg_process_holes */
static void
agg_fetch_replica_ult(void *arg)
{
	struct ec_agg_stripe_ud	*stripe_ud = (struct ec_agg_stripe_ud *)arg;
	daos_iod_t		*iod = &stripe_ud->asu_ud_iod;
	d_sg_list_t		*sgl = &stripe_ud->asu_ud_sgl;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	struct ec_agg_extent	*agg_extent;
	uint32_t		 len = ec_age2cs(entry);
	uint64_t		 cell_b = ec_age2cs_b(entry);
	uint64_t		 k = ec_age2k(entry);
	uint64_t		 ss = entry->ae_cur_stripe.as_stripenum *
					k * len;
	uint64_t		 last_ext_end = 0;
	uint64_t		 ext_cnt = 0;
	uint64_t		 ext_tot_len = 0;
	int			 rc = 0;
	bool			 valid_hole = false;

	/* Process extent list to find what to re-replicate -- build recx array
	 */
	d_list_for_each_entry(agg_extent,
			      &entry->ae_cur_stripe.as_dextents, ae_link) {
		if (agg_extent->ae_epoch < entry->ae_par_extent.ape_epoch)
			continue;
		if (agg_extent->ae_hole)
			valid_hole = true;
		if (agg_extent->ae_recx.rx_idx - ss > last_ext_end) {
			stripe_ud->asu_ud_recxs[ext_cnt].rx_idx =
				ss + last_ext_end;
			stripe_ud->asu_ud_recxs[ext_cnt].rx_nr =
				agg_extent->ae_recx.rx_idx - ss -
				last_ext_end;
			ext_tot_len +=
			stripe_ud->asu_ud_recxs[ext_cnt++].rx_nr;
		}
		last_ext_end = agg_extent->ae_recx.rx_idx +
			agg_extent->ae_recx.rx_nr - ss;
		if (last_ext_end >= (uint64_t)k * len)
			break;
	}

	if (!valid_hole)
		goto out;

	if (last_ext_end < k * len) {
		stripe_ud->asu_ud_recxs[ext_cnt].rx_idx = ss + last_ext_end;
		stripe_ud->asu_ud_recxs[ext_cnt].rx_nr = k * len - last_ext_end;
		ext_tot_len += stripe_ud->asu_ud_recxs[ext_cnt++].rx_nr;
	}
	stripe_ud->asu_cell_cnt = ext_cnt;
	iod->iod_name = entry->ae_akey;
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_size = entry->ae_rsize;
	iod->iod_nr = ext_cnt;
	iod->iod_recxs = stripe_ud->asu_ud_recxs;
	sgl->sg_nr = 1;
	sgl->sg_iovs = &entry->ae_sgl.sg_iovs[AGG_IOV_DATA];
	sgl->sg_iovs->iov_len = ext_tot_len * entry->ae_rsize;
	D_ASSERT(entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_len <= k * cell_b);
	if (ext_cnt) {
		rc = dsc_obj_fetch(entry->ae_obj_hdl, entry->ae_cur_stripe.as_hi_epoch,
				   &entry->ae_dkey, 1, iod, sgl, NULL, DIOF_FOR_EC_AGG, NULL, NULL);
		if (rc) {
			D_ERROR("dsc_obj_fetch failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
	}

out:
	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

static void
agg_stripe_set_remove(struct ec_agg_entry *entry, bool parity_ext)
{
	struct ec_agg_stripe_ud	*stripe_ud = &entry->ae_stripe_ud;

	stripe_ud->asu_remove_iod.iod_name = entry->ae_akey;
	stripe_ud->asu_remove_iod.iod_type = DAOS_IOD_ARRAY;
	stripe_ud->asu_remove_iod.iod_size = entry->ae_rsize;
	stripe_ud->asu_remove_iod.iod_nr = 1;
	stripe_ud->asu_remove_iod.iod_recxs = &stripe_ud->asu_remove_recx;
	if (parity_ext) {
		stripe_ud->asu_remove_recx.rx_nr = ec_age2cs(entry);
		stripe_ud->asu_remove_recx.rx_idx =
			(entry->ae_cur_stripe.as_stripenum * ec_age2cs(entry)) | PARITY_INDICATOR;
	} else {
		stripe_ud->asu_remove_recx.rx_nr = ec_age2ss(entry);
		stripe_ud->asu_remove_recx.rx_idx =
			entry->ae_cur_stripe.as_stripenum * ec_age2ss(entry);
	}
	stripe_ud->asu_remove = 1;
}

static int
agg_process_holes(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud	*stripe_ud = &entry->ae_stripe_ud;
	daos_iod_t		*iod = &stripe_ud->asu_ud_iod;
	int			 tid, rc = 0;
	int			*status;

	D_ALLOC_ARRAY(stripe_ud->asu_ud_recxs,
		      entry->ae_cur_stripe.as_extent_cnt + 1);
	if (stripe_ud->asu_ud_recxs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	stripe_ud->asu_agg_entry = entry;
	rc = agg_get_obj_handle(entry);
	if (rc) {
		D_ERROR("Failed to open object: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	rc = agg_prep_sgl(entry);
	if (rc)
		goto out;

	rc = ABT_eventual_create(sizeof(*status), &stripe_ud->asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_create(agg_fetch_replica_ult, stripe_ud,
			    DSS_XS_IOFW, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud->asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		D_GOTO(ev_out, rc = *status);

	/* Update local vos with fetched non-punched replicate, and remove parity ext
	 * The iod/sgl already set in agg_fetch_replica_ult.
	 */
	if (iod->iod_nr) {
		stripe_ud->asu_update = 1;
		agg_stripe_set_remove(entry, true);
	}

ev_out:
	ABT_eventual_free(&stripe_ud->asu_eventual);
out:
	return rc;
}

static int
check_tx(daos_handle_t th, int rc)
{
	/** if we are not using a DTX, no restart is possible */
	if (daos_handle_is_valid(th)) {
		int ret;

		if (rc == -DER_TX_RESTART) {
			/** restart the TX handle */
			rc = dsc_tx_restart(th);
			if (rc) {
				/** restart failed, so just fail */
				D_ERROR("dsc_tx_restart() fail, "DF_RC"\n", DP_RC(rc));
			} else {
				/** restart succeeded, so return restart code */
				return -DER_TX_RESTART;
			}
		}

		/** on success or non-restart errors, close the handle */
		ret = dsc_tx_close(th);
		if (ret) {
			D_ERROR("dsc_tx_close() fail, "DF_RC"\n", DP_RC(ret));
			if (rc == 0)
				rc = ret;
		}
	}

	return rc;
}

static void
agg_peer_update_ult(void *arg)
{
	struct ec_agg_stripe_ud	*stripe_ud = (struct ec_agg_stripe_ud *)arg;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	struct ec_agg_param	*agg_param;
	daos_handle_t		 oh = entry->ae_obj_hdl;
	daos_handle_t		 th;
	int			 rc;

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = dsc_tx_open(agg_param->ap_pool_info.api_cont_hdl, entry->ae_cur_stripe.as_hi_epoch,
			 DAOS_TF_ZERO_COPY | DAOS_TF_SPEC_EPOCH, &th);
	if (rc) {
		D_ERROR(DF_UOID" dsc_tx_open fail, "DF_RC"\n", DP_UOID(entry->ae_oid), DP_RC(rc));
		goto out;
	}

tx_restart:
	if (stripe_ud->asu_update) {
		rc = dsc_obj_update(oh, th, 0, &entry->ae_dkey, 1, DIOF_FOR_EC_AGG,
				    &stripe_ud->asu_ud_iod, &stripe_ud->asu_ud_sgl, NULL);
		if (rc) {
			D_ERROR(DF_UOID" dsc_obj_update for asu_update fail, "DF_RC"\n",
				DP_UOID(entry->ae_oid), DP_RC(rc));
			goto tx_close;
		}
	}

	D_ASSERT(stripe_ud->asu_remove);
	rc = dsc_obj_update(oh, th, 0, &entry->ae_dkey, 1, DIOF_FOR_EC_AGG | DIOF_EC_AGG_REMOVE,
			    &stripe_ud->asu_remove_iod, NULL, (void *)agg_param->ap_epr.epr_lo);
	if (rc) {
		D_ERROR(DF_UOID" dsc_obj_update for asu_remove fail, "DF_RC"\n",
			DP_UOID(entry->ae_oid), DP_RC(rc));
		goto tx_close;
	}

	rc = dsc_tx_commit(th, 0);
	if (rc) {
		if (rc != -DER_TX_RESTART)
			D_ERROR(DF_UOID" dsc_tx_commit fail, "DF_RC"\n",
				DP_UOID(entry->ae_oid), DP_RC(rc));
		goto tx_close;
	}

tx_close:
	rc = check_tx(th, rc);
	if (rc == -DER_TX_RESTART) {
		D_ERROR(DF_UOID" check_tx fail, "DF_RC"\n",
			DP_UOID(entry->ae_oid), DP_RC(rc));
		goto tx_restart;
	}
out:
	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

static int
agg_peer_update(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud	*stripe_ud = &entry->ae_stripe_ud;
	int			*status;
	int			 tid, rc = 0;

	rc = agg_get_obj_handle(entry);
	if (rc) {
		D_ERROR("Failed to open object: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = ABT_eventual_create(sizeof(*status), &stripe_ud->asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_create(agg_peer_update_ult, stripe_ud, DSS_XS_IOFW, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud->asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		D_GOTO(ev_out, rc = *status);

ev_out:
	ABT_eventual_free(&stripe_ud->asu_eventual);
out:
	return rc;
}

/* Process the prior stripe. Invoked when the iterator has moved to the first
 * extent in the subsequent.
 */
static int
agg_process_stripe(struct ec_agg_param *agg_param, struct ec_agg_entry *entry)
{
	vos_iter_param_t	iter_param = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	int			rc = 0;

	if (DAOS_FAIL_CHECK(DAOS_FORCE_EC_AGG_FAIL))
		D_GOTO(out, rc = -DER_DATA_LOSS);

	/* Query the parity, entry->ae_par_extent.ape_epoch will be set to
	 * parity ext epoch if exist.
	 */
	iter_param.ip_hdl		= DAOS_HDL_INVAL;
	/* set epr_lo as zero to pass-through possibly existed snapshot
	 * between agg_param->ap_epr.epr_lo and .epr_hi.
	 */
	iter_param.ip_epr.epr_lo	= 0;
	iter_param.ip_epr.epr_hi	= agg_param->ap_epr.epr_hi;
	iter_param.ip_ih		= entry->ae_thdl;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_nr	= ec_age2cs(entry);
	iter_param.ip_recx.rx_idx	= PARITY_INDICATOR |
					  (entry->ae_cur_stripe.as_stripenum *
					   iter_param.ip_recx.rx_nr);
	ec_age_set_no_parity(entry);
	rc = vos_iterate(&iter_param, VOS_ITER_RECX, false, &anchors,
			 agg_recx_iter_pre_cb, NULL, entry, NULL);
	D_DEBUG(DB_EPC, "Querying parity for stripe: %lu, offset: "DF_X64
		", "DF_RC"\n", entry->ae_cur_stripe.as_stripenum,
		iter_param.ip_recx.rx_idx, DP_RC(rc));
	if (rc != 0)
		goto out;

	/* with parity and higher than replicas, delete the old replica */
	if (ec_age_with_parity(entry) && ec_age_parity_higher(entry)) {
		D_DEBUG(DB_EPC, "delete replica for stripe: %lu,"
			DF_U64"/"DF_U64" eph "DF_X64" >= "DF_X64"\n",
			entry->ae_cur_stripe.as_stripenum,
			iter_param.ip_recx.rx_idx,
			iter_param.ip_recx.rx_nr,
			entry->ae_par_extent.ape_epoch,
			entry->ae_cur_stripe.as_hi_epoch);
		agg_stripe_set_remove(entry, false);
		goto peer_update;
	}

	if (ec_age_stripe_full(entry, ec_age_with_parity(entry))) {
		/* Replicas constitute a full stripe, 1) no parity, or 2) with parity
		 * and all replica extents are newer than parity.
		 */
		rc = agg_encode_local_parity(entry);
		if (rc) {
			D_ERROR(DF_UOID" agg_encode_local_parity fail: "DF_RC"\n",
				DP_UOID(entry->ae_oid), DP_RC(rc));
			goto out;
		}
	} else {
		/* No parity, partial-stripe worth of replica, nothing to do */
		if (!ec_age_with_parity(entry))
			goto out;

		/* With parity and some newer partial replicas, possibly holes */
		if (ec_age_with_hole(entry)) {
			/* With parity and newer punch ext (hole). Fetch non-punched replica exts
			 * and update to parity shards, remove parity ext.
			 */
			rc = agg_process_holes(entry);
			if (rc == 0) {
				goto peer_update;
			} else {
				D_ERROR(DF_UOID" agg_process_holes fail: "DF_RC"\n",
					DP_UOID(entry->ae_oid), DP_RC(rc));
				goto out;
			}
		} else {
			/* With parity and newer replica ext (partial overwrite) */
			rc = agg_process_partial_stripe(entry);
			if (rc) {
				D_ERROR(DF_UOID" agg_process_partial_stripe fail: "DF_RC"\n",
					DP_UOID(entry->ae_oid), DP_RC(rc));
				goto out;
			}
		}
	}

	/* For the success of above 2 cases - 1) agg_encode_local_parity, and
	 * 2) agg_process_partial_stripe, here the new parity generated. Will
	 * with same two handing - write new parity and remove old replicas.
	 */
	D_ASSERT(rc == 0);
	rc = agg_stripe_set_write_parity(entry);
	if (rc) {
		D_ERROR(DF_UOID" agg_stripe_set_write_parity fail: "DF_RC"\n",
			DP_UOID(entry->ae_oid), DP_RC(rc));
		goto out;
	}

	agg_stripe_set_remove(entry, false);

peer_update:
	rc = agg_peer_update(entry);
	if (rc)
		D_ERROR(DF_UOID" agg_peer_update fail: "DF_RC"\n",
			DP_UOID(entry->ae_oid), DP_RC(rc));

out:
	agg_clear_extents(entry);
	return rc;
}

/* Returns the subrange of the RECX iterator's returned recx that lies within
 * the current stripe.
 */
static daos_off_t
agg_in_stripe(struct ec_agg_entry *entry, daos_recx_t *recx)
{
	unsigned int		len = ec_age2cs(entry);
	unsigned int		k = ec_age2k(entry);
	daos_off_t		stripe = recx->rx_idx / (len * k);
	daos_off_t		stripe_end = (stripe + 1) * len * k;

	if (recx->rx_idx + recx->rx_nr > stripe_end)
		return stripe_end - recx->rx_idx;
	else
		return recx->rx_nr;
}

static int
agg_extent_add(struct ec_agg_entry *agg_entry, vos_iter_entry_t *entry,
	       daos_recx_t *recx)
{
	struct ec_agg_extent	*extent = NULL;
	int			rc = 0;

	/* Add the extent to the entry, for the current stripe */
	D_ALLOC_PTR(extent);
	if (extent == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	extent->ae_recx = *recx;
	extent->ae_epoch = entry->ie_epoch;
	agg_entry->ae_rsize = entry->ie_rsize;

	d_list_add_tail(&extent->ae_link,
			&agg_entry->ae_cur_stripe.as_dextents);

	if (!agg_entry->ae_cur_stripe.as_extent_cnt) {
		/* first extent in stripe: save the start offset */
		agg_entry->ae_cur_stripe.as_offset =  extent->ae_recx.rx_idx -
			rounddown(extent->ae_recx.rx_idx, ec_age2ss(agg_entry));
		agg_entry->ae_cur_stripe.as_stripenum =
				agg_stripenum(agg_entry, recx->rx_idx);
	}

	agg_entry->ae_cur_stripe.as_extent_cnt++;
	if (BIO_ADDR_IS_HOLE(&entry->ie_biov.bi_addr)) {
		extent->ae_hole = true;
		agg_entry->ae_cur_stripe.as_has_holes = true;
	} else {
		agg_entry->ae_cur_stripe.as_stripe_fill +=
			agg_in_stripe(agg_entry, recx);
	}

	if (extent->ae_epoch > agg_entry->ae_cur_stripe.as_hi_epoch)
		agg_entry->ae_cur_stripe.as_hi_epoch = extent->ae_epoch;

	D_DEBUG(DB_TRACE, "adding extent "DF_RECX", to stripe %lu, shard: %u\n",
		DP_RECX(extent->ae_recx),
		agg_stripenum(agg_entry, extent->ae_recx.rx_idx),
		agg_entry->ae_oid.id_shard);
out:
	return rc;
}

/* Iterator call back sub-function for handling data extents. */
static int
agg_data_extent(struct ec_agg_param *agg_param, vos_iter_entry_t *entry,
		struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	daos_off_t	offset = entry->ie_recx.rx_idx;
	daos_off_t	end = DAOS_RECX_END(entry->ie_recx);
	int		rc = 0;

	D_ASSERT(!(entry->ie_recx.rx_idx & PARITY_INDICATOR));

	D_DEBUG(DB_IO, DF_UOID" get recx "DF_RECX", %u\n",
		DP_UOID(agg_entry->ae_oid), DP_RECX(entry->ie_recx),
		entry->ie_minor_epc);

	while (offset < end) {
		daos_off_t this_stripenum;
		daos_off_t this_end_stripenum;
		daos_recx_t new_recx;
		daos_off_t  min_end = min(roundup(offset + 1,
						  ec_age2ss(agg_entry)), end);

		new_recx.rx_idx = offset;
		new_recx.rx_nr = min_end - offset;

		this_stripenum = agg_stripenum(agg_entry, new_recx.rx_idx);
		this_end_stripenum = agg_stripenum(agg_entry,
						   DAOS_RECX_END(new_recx));

		/* If it can be added to the current stripe */
		if (agg_entry->ae_cur_stripe.as_extent_cnt == 0 ||
		    this_stripenum == agg_entry->ae_cur_stripe.as_stripenum) {
			rc = agg_extent_add(agg_entry, entry, &new_recx);
			if (rc)
				D_GOTO(out, rc);
			offset += new_recx.rx_nr;
		}

		/* If it reaches to the end of the stripe, let's process it */
		if ((this_stripenum > agg_entry->ae_cur_stripe.as_stripenum ||
		     this_end_stripenum >
		     agg_entry->ae_cur_stripe.as_stripenum) &&
		     agg_entry->ae_cur_stripe.as_extent_cnt > 0) {
			rc = agg_process_stripe(agg_param, agg_entry);
			if (rc) {
				D_ERROR(DF_UOID" stripe "DF_U64":"DF_RC"\n",
					DP_UOID(agg_entry->ae_oid),
					agg_entry->ae_cur_stripe.as_stripenum,
					DP_RC(rc));
				D_GOTO(out, rc);
			}
			D_ASSERT(agg_entry->ae_cur_stripe.as_extent_cnt == 0);
		}
	}
out:
	return rc;
}

/* Post iteration call back for akey.  */
static int
agg_akey_post(daos_handle_t ih, struct ec_agg_param *agg_param,
	      vos_iter_entry_t *entry, struct ec_agg_entry *agg_entry,
	      unsigned int *acts)
{
	int	   rc = 0;

	if (agg_entry->ae_cur_stripe.as_extent_cnt) {
		rc = agg_process_stripe(agg_param, agg_entry);
		if (rc) {
			D_ERROR("Process stripe returned "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		agg_entry->ae_cur_stripe.as_stripenum	= 0UL;
		agg_entry->ae_cur_stripe.as_hi_epoch	= 0UL;
		agg_entry->ae_cur_stripe.as_stripe_fill = 0UL;
		agg_entry->ae_cur_stripe.as_offset	= 0U;
	}

	return rc;
}

/* Compare function for keys.  Used to reset iterator position.  */
static inline int
agg_key_compare(daos_key_t key1, daos_key_t key2)
{
	if (key1.iov_len != key2.iov_len)
		return 1;

	return memcmp(key1.iov_buf, key2.iov_buf, key1.iov_len);
}

static inline void
agg_reset_pos(vos_iter_type_t type, struct ec_agg_entry *agg_entry)
{
	switch (type) {
	case VOS_ITER_OBJ:
		memset(&agg_entry->ae_oid, 0, sizeof(agg_entry->ae_oid));
		break;
	case VOS_ITER_DKEY:
		memset(&agg_entry->ae_dkey, 0, sizeof(agg_entry->ae_dkey));
		break;
	case VOS_ITER_AKEY:
		memset(&agg_entry->ae_akey, 0, sizeof(agg_entry->ae_akey));
		break;
	default:
		break;
	}
}

static int
agg_shard_is_leader(struct ds_pool *pool, struct ec_agg_entry *agg_entry)
{
	struct pl_obj_shard	*shard;
	struct daos_oclass_attr *oca;
	uint32_t		grp_idx;
	uint32_t		grp_start;
	uint32_t		ec_tgt_idx;
	int			shard_idx;
	int			rc;

	oca = &agg_entry->ae_oca;
	grp_idx = agg_entry->ae_oid.id_shard / daos_oclass_grp_size(oca);
	grp_start = grp_idx * daos_oclass_grp_size(oca);
	ec_tgt_idx = obj_ec_shard_idx(agg_entry->ae_dkey_hash, oca,
				      daos_oclass_grp_size(oca) - 1);

	/**
	 * FIXME: only the last parity shard can be the EC agg leader. What about
	 * Degraded mode?
	 */
	if (agg_entry->ae_oid.id_shard != ec_tgt_idx + grp_start)
		return 0;

	/* If last parity unavailable, then skip the object via returning -DER_STALE. */
	shard_idx = grp_idx * agg_entry->ae_obj_layout->ol_grp_size + ec_tgt_idx;
	shard = pl_obj_get_shard(agg_entry->ae_obj_layout, shard_idx);
	if (shard->po_target != -1 && shard->po_shard != -1 && !shard->po_rebuilding)
		rc = (agg_entry->ae_oid.id_shard == shard->po_shard) ? 1 : 0;
	else
		rc = -DER_STALE;

	return rc;
}

/* Initializes the struct holding the iteration state (ec_agg_entry). */
static void
agg_reset_dkey_entry(struct ec_agg_entry *agg_entry, vos_iter_entry_t *entry)
{
	agg_reset_pos(VOS_ITER_AKEY, agg_entry);

	agg_entry->ae_cur_stripe.as_stripenum	= 0UL;
	agg_entry->ae_cur_stripe.as_hi_epoch	= 0UL;
	agg_entry->ae_cur_stripe.as_stripe_fill = 0UL;
	agg_entry->ae_cur_stripe.as_extent_cnt	= 0U;
	agg_entry->ae_cur_stripe.as_offset	= 0U;
}

/* Handles dkeys returned by the per-object nested iterator. */
static int
agg_dkey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_param *agg_param, struct ec_agg_entry *agg_entry,
	 unsigned int *acts)
{
	uint64_t	dkey_hash;
	int		rc;

	if (!agg_key_compare(agg_entry->ae_dkey, entry->ie_key)) {
		D_DEBUG(DB_EPC, "Skip dkey: "DF_KEY" ec agg on re-probe\n",
			DP_KEY(&entry->ie_key));
		*acts |= VOS_ITER_CB_SKIP;
		return 0;
	}
	agg_entry->ae_rotate_parity = 1;
	agg_entry->ae_dkey = entry->ie_key;
	dkey_hash = obj_dkey2hash(agg_entry->ae_oid.id_pub, &agg_entry->ae_dkey);
	if (agg_entry->ae_rotate_parity)
		agg_entry->ae_dkey_hash = dkey_hash;
	else
		agg_entry->ae_dkey_hash = 0;

	agg_reset_pos(VOS_ITER_AKEY, agg_entry);
	rc = agg_shard_is_leader(agg_param->ap_pool_info.api_pool, agg_entry);
	if (rc == 1) {
		D_DEBUG(DB_EPC, "oid:"DF_UOID":"DF_KEY" ec agg starting\n",
			DP_UOID(agg_entry->ae_oid), DP_KEY(&agg_entry->ae_dkey));
		agg_reset_dkey_entry(&agg_param->ap_agg_entry, entry);
		rc = 0;
	} else {
		if (rc < 0) {
			D_ERROR("oid:"DF_UOID" ds_pool_check_leader failed "
				DF_RC"\n", DP_UOID(entry->ie_oid), DP_RC(rc));
			if (rc == -DER_STALE)
				agg_param->ap_obj_skipped = 1;
			rc = 0;
		}
		*acts |= VOS_ITER_CB_SKIP;
	}

	return rc;
}

/* Handles akeys returned by the iterator. */
static int
agg_akey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	if (entry->ie_child_type == VOS_ITER_SINGLE) {
		*acts |= VOS_ITER_CB_SKIP;
		return 0;
	}
	agg_entry->ae_akey = entry->ie_key;
	agg_entry->ae_thdl = ih;

	return 0;
}

/* Invokes the yield function pointer. */
static inline bool
ec_aggregate_yield(struct ec_agg_param *agg_param)
{
	int	rc;

	D_ASSERT(agg_param->ap_yield_func != NULL);
	rc = agg_param->ap_yield_func(agg_param->ap_yield_arg);
	if (rc < 0) /* Abort */
		return true;

	/*
	 * FIXME: Implement fine credits for various operations and adjust
	 *	  the credits according to the 'rc'.
	 */
	return false;
}

/* Post iteration call back for outer iterator */
static int
agg_iterate_post_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		    vos_iter_type_t type, vos_iter_param_t *param,
		    void *cb_arg, unsigned int *acts)
{
	struct ec_agg_param	*agg_param = (struct ec_agg_param *)cb_arg;
	struct ec_agg_entry	*agg_entry = &agg_param->ap_agg_entry;
	int			 rc = 0;

	switch (type) {
	case VOS_ITER_OBJ:
		if (agg_entry->ae_obj_layout) {
			pl_obj_layout_free(agg_entry->ae_obj_layout);
			agg_entry->ae_obj_layout = NULL;
		}
		break;
	case VOS_ITER_DKEY:
		agg_reset_pos(VOS_ITER_DKEY, agg_entry);
		break;
	case VOS_ITER_AKEY:
		rc = agg_akey_post(ih, agg_param, entry, agg_entry, acts);
		break;
	case VOS_ITER_RECX:
		break;
	default:
		break;
	}

	return rc;
}

static void
agg_reset_entry(struct ec_agg_entry *agg_entry, vos_iter_entry_t *entry,
		struct daos_oclass_attr *oca)
{
	agg_entry->ae_rsize	= 0UL;
	if (entry) {
		agg_entry->ae_oid	= entry->ie_oid;
		agg_entry->ae_codec	= obj_id2ec_codec(entry->ie_oid.id_pub);
		D_ASSERT(agg_entry->ae_codec);
	} else {
		agg_entry->ae_codec = NULL;
	}
	if (oca)
		agg_entry->ae_oca	= *oca;

	if (daos_handle_is_valid(agg_entry->ae_obj_hdl)) {
		dsc_obj_close(agg_entry->ae_obj_hdl);
		agg_entry->ae_obj_hdl = DAOS_HDL_INVAL;
	}

	agg_reset_pos(VOS_ITER_DKEY, agg_entry);
	agg_reset_dkey_entry(agg_entry, entry);
}

static int
agg_filter(daos_handle_t ih, vos_iter_desc_t *desc, void *cb_arg, unsigned int *acts)
{
	struct ec_agg_param	*agg_param = (struct ec_agg_param *)cb_arg;
	struct ec_agg_pool_info	*info = &agg_param->ap_pool_info;
	struct daos_oclass_attr  oca;
	int			 rc = 0;

	if (desc->id_type != VOS_ITER_OBJ)
		goto check;

	rc = dsc_obj_id2oc_attr(desc->id_oid.id_pub, &info->api_props, &oca);
	if (rc) {
		D_ERROR("Skip object("DF_OID") with unknown class(%u)\n",
			DP_OID(desc->id_oid.id_pub),
			daos_obj_id2class(desc->id_oid.id_pub));
		*acts = VOS_ITER_CB_SKIP;
		agg_param->ap_credits++;
		goto done;
	}

	if (!daos_oclass_is_ec(&oca)) { /* Skip non-EC object */
		D_DEBUG(DB_EPC, "Skip oid:"DF_UOID" non-ec obj\n",
			DP_UOID(desc->id_oid));
		agg_param->ap_credits++;
		*acts = VOS_ITER_CB_SKIP;
		goto done;
	}

check:
	if (desc->id_agg_write <= agg_param->ap_filter_eph) {
		if (desc->id_type == VOS_ITER_OBJ)
			D_DEBUG(DB_EPC, "Skip oid:"DF_UOID" agg_epoch="DF_X64" filter="DF_X64"\n",
				DP_UOID(desc->id_oid), desc->id_agg_write,
				agg_param->ap_filter_eph);
		else
			D_DEBUG(DB_EPC, "Skip key:"DF_KEY" agg_epoch="DF_X64" filter="DF_X64"\n",
				DP_KEY(&desc->id_key), desc->id_agg_write,
				agg_param->ap_filter_eph);
		agg_param->ap_credits++;
		*acts = VOS_ITER_CB_SKIP;
		goto done;
	}
done:
	if (agg_param->ap_credits > agg_param->ap_credits_max) {
		agg_param->ap_credits = 0;
		D_DEBUG(DB_EPC, "EC aggregation yield type %d. acts %u\n",
			desc->id_type, *acts);
		if (ec_aggregate_yield(agg_param)) {
			D_DEBUG(DB_EPC, "EC aggregation quit\n");
			*acts |= VOS_ITER_CB_EXIT;
		}
	}

	return rc;
}

/* Iterator pre-callback for objects. Determines if object is subject
 * to aggregation. Skips objects that are not EC, or are not led by
 * this target.
 */
static int
ec_agg_object(daos_handle_t ih, vos_iter_entry_t *entry, struct ec_agg_param *agg_param,
	      unsigned int *acts)
{
	struct ec_agg_pool_info *info = &agg_param->ap_pool_info;
	struct ec_agg_entry	*agg_entry = &agg_param->ap_agg_entry;
	struct daos_obj_md	 md = { 0 };
	struct pl_map		*map;
	struct daos_oclass_attr  oca;
	struct cont_props	 props;
	int			 rc = 0;

	/** We should have filtered it if it isn't EC */
	rc = dsc_obj_id2oc_attr(entry->ie_oid.id_pub, &info->api_props, &oca);
	D_ASSERT(rc == 0 && daos_oclass_is_ec(&oca));
	agg_reset_entry(&agg_param->ap_agg_entry, entry, &oca);
	map = pl_map_find(agg_param->ap_pool_info.api_pool_uuid, entry->ie_oid.id_pub);
	if (map == NULL) {
		D_ERROR("Failed to find pool map to check leader for "DF_UOID"\n",
			DP_UOID(entry->ie_oid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	props = dc_cont_hdl2props(info->api_cont_hdl);
	md.omd_id = entry->ie_oid.id_pub;
	md.omd_ver = agg_param->ap_pool_info.api_pool->sp_map_version;
	md.omd_fdom_lvl = props.dcp_redun_lvl;
	rc = pl_obj_place(map, &md, DAOS_OO_RO, NULL, &agg_entry->ae_obj_layout);

out:
	return rc;
}

/* Call-back function for full VOS iteration outer iterator.
*/
static int
agg_iterate_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		   vos_iter_type_t type, vos_iter_param_t *param,
		   void *cb_arg, unsigned int *acts)
{
	struct ec_agg_param	*agg_param = (struct ec_agg_param *)cb_arg;
	struct ec_agg_entry	*agg_entry = &agg_param->ap_agg_entry;
	int			 rc = 0;

	D_ASSERT(agg_param->ap_initialized);

	switch (type) {
	case VOS_ITER_OBJ:
		agg_param->ap_epr = param->ip_epr;
		rc = ec_agg_object(ih, entry, agg_param, acts);
		break;
	case VOS_ITER_DKEY:
		rc = agg_dkey(ih, entry, agg_param, agg_entry, acts);
		break;
	case VOS_ITER_AKEY:
		rc = agg_akey(ih, entry, agg_entry, acts);
		break;
	case VOS_ITER_RECX:
		rc = agg_data_extent(agg_param, entry, agg_entry, acts);
		break;
	default:
		/* Verify that single values are always skipped */
		D_ASSERT(0);
		break;
	}

	if (rc < 0) {
		D_ERROR("EC aggregation failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	agg_param->ap_credits += 20;
	if (agg_param->ap_credits > agg_param->ap_credits_max) {
		agg_param->ap_credits = 0;
		D_DEBUG(DB_EPC, "EC aggregation yield type %d. acts %u\n",
			type, *acts);
		if (ec_aggregate_yield(agg_param)) {
			D_DEBUG(DB_EPC, "EC aggregation quit\n");
			*acts |= VOS_ITER_CB_EXIT;
		}
	}

	return rc;
}

struct ec_agg_ult_arg {
	struct ec_agg_param	*param;
	daos_epoch_t		*ec_query_p;
	uint32_t		tgt_idx;
};

/* Captures the IV values need for pool and container open. Runs in
 * system xstream.
 */
static	int
ec_agg_init_ult(void *arg)
{
	struct ec_agg_ult_arg	*ult_arg = arg;
	struct ec_agg_param	*agg_param = ult_arg->param;
	struct ds_pool		*pool = agg_param->ap_pool_info.api_pool;
	struct daos_prop_entry	*entry = NULL;
	daos_prop_t		*prop = NULL;
	int			 rc;

	rc = ds_cont_ec_eph_insert(agg_param->ap_pool_info.api_pool,
				   agg_param->ap_pool_info.api_cont_uuid,
				   ult_arg->tgt_idx, &ult_arg->ec_query_p);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_pool_iv_srv_hdl_fetch(pool, &agg_param->ap_pool_info.api_poh_uuid,
				      &agg_param->ap_pool_info.api_coh_uuid);
	if (rc)
		D_GOTO(out, rc);

	prop = daos_prop_alloc(0);
	if (prop == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_pool_iv_prop_fetch(pool, prop);
	if (rc) {
		D_ERROR("ds_pool_iv_prop_fetch failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_LIST);
	D_ASSERT(entry != NULL);
	rc = d_rank_list_dup(&agg_param->ap_pool_info.api_svc_list,
			     (d_rank_list_t *)entry->dpe_val_ptr);
	if (rc)
		D_GOTO(out, rc);

out:
	if (prop)
		daos_prop_free(prop);

	D_DEBUG(DB_EPC, DF_UUID" get iv for agg: %d\n",
		DP_UUID(agg_param->ap_pool_info.api_cont_uuid), rc);

	return rc;
}

static	int
ec_agg_fini_ult(void *arg)
{
	struct ec_agg_ult_arg	*ult_arg = arg;
	struct ec_agg_param	*agg_param = ult_arg->param;
	int			 rc;

	rc = ds_cont_ec_eph_delete(agg_param->ap_pool_info.api_pool,
				   agg_param->ap_pool_info.api_cont_uuid,
				   ult_arg->tgt_idx);
	D_ASSERT(rc == 0);
	return 0;
}

static void
ec_agg_param_fini(struct ds_cont_child *cont, struct ec_agg_param *agg_param)
{
	struct ec_agg_entry	*agg_entry = &agg_param->ap_agg_entry;
	struct ec_agg_ult_arg	arg;

	arg.param = agg_param;
	arg.tgt_idx = dss_get_module_info()->dmi_tgt_id;
	if (cont->sc_ec_query_agg_eph) {
		dss_ult_execute(ec_agg_fini_ult, &arg, NULL, NULL, DSS_XS_SYS, 0, 0);
		cont->sc_ec_query_agg_eph = NULL;
	}

	if (daos_handle_is_valid(agg_param->ap_pool_info.api_cont_hdl))
		dsc_cont_close(agg_param->ap_pool_info.api_pool_hdl,
			       agg_param->ap_pool_info.api_cont_hdl);

	D_ASSERT(agg_entry->ae_sgl.sg_nr == AGG_IOV_CNT || agg_entry->ae_sgl.sg_nr == 0);
	d_sgl_fini(&agg_entry->ae_sgl, true);
	if (daos_handle_is_valid(agg_param->ap_pool_info.api_pool_hdl))
		dsc_pool_close(agg_param->ap_pool_info.api_pool_hdl);

	if (agg_param->ap_pool_info.api_svc_list)
		d_rank_list_free(agg_param->ap_pool_info.api_svc_list);

	memset(agg_param, 0, sizeof(*agg_param));
}

static int
ec_agg_param_init(struct ds_cont_child *cont, struct agg_param *param)
{
	struct ec_agg_param	*agg_param = param->ap_data;
	struct ec_agg_pool_info *info = &agg_param->ap_pool_info;
	struct ec_agg_ult_arg	arg;
	int			rc;

	D_ASSERT(agg_param->ap_initialized == 0);
	uuid_copy(info->api_pool_uuid, cont->sc_pool->spc_uuid);
	uuid_copy(info->api_cont_uuid, cont->sc_uuid);
	info->api_pool = cont->sc_pool->spc_pool;

	agg_param->ap_cont_handle	= cont->sc_hdl;
	agg_param->ap_yield_func	= agg_rate_ctl;
	agg_param->ap_yield_arg		= param;
	agg_param->ap_credits_max	= EC_AGG_ITERATION_MAX;
	D_INIT_LIST_HEAD(&agg_param->ap_agg_entry.ae_cur_stripe.as_dextents);

	arg.param = agg_param;
	arg.tgt_idx = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_execute(ec_agg_init_ult, &arg, NULL, NULL, DSS_XS_SYS, 0, 0);
	if (rc != 0)
		D_GOTO(out, rc);
	cont->sc_ec_query_agg_eph = arg.ec_query_p;

	rc = dsc_pool_open(info->api_pool_uuid,
			   info->api_poh_uuid, DAOS_PC_RW,
			   NULL, info->api_pool->sp_map,
			   info->api_svc_list, &info->api_pool_hdl);
	if (rc) {
		D_ERROR("dsc_pool_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dsc_cont_open(info->api_pool_hdl, info->api_cont_uuid,
			   info->api_coh_uuid, DAOS_COO_RW,
			   &info->api_cont_hdl);
	if (rc) {
		D_ERROR("dsc_cont_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dsc_cont_get_props(info->api_cont_hdl, &info->api_props);
	D_ASSERT(rc == 0);
out:
	if (rc) {
		D_DEBUG(DB_EPC, "aggregate param init failed: %d\n", rc);
		ec_agg_param_fini(cont, agg_param);
	} else {
		agg_param->ap_initialized = 1;
	}

	return rc;
}

/* Iterates entire VOS. Invokes nested iterator to recurse through trees
 * for all objects meeting the criteria: object is EC, and this target is
 * leader.
 */
static int
cont_ec_aggregate_cb(struct ds_cont_child *cont, daos_epoch_range_t *epr,
		     uint32_t flags, struct agg_param *agg_param)
{
	struct ec_agg_param	 *ec_agg_param = agg_param->ap_data;
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_iter_anchors  anchors = { 0 };
	int			 rc = 0;

	/*
	 * Avoid calling into vos_aggregate() when aborting aggregation
	 * on ds_cont_child purging.
	 */
	D_ASSERT(cont->sc_ec_agg_req != NULL);
	if (dss_ult_exiting(cont->sc_ec_agg_req))
		return 1;

	if (!ec_agg_param->ap_initialized) {
		rc = ec_agg_param_init(cont, agg_param);
		if (rc)
			return rc;
	}

	if (flags & VOS_AGG_FL_FORCE_SCAN) {
		/** We don't want to use the latest container aggregation epoch for the filter
		 *  in this case.   We instead use the lower bound of the epoch range.
		 */
		ec_agg_param->ap_filter_eph = epr->epr_lo;
	} else {
		ec_agg_param->ap_filter_eph = MAX(epr->epr_lo, cont->sc_ec_agg_eph);
	}

	if (ec_agg_param->ap_filter_eph != 0 &&
	    ec_agg_param->ap_filter_eph >= cont->sc_ec_update_timestamp) {
		D_DEBUG(DB_EPC, DF_CONT" skip EC agg "DF_U64">= "DF_U64"\n",
			DP_CONT(cont->sc_pool_uuid, cont->sc_uuid), ec_agg_param->ap_filter_eph,
			cont->sc_ec_update_timestamp);
		goto update_hae;
	}

	iter_param.ip_hdl		= cont->sc_hdl;
	iter_param.ip_epr.epr_lo	= epr->epr_lo;
	iter_param.ip_epr.epr_hi	= epr->epr_hi;
	iter_param.ip_epc_expr		= VOS_IT_EPC_RR;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx	= 0ULL;
	iter_param.ip_recx.rx_nr	= ~PARITY_INDICATOR;
	iter_param.ip_filter_cb		= agg_filter;
	iter_param.ip_filter_arg	= ec_agg_param;

	agg_reset_entry(&ec_agg_param->ap_agg_entry, NULL, NULL);

	ec_agg_param->ap_obj_skipped = 0;
	rc = vos_iterate(&iter_param, VOS_ITER_OBJ, true, &anchors,
			 agg_iterate_pre_cb, agg_iterate_post_cb, ec_agg_param, NULL);

	/* Post_cb may not being executed in some cases */
	agg_clear_extents(&ec_agg_param->ap_agg_entry);

	if (daos_handle_is_valid(ec_agg_param->ap_agg_entry.ae_obj_hdl)) {
		dsc_obj_close(ec_agg_param->ap_agg_entry.ae_obj_hdl);
		ec_agg_param->ap_agg_entry.ae_obj_hdl = DAOS_HDL_INVAL;
	}

	if (ec_agg_param->ap_obj_skipped && !cont->sc_stopping) {
		D_DEBUG(DB_EPC, "with skipped obj during aggregation.\n");
		/* There is rebuild going on, and we can't proceed EC aggregate boundary,
		 * Let's wait for 5 seconds for another EC aggregation.
		 */
		D_ASSERT(cont->sc_ec_agg_req != NULL);
		sched_req_sleep(cont->sc_ec_agg_req, 5 * 1000);
	}

update_hae:
	if (rc == 0 && ec_agg_param->ap_obj_skipped == 0) {
		cont->sc_ec_agg_eph = max(cont->sc_ec_agg_eph, epr->epr_hi);
		if (!cont->sc_stopping && cont->sc_ec_query_agg_eph)
			*cont->sc_ec_query_agg_eph = cont->sc_ec_agg_eph;
	}

	return rc;
}

void
ds_obj_ec_aggregate(void *arg)
{
	struct ds_cont_child	*cont = arg;
	struct ec_agg_param	agg_param = { 0 };
	struct agg_param	param = { 0 };
	int			rc;

	D_DEBUG(DB_EPC, "start EC aggregation "DF_UUID"\n",
		DP_UUID(cont->sc_uuid));
	param.ap_data = &agg_param;
	param.ap_cont = cont;
	rc = ec_agg_param_init(cont, &param);
	if (rc) {
		/* To make sure the EC aggregation can be run on this xstream,
		 * let's do not exit here, and in cont_ec_aggregate_cb(), it will
		 * keep retrying parameter init.
		 */
		D_CDEBUG(rc == -DER_NOTLEADER, DB_EPC, DLOG_ERR,
			 DF_UUID" EC aggregation failed: "DF_RC"\n",
			 DP_UUID(cont->sc_uuid), DP_RC(rc));
	}

	cont_aggregate_interval(cont, cont_ec_aggregate_cb, &param);

	ec_agg_param_fini(cont, &agg_param);
}
