/**
 * (C) Copyright 2020-2021 Intel Corporation.
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
#include "obj_internal.h"

#define EC_AGG_ITERATION_MAX	256

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
	d_list_t	as_hoextents;   /* list of hold-over extents         */
	daos_off_t	as_stripe_fill; /* amount of stripe covered by data  */
	unsigned int	as_extent_cnt;  /* number of replica extents         */
	unsigned int	as_offset;      /* start offset in stripe            */
	bool		as_has_holes;   /* stripe includes holes             */
};

/* Aggregation state for an object.
 */
struct ec_agg_entry {
	daos_unit_oid_t		 ae_oid;	 /* OID of iteration entry    */
	struct daos_oclass_attr	 ae_oca;	 /* Object class of object    */
	struct obj_ec_codec	*ae_codec;	 /* Encode/decode for oclass  */
	d_sg_list_t		 ae_sgl;	 /* Mem for entry processing  */
	daos_handle_t		 ae_thdl;	 /* Iterator handle           */
	daos_key_t		 ae_dkey;	 /* Current dkey              */
	daos_key_t		 ae_akey;	 /* Current akey              */
	daos_size_t		 ae_rsize;	 /* Record size of cur array  */
	struct ec_agg_stripe	 ae_cur_stripe;  /* Struct for current stripe */
	struct ec_agg_par_extent ae_par_extent;	 /* Parity extent             */
	daos_handle_t		 ae_obj_hdl;	 /* Object handle for cur obj */
	struct daos_shard_loc	 ae_peer_pshards[OBJ_EC_MAX_P];
};

/* Parameters used to drive iterate all.
 */
struct ec_agg_param {
	struct ec_agg_pool_info	 ap_pool_info;	 /* pool/cont info            */
	struct ec_agg_entry	 ap_agg_entry;	 /* entry used for each OID   */
	daos_epoch_range_t	 ap_epr;	 /* hi/lo extent threshold    */
	struct dtx_handle	*ap_dth;	 /* handle for DTX refresh    */
	daos_handle_t		 ap_cont_handle; /* VOS container handle */
	bool			(*ap_yield_func)(void *arg); /* yield function*/
	void			*ap_yield_arg;   /* yield argument            */
	uint32_t		 ap_credits_max; /* # of tight loops to yield */
	uint32_t		 ap_credits;     /* # of tight loops          */
	uint32_t		 ap_initialized:1; /* initialized flag */
};

/* Struct used to drive offloaded stripe update.
 */
struct ec_agg_stripe_ud {
	struct ec_agg_entry	*asu_agg_entry; /* Associated entry      */
	uint8_t			*asu_bit_map;   /* Bitmap of cells       */
	daos_recx_t		*asu_recxs;     /* For re-replicate      */
	unsigned int		 asu_cell_cnt;  /* Count of cells        */
	bool			 asu_recalc;    /* Should recalc parity  */
	bool			 asu_write_par; /* Should write parity   */
	struct daos_csummer	*asu_csummer;
	daos_iod_t		 asu_iod;
	d_iov_t			 asu_csum_iov;
	struct dcs_iod_csums	*asu_iod_csums; /* iod csums */
	ABT_eventual		 asu_eventual;  /* Eventual for offload  */
};

/* Represents an replicated data extent.
 */
struct ec_agg_extent {
	d_list_t	ae_link;        /* for extents list   */
	daos_recx_t	ae_recx;        /* idx, nr for extent */
	daos_epoch_t	ae_epoch;       /* epoch for extent   */
	bool		ae_hole;        /* extent is a hole   */
};

static inline struct daos_csummer *
ec_agg_param2csummer(struct ec_agg_param *agg_param)
{
	return dsc_cont2csummer(agg_param->ap_pool_info.api_cont_hdl);
}

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
	uint32_t	k, p, shard;

	k = ec_age2k(entry);
	p = ec_age2p(entry);
	shard = ec_age2shard(entry) % (k + p);
	D_ASSERT(shard >= k && shard < (k + p));

	return (shard - k) % p;
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

	d_list_for_each_entry_safe(extent, ext_tmp,
				   &entry->ae_cur_stripe.as_dextents,
				   ae_link) {
		/* Check for carry-over extent. */
		tail = agg_carry_over(entry, extent);
		D_ASSERT(tail == 0);
		if (extent->ae_hole && tail)
			carry_is_hole = true;

		d_list_del(&extent->ae_link);
		entry->ae_cur_stripe.as_extent_cnt--;
		D_FREE_PTR(extent);
	}

	entry->ae_cur_stripe.as_offset = 0U;
	D_ASSERT(entry->ae_cur_stripe.as_extent_cnt == 0);
	entry->ae_cur_stripe.as_hi_epoch = 0UL;
	entry->ae_cur_stripe.as_stripe_fill = 0;
	entry->ae_cur_stripe.as_has_holes = carry_is_hole ? true : false;
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
agg_overlap(unsigned int estart, unsigned int elen, unsigned int cell,
	    unsigned int k, unsigned int len, daos_off_t stripenum)
{
	daos_off_t cell_start = k * len * stripenum + len * cell;

	estart += k * len * stripenum;
	if (cell_start <= estart && estart < cell_start + len)
		return true;
	if (estart <= cell_start && cell_start < estart + elen)
		return true;
	return false;
}

static unsigned int
agg_count_cells(uint8_t *fcbit_map, uint8_t *tbit_map, unsigned int estart,
		unsigned int elen, unsigned int k, unsigned int len,
		unsigned int stripenum, unsigned int *full_cell_cnt)
{
	unsigned int i, cell_cnt = 0;

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
	struct daos_obj_layout	*layout;
	struct ec_agg_param	*agg_param;
	unsigned int		 k = ec_age2k(entry);
	d_rank_t		 myrank;
	int			 i, j, rc = 0;

	if (daos_handle_is_valid(entry->ae_obj_hdl))
		return rc;

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = dsc_obj_open(agg_param->ap_pool_info.api_cont_hdl,
			  entry->ae_oid.id_pub, DAOS_OO_RW,
			  &entry->ae_obj_hdl);
	if (rc)
		goto out;

	crt_group_rank(NULL, &myrank);
	rc = dc_obj_layout_get(entry->ae_obj_hdl, &layout);
	if (rc)
		goto out;

	for (i = 0; i < layout->ol_nr; i++) {
		struct daos_obj_shard *sd = layout->ol_shards[i];
		int p;

		for (j = p = 0; j < sd->os_replica_nr; j++) {
			if (j >= k) {
				entry->ae_peer_pshards[p].sd_rank
					= sd->os_shard_loc[j].sd_rank;
				entry->ae_peer_pshards[p].sd_tgt_idx
					= sd->os_shard_loc[j].sd_tgt_idx;
				p++;
			}
		}
	}
	daos_obj_layout_free(layout);
out:
	/* NB: entry::ae_obj_hdl will be closed externally */
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
	if (recxs)
		D_FREE(recxs);
	if (sgl.sg_iovs)
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
	struct ec_agg_stripe_ud		stripe_ud = { 0 };
	int				*status;
	int				tid, rc = 0;

	stripe_ud.asu_agg_entry = entry;
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = ABT_eventual_create(sizeof(*status), &stripe_ud.asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	rc = dss_ult_create(agg_encode_full_stripe_ult, &stripe_ud,
			    DSS_XS_OFFLOAD, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud.asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		rc = *status;
	else
		rc = 0;

ev_out:
	ABT_eventual_free(&stripe_ud.asu_eventual);
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

/* Writes updated parity to VOS, and removes replicas fully contained
 * in the processed stripe.
 */
static int
agg_update_vos(struct ec_agg_param *agg_param, struct ec_agg_entry *entry,
	       bool write_parity)
{
	daos_recx_t		 recx = { 0 };
	daos_epoch_range_t	 epoch_range = { 0 };
	struct ec_agg_param	*ap;
	struct ec_agg_extent	*ext;
	uint32_t		 len = ec_age2cs(entry);
	uint32_t		 pidx = ec_age2pidx(entry);
	struct daos_csummer	*csummer;
	struct dcs_iod_csums	*iod_csums = NULL;
	int			 rc = 0;

	ap = container_of(entry, struct ec_agg_param, ap_agg_entry);

	if (write_parity) {
		d_sg_list_t	 sgl = { 0 };
		daos_iod_t	 iod = { 0 };
		d_iov_t		 iov;

		csummer = ec_agg_param2csummer(agg_param);
		iov = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY];
		iov.iov_buf += pidx * ec_age2cs_b(entry);
		iov.iov_len = ec_age2cs_b(entry);
		sgl.sg_iovs = &iov;
		sgl.sg_nr = 1;
		iod.iod_nr = 1;
		iod.iod_size = entry->ae_rsize;
		iod.iod_name = entry->ae_akey;
		iod.iod_type = DAOS_IOD_ARRAY;
		iod.iod_recxs = &recx;
		recx.rx_idx = (entry->ae_cur_stripe.as_stripenum * len) |
							PARITY_INDICATOR;
		recx.rx_nr = len;
		if (csummer != NULL) {
			rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL,
						    1, false, NULL, 0,
						    &iod_csums);
			if (rc) {
				D_ERROR("daos_csummer_calc_iods failed: "DF_RC
					"\n", DP_RC(rc));
				goto out;
			}
			D_ASSERT(iod_csums != NULL);
		}
		rc = vos_obj_update(ap->ap_cont_handle, entry->ae_oid,
				    entry->ae_cur_stripe.as_hi_epoch, 0, 0,
				    &entry->ae_dkey, 1, &iod, iod_csums, &sgl);
		if (csummer != NULL && iod_csums != NULL)
			daos_csummer_free_ic(csummer, &iod_csums);
		if (rc) {
			D_ERROR("vos_obj_update failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	d_list_for_each_entry(ext, &entry->ae_cur_stripe.as_dextents, ae_link) {
		int err;

		epoch_range.epr_lo = epoch_range.epr_hi = ext->ae_epoch;
		err = vos_obj_array_remove(ap->ap_cont_handle,
					   entry->ae_oid,
					   &epoch_range,
					   &entry->ae_dkey,
					   &entry->ae_akey,
					   &ext->ae_recx);
		if (err)
			D_ERROR("array_remove fails: "DF_RC"\n", DP_RC(err));
		if (!rc && err)
			rc = err;
	}
out:
	return rc;
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
	if (recxs != NULL)
		D_FREE(recxs);
	if (sgl.sg_iovs != NULL)
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
	uint32_t	 k = ec_age2k(entry);
	uint32_t	 p = ec_age2p(entry);
	uint32_t	 shard  = ec_age2shard(entry);
	uint32_t	 pidx, sidx, peer_shard;
	int		 i, rc = 0;

	/* Only called when p > 1. */
	D_ASSERT(p > 1);
	sidx = shard % (k + p);
	D_ASSERT(sidx >= k && sidx < k + p);
	pidx = sidx - k;

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
	for (i = 0; i < p; i++) {
		if (i == pidx)
			continue;
		d_iov_set(&iov, &buf[i * cell_b], cell_b);
		peer_shard = rounddown(shard, k + p) + k + i;
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
	cell_start = cell_idx * len;
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
	struct ec_agg_stripe_ud	 stripe_ud = { 0 };
	struct ec_agg_extent	*extent;
	int			*status;
	uint8_t			*bit_map = NULL;
	uint8_t			 fcbit_map[OBJ_TGT_BITMAP_LEN] = {0};
	uint8_t			 tbit_map[OBJ_TGT_BITMAP_LEN] = {0};
	unsigned int		 len = ec_age2cs(entry);
	unsigned int		 k = ec_age2k(entry);
	unsigned long            ss;
	unsigned int		 i, full_cell_cnt = 0;
	unsigned int		 cell_cnt = 0;
	unsigned int		 estart, elen = 0;
	unsigned int		 eend = 0;
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
						    elen, k, len, entry->
						    ae_cur_stripe.as_stripenum,
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
		stripe_ud.asu_recalc = true;
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
					     stripe_ud.asu_recalc);
	if (rc)
		goto out;

	if (stripe_ud.asu_recalc) {
		for (i = 0; i < k; i++) {
			if (isset(bit_map, i))
				clrbit(bit_map, i);
			else
				setbit(bit_map, i);
		}
		cell_cnt = k - cell_cnt;
	}

	stripe_ud.asu_agg_entry = entry;
	stripe_ud.asu_bit_map = bit_map;
	stripe_ud.asu_cell_cnt = cell_cnt;

	rc = ABT_eventual_create(sizeof(*status), &stripe_ud.asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_create(agg_process_partial_stripe_ult, &stripe_ud,
			    DSS_XS_IOFW, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud.asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0) {
		rc = *status;
		goto ev_out;
	}

ev_out:
	ABT_eventual_free(&stripe_ud.asu_eventual);

out:
	return rc;
}

/* Sends the generated parity and the stripe number to the peer
 * parity target. Handler writes the parity and deletes the replicas
 * for the stripe.  Has to be extended to support p > 2.
 */
static void
agg_peer_update_ult(void *arg)
{
	struct ec_agg_stripe_ud	*stripe_ud = (struct ec_agg_stripe_ud *)arg;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	unsigned int		 len = ec_age2cs(entry);
	d_iov_t			 iov = { 0 };
	d_sg_list_t		 sgl = { 0 };
	daos_iod_t		 iod = { 0 };
	daos_recx_t		 recx = { 0 };
	crt_endpoint_t		 tgt_ep = { 0 };
	unsigned char		*buf = NULL;
	struct obj_ec_agg_in	*ec_agg_in = NULL;
	struct obj_ec_agg_out	*ec_agg_out;
	struct ec_agg_param	*agg_param;
	crt_bulk_t		 bulk_hdl = NULL;
	struct daos_csummer	*csummer = NULL;
	struct dcs_iod_csums	*iod_csums;
	uint32_t		 shard  = ec_age2shard(entry);
	uint32_t		 pidx = ec_age2pidx(entry);
	uint64_t		 cell_b = ec_age2cs_b(entry);
	uint32_t		 k = ec_age2k(entry);
	uint32_t		 p = ec_age2p(entry);
	uint32_t		 peer, peer_shard;
	crt_rpc_t		*rpc = NULL;
	int			 rc = 0;

	if (unlikely(DAOS_FAIL_CHECK(DAOS_FORCE_EC_AGG_PEER_FAIL)))
		D_GOTO(out, rc = -DER_TIMEDOUT);

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	csummer = ec_agg_param2csummer(agg_param);
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = entry->ae_akey;
	iod.iod_size = entry->ae_rsize;
	for (peer = 0; peer < p; peer++) {
		if (peer == pidx)
			continue;
		tgt_ep.ep_rank = entry->ae_peer_pshards[peer].sd_rank;
		tgt_ep.ep_tag = entry->ae_peer_pshards[peer].sd_tgt_idx;
		rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
				    DAOS_OBJ_RPC_EC_AGGREGATE, &rpc);
		if (rc) {
			D_ERROR(DF_UOID" pidx %d to peer %d, obj_req_create "
				DF_RC"\n", DP_UOID(entry->ae_oid), pidx, peer,
				DP_RC(rc));
			goto out;
		}
		ec_agg_in = crt_req_get(rpc);
		uuid_copy(ec_agg_in->ea_pool_uuid,
			  agg_param->ap_pool_info.api_pool_uuid);
		uuid_copy(ec_agg_in->ea_coh_uuid,
			  agg_param->ap_pool_info.api_poh_uuid);
		uuid_copy(ec_agg_in->ea_cont_uuid,
			  agg_param->ap_pool_info.api_cont_uuid);
		uuid_copy(ec_agg_in->ea_coh_uuid,
			  agg_param->ap_pool_info.api_coh_uuid);
		ec_agg_in->ea_oid = entry->ae_oid;
		peer_shard = rounddown(shard, k + p) + k + peer;
		ec_agg_in->ea_oid.id_shard = peer_shard;
		ec_agg_in->ea_dkey = entry->ae_dkey;
		ec_agg_in->ea_epoch_range.epr_lo = agg_param->ap_epr.epr_lo;
		ec_agg_in->ea_epoch_range.epr_hi =
			entry->ae_cur_stripe.as_hi_epoch;
		ec_agg_in->ea_stripenum = entry->ae_cur_stripe.as_stripenum;
		ec_agg_in->ea_map_ver =
			agg_param->ap_pool_info.api_pool->sp_map_version;
		ec_agg_in->ea_iod_csums.ca_arrays = NULL;
		ec_agg_in->ea_iod_csums.ca_count = 0;
		iod_csums = NULL;
		iod.iod_nr = 0;
		if (stripe_ud->asu_write_par) {
			recx.rx_idx = (ec_agg_in->ea_stripenum * len) |
				      PARITY_INDICATOR;
			recx.rx_nr = len;
			iod.iod_nr = 1;
			iod.iod_recxs = &recx;
			buf = (unsigned char *)
				entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
			d_iov_set(&iov, &buf[peer * cell_b], cell_b);
			sgl.sg_iovs = &iov;
			sgl.sg_nr = sgl.sg_nr_out = 1;
			rc = crt_bulk_create(dss_get_module_info()->dmi_ctx,
					     &sgl, CRT_BULK_RW, &bulk_hdl);
			if (rc) {
				D_ERROR(DF_UOID" pidx %d to peer %d, "
					"crt_bulk_create "DF_RC"\n",
					DP_UOID(entry->ae_oid), pidx, peer,
					DP_RC(rc));
				goto out_rpc;
			}
			ec_agg_in->ea_bulk = bulk_hdl;

			if (csummer != NULL) {
				rc = daos_csummer_calc_iods(csummer, &sgl, &iod,
							    NULL, 1, false,
							    NULL, 0,
							    &iod_csums);
				if (rc) {
					D_ERROR("daos_csummer_calc_iods "
						"failed: "DF_RC"\n", DP_RC(rc));
					goto out;
				}
				D_ASSERT(iod_csums != NULL);

				ec_agg_in->ea_iod_csums.ca_arrays = iod_csums;
				ec_agg_in->ea_iod_csums.ca_count = 1;
			}
		}
		ec_agg_in->ea_iod = iod;

		rc = dss_rpc_send(rpc);
		if (rc) {
			D_ERROR(DF_UOID" pidx %d to peer %d, dss_rpc_send "
				DF_RC"\n", DP_UOID(entry->ae_oid), pidx, peer,
				DP_RC(rc));
			goto out_bulk;
		}
		ec_agg_out = crt_reply_get(rpc);
		rc = ec_agg_out->ea_status;
		if (bulk_hdl) {
			crt_bulk_free(bulk_hdl);
			bulk_hdl = NULL;
		}
		if (csummer != NULL && iod_csums != NULL)
			daos_csummer_free_ic(csummer, &iod_csums);
		crt_req_decref(rpc);
		rpc = NULL;
		if (rc) {
			D_ERROR(DF_UOID" pidx %d to peer %d, ea_status "
				DF_RC"\n", DP_UOID(entry->ae_oid), pidx, peer,
				DP_RC(rc));
			break;
		}
	}

out_bulk:
	if (bulk_hdl)
		crt_bulk_free(bulk_hdl);
out_rpc:
	if (rpc)
		crt_req_decref(rpc);
out:
	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

/* Invokes helper function to send the generated parity and the stripe number
 * to the peer parity target.
 */
static int
agg_peer_update(struct ec_agg_entry *entry, bool write_parity)
{
	struct ec_agg_stripe_ud	 stripe_ud = { 0 };
	struct ec_agg_param	*agg_param;
	struct pool_target	*targets;
	int			*status;
	struct daos_shard_loc	*peer_loc;
	uint32_t		 failed_tgts_cnt = 0;
	uint32_t		 p = ec_age2p(entry);
	uint32_t		 peer;
	int			 i, tid, rc = 0;

	D_ASSERT(!write_parity ||
		 entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf);

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = pool_map_find_failed_tgts(agg_param->ap_pool_info.api_pool->sp_map,
				       &targets, &failed_tgts_cnt);
	if (rc) {
		D_ERROR(DF_UOID" pool_map_find_failed_tgts failed: "DF_RC"\n",
			DP_UOID(entry->ae_oid), DP_RC(rc));
		goto out;
	}
	if (targets != NULL) {
		for (peer = 0; peer < p; peer++) {
			peer_loc = &entry->ae_peer_pshards[peer];
			for (i = 0; i < failed_tgts_cnt; i++) {
				if (targets[i].ta_comp.co_rank ==
				    peer_loc->sd_rank) {
					D_DEBUG(DB_EPC, DF_UOID" peer parity "
						"tgt failed rank %d, tgt_idx "
						"%d.\n", DP_UOID(entry->ae_oid),
						peer_loc->sd_rank,
						peer_loc->sd_tgt_idx);
					goto out;
				}
			}
		}
	}

	stripe_ud.asu_write_par = write_parity;
	stripe_ud.asu_agg_entry = entry;

	rc = agg_get_obj_handle(entry);
	if (rc) {
		D_ERROR("Failed to open object: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	rc = ABT_eventual_create(sizeof(*status), &stripe_ud.asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_create(agg_peer_update_ult, &stripe_ud,
			    DSS_XS_IOFW, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud.asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		rc = *status;
ev_out:
	ABT_eventual_free(&stripe_ud.asu_eventual);
out:
	return rc;
}

#define EC_CSUM_BUF_SIZE	(256)
static void
agg_process_holes_ult(void *arg)
{
	crt_endpoint_t		 tgt_ep = { 0 };
	struct ec_agg_stripe_ud	*stripe_ud = (struct ec_agg_stripe_ud *)arg;
	daos_iod_t		*iod = &stripe_ud->asu_iod;
	d_iov_t			*csum_iov_fetch = &stripe_ud->asu_csum_iov;
	d_iov_t			 tmp_csum_iov;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	struct ec_agg_extent	*agg_extent;
	struct pool_target	*targets;
	struct ec_agg_param	*agg_param;
	struct obj_ec_rep_in	*ec_rep_in = NULL;
	struct obj_ec_rep_out	*ec_rep_out = NULL;
	crt_rpc_t		*rpc = NULL;
	crt_bulk_t		 bulk_hdl = NULL;
	uint32_t		 len = ec_age2cs(entry);
	uint64_t		 cell_b = ec_age2cs_b(entry);
	uint32_t		 k = ec_age2k(entry);
	uint32_t		 p = ec_age2p(entry);
	uint64_t		 ss = entry->ae_cur_stripe.as_stripenum *
					k * len;
	uint64_t		 last_ext_end = 0;
	uint64_t		 ext_cnt = 0;
	uint64_t		 ext_tot_len = 0;
	uint32_t		 failed_tgts_cnt = 0;
	uint32_t		 pidx = ec_age2pidx(entry);
	uint32_t		 peer;
	int			 i, rc = 0;
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
			stripe_ud->asu_recxs[ext_cnt].rx_idx =
				ss + last_ext_end;
			stripe_ud->asu_recxs[ext_cnt].rx_nr =
				agg_extent->ae_recx.rx_idx - ss -
				last_ext_end;
			ext_tot_len +=
			stripe_ud->asu_recxs[ext_cnt++].rx_nr;
		}
		last_ext_end = agg_extent->ae_recx.rx_idx +
			agg_extent->ae_recx.rx_nr - ss;
		if (last_ext_end >= k * len)
			break;
	}

	if (!valid_hole)
		goto out;

	if (last_ext_end < k * len) {
		stripe_ud->asu_recxs[ext_cnt].rx_idx = ss + last_ext_end;
		stripe_ud->asu_recxs[ext_cnt].rx_nr = k * len - last_ext_end;
		ext_tot_len += stripe_ud->asu_recxs[ext_cnt++].rx_nr;
	}
	stripe_ud->asu_cell_cnt = ext_cnt;
	iod->iod_name = entry->ae_akey;
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_size = entry->ae_rsize;
	iod->iod_nr = ext_cnt;
	iod->iod_recxs = stripe_ud->asu_recxs;
	entry->ae_sgl.sg_nr = 1;
	entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_len = ext_tot_len *
						      entry->ae_rsize;
	D_ASSERT(entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_len <= k * cell_b);
	/* Pull data via dsc_obj_fetch */
	if (ext_cnt) {
		bool	retried = false;

		rc = daos_iov_alloc(csum_iov_fetch, EC_CSUM_BUF_SIZE, false);
		if (rc != 0)
			goto out;

fetch_again:
		rc = dsc_obj_fetch(entry->ae_obj_hdl,
				   entry->ae_cur_stripe.as_hi_epoch,
				   &entry->ae_dkey, 1, iod, &entry->ae_sgl,
				   NULL, DIOF_FOR_EC_AGG, NULL, csum_iov_fetch);
		if (rc) {
			D_ERROR("dsc_obj_fetch failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}

		if (retried)
			D_ASSERT(csum_iov_fetch->iov_len == 0 ||
				 csum_iov_fetch->iov_len <=
				 csum_iov_fetch->iov_buf_len);

		if (csum_iov_fetch->iov_len > csum_iov_fetch->iov_buf_len &&
		    !retried) {
			/** retry dsc_obj_fetch with appropriate csum_iov
			 * buf length
			 */
			void *tmp_ptr;

			D_REALLOC(tmp_ptr, csum_iov_fetch->iov_buf,
				  csum_iov_fetch->iov_buf_len,
				  csum_iov_fetch->iov_len);
			if (tmp_ptr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			csum_iov_fetch->iov_buf_len = csum_iov_fetch->iov_len;
			csum_iov_fetch->iov_len = 0;
			csum_iov_fetch->iov_buf = tmp_ptr;
			retried = true;
			goto fetch_again;
		}

		rc = daos_csummer_csum_init_with_packed(&stripe_ud->asu_csummer,
							csum_iov_fetch);
		if (rc) {
			D_ERROR("daos_csummer_csum_init_with_packed failed: "
				DF_RC"\n", DP_RC(rc));
			goto out;
		}

		tmp_csum_iov = *csum_iov_fetch;
		rc = daos_csummer_alloc_iods_csums_with_packed(
			stripe_ud->asu_csummer, iod, 1, &tmp_csum_iov,
			&stripe_ud->asu_iod_csums);
		if (rc != 0) {
			D_ERROR("setting up iods csums failed: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	}

	if (ec_age2p(entry) < 2)
		goto out;

	if (ext_cnt) {
		rc = crt_bulk_create(dss_get_module_info()->dmi_ctx,
				     &entry->ae_sgl, CRT_BULK_RW, &bulk_hdl);
		if (rc) {
			D_ERROR(DF_UOID" crt_bulk_create returned: "DF_RC"\n",
				DP_UOID(entry->ae_oid), DP_RC(rc));
			goto out;
		}
	}

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	/* Invoke peer re-replicate */
	for (peer = 0; peer < p; peer++) {
		if (pidx == peer)
			continue;
		rc = pool_map_find_failed_tgts(
			agg_param->ap_pool_info.api_pool->sp_map,
			&targets, &failed_tgts_cnt);
		if (rc) {
			D_ERROR(DF_UOID" pool_map_find_failed_tgts failed: "
				DF_RC"\n", DP_UOID(entry->ae_oid), DP_RC(rc));
			goto out;
		}

		if (targets != NULL) {
			for (i = 0; i < failed_tgts_cnt; i++) {
				if (targets[i].ta_comp.co_rank ==
				    entry->ae_peer_pshards[peer].sd_rank) {
					D_ERROR(DF_UOID" peer %d parity tgt "
						"failed\n",
						DP_UOID(entry->ae_oid),
						peer);
					rc = -1;
					goto out;
				}
			}
		}

		tgt_ep.ep_rank = entry->ae_peer_pshards[peer].sd_rank;
		tgt_ep.ep_tag = entry->ae_peer_pshards[peer].sd_tgt_idx;
		rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
				    DAOS_OBJ_RPC_EC_REPLICATE, &rpc);
		if (rc) {
			D_ERROR(DF_UOID" obj_req_create failed: "DF_RC"\n",
				DP_UOID(entry->ae_oid), DP_RC(rc));
			goto out;
		}
		ec_rep_in = crt_req_get(rpc);
		uuid_copy(ec_rep_in->er_pool_uuid,
			  agg_param->ap_pool_info.api_pool_uuid);
		uuid_copy(ec_rep_in->er_coh_uuid,
			  agg_param->ap_pool_info.api_poh_uuid);
		uuid_copy(ec_rep_in->er_cont_uuid,
			  agg_param->ap_pool_info.api_cont_uuid);
		uuid_copy(ec_rep_in->er_coh_uuid,
			  agg_param->ap_pool_info.api_coh_uuid);
		ec_rep_in->er_oid = entry->ae_oid;
		ec_rep_in->er_oid.id_shard--;
		ec_rep_in->er_dkey = entry->ae_dkey;
		ec_rep_in->er_iod = *iod;
		ec_rep_in->er_iod_csums.ca_arrays = stripe_ud->asu_iod_csums;
		ec_rep_in->er_iod_csums.ca_count =
			stripe_ud->asu_iod_csums == NULL ? 0 : 1;
		ec_rep_in->er_stripenum = entry->ae_cur_stripe.as_stripenum;
		ec_rep_in->er_epoch = entry->ae_cur_stripe.as_hi_epoch;
		ec_rep_in->er_map_ver =
			agg_param->ap_pool_info.api_pool->sp_map_version;
		ec_rep_in->er_bulk = bulk_hdl;
		rc = dss_rpc_send(rpc);
		if (rc) {
			D_ERROR(DF_UOID" peer %d dss_rpc_send failed "DF_RC"\n",
				DP_UOID(entry->ae_oid), peer, DP_RC(rc));
			goto out;
		}
		ec_rep_out = crt_reply_get(rpc);
		rc = ec_rep_out->er_status;
		crt_req_decref(rpc);
		rpc = NULL;
		if (rc) {
			D_ERROR(DF_UOID" peer %d er_status failed "DF_RC"\n",
				DP_UOID(entry->ae_oid), peer, DP_RC(rc));
			break;
		}
	}

out:
	if (rpc)
		crt_req_decref(rpc);
	if (bulk_hdl)
		crt_bulk_free(bulk_hdl);
	entry->ae_sgl.sg_nr = AGG_IOV_CNT;
	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

static int
agg_process_holes(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud	 stripe_ud = { 0 };
	daos_iod_t		*iod = &stripe_ud.asu_iod;
	daos_recx_t		 recx = { 0 };
	daos_epoch_range_t	 epoch_range = { 0 };
	struct ec_agg_param	*agg_param;
	int			 tid, rc = 0;
	int			*status;

	D_ALLOC_ARRAY(stripe_ud.asu_recxs,
		      entry->ae_cur_stripe.as_extent_cnt + 1);
	if (stripe_ud.asu_recxs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	stripe_ud.asu_agg_entry = entry;
	rc = agg_get_obj_handle(entry);
	if (rc) {
		D_ERROR("Failed to open object: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	rc = agg_prep_sgl(entry);
	if (rc)
		goto out;
	rc = ABT_eventual_create(sizeof(*status), &stripe_ud.asu_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	tid = dss_get_module_info()->dmi_tgt_id;
	rc = dss_ult_create(agg_process_holes_ult, &stripe_ud,
			    DSS_XS_IOFW, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud.asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		D_GOTO(ev_out, rc = *status);

	/* Update local vos with replicate */
	entry->ae_sgl.sg_nr = 1;
	agg_param = container_of(entry, struct ec_agg_param,
				 ap_agg_entry);
	if (iod->iod_nr) {
		/* write the reps to vos */
		rc = vos_obj_update(agg_param->ap_cont_handle, entry->ae_oid,
				    entry->ae_cur_stripe.as_hi_epoch, 0, 0,
				    &entry->ae_dkey, 1, iod,
				    stripe_ud.asu_iod_csums,
				    &entry->ae_sgl);
		if (rc) {
			D_ERROR("vos_update_begin failed: "DF_RC"\n",
				DP_RC(rc));
			goto ev_out;
		}
		/* Delete parity */
		epoch_range.epr_lo = agg_param->ap_epr.epr_lo;
		epoch_range.epr_hi = entry->ae_cur_stripe.as_hi_epoch;
		recx.rx_nr = ec_age2cs(entry);
		recx.rx_idx = (entry->ae_cur_stripe.as_stripenum * recx.rx_nr) |
			      PARITY_INDICATOR;
		rc = vos_obj_array_remove(agg_param->ap_cont_handle,
					  entry->ae_oid, &epoch_range,
					  &entry->ae_dkey, &entry->ae_akey,
					  &recx);
	}
ev_out:
	entry->ae_sgl.sg_nr = AGG_IOV_CNT;
	ABT_eventual_free(&stripe_ud.asu_eventual);

out:
	D_FREE(stripe_ud.asu_recxs);
	daos_csummer_free_ic(stripe_ud.asu_csummer, &stripe_ud.asu_iod_csums);
	if (stripe_ud.asu_csum_iov.iov_buf)
		D_FREE(stripe_ud.asu_csum_iov.iov_buf);
	daos_csummer_destroy(&stripe_ud.asu_csummer);
	return rc;
}

/* Process the prior stripe. Invoked when the iterator has moved to the first
 * extent in the subsequent.
 */
static int
agg_process_stripe(struct ec_agg_param *agg_param, struct ec_agg_entry *entry)
{
	struct dtx_handle	*dth = agg_param->ap_dth;
	vos_iter_param_t	iter_param = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	bool			update_vos = true;
	bool			write_parity = true;
	bool			process_holes = false;
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
			 agg_recx_iter_pre_cb, NULL, entry, dth);
	D_DEBUG(DB_EPC, "Querying parity for stripe: %lu, offset: "DF_X64
		", "DF_RC"\n", entry->ae_cur_stripe.as_stripenum,
		iter_param.ip_recx.rx_idx, DP_RC(rc));
	if (rc != 0)
		goto out;

	/* with parity and higher than replicas, delete the old replica */
	if (ec_age_with_parity(entry) && ec_age_parity_higher(entry)) {
		update_vos = true;
		write_parity = false;
		D_DEBUG(DB_EPC, "delete replica for stripe: %lu,"
			DF_U64"/"DF_U64" eph "DF_X64" >= "DF_X64"\n",
			entry->ae_cur_stripe.as_stripenum,
			iter_param.ip_recx.rx_idx,
			iter_param.ip_recx.rx_nr,
			entry->ae_par_extent.ape_epoch,
			entry->ae_cur_stripe.as_hi_epoch);
		goto out;
	}

	/* Replicas constitute a full stripe, 1) no parity, or 2) with parity
	 * and all replica extents are newer than parity.
	 */
	if (ec_age_stripe_full(entry, ec_age_with_parity(entry))) {
		rc = agg_encode_local_parity(entry);
		goto out;
	}

	/* No parity, partial-stripe worth of replica, nothing to do */
	if (!ec_age_with_parity(entry)) {
		update_vos = false;
		goto out;
	}

	/* With parity and some newer partial replicas, possibly holes */
	if (ec_age_with_hole(entry))
		process_holes = true;
	else
		rc = agg_process_partial_stripe(entry);

out:
	if (process_holes && rc == 0) {
		rc = agg_process_holes(entry);
	} else if (update_vos && rc == 0) {
		if (ec_age2p(entry) > 1)  {
			/* offload of ds_obj_update to push remote parity */
			rc = agg_peer_update(entry, write_parity);
			if (rc)
				D_ERROR("agg_peer_update fail: "DF_RC"\n",
					DP_RC(rc));
		}

		if (rc == 0) {
			rc = agg_update_vos(agg_param, entry, write_parity);
			if (rc)
				D_ERROR("agg_update_vos failed: "DF_RC"\n",
					DP_RC(rc));
		}
	}

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

		*acts |= VOS_ITER_CB_YIELD;
	}

	return rc;
}

/* Compare function for keys.  Used to reset iterator position.
*/
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

/* Handles dkeys returned by the per-object nested iteratior.
*/
static int
agg_dkey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	if (agg_key_compare(agg_entry->ae_dkey, entry->ie_key)) {
		agg_entry->ae_dkey	= entry->ie_key;
		agg_reset_pos(VOS_ITER_AKEY, agg_entry);
	} else {
		D_DEBUG(DB_EPC, "Skip dkey: "DF_KEY" ec agg on re-probe\n",
			DP_KEY(&entry->ie_key));
		*acts |= VOS_ITER_CB_SKIP;
	}

	return 0;
}

/* Handles akeys returned by the iteratior.
 *
 */
static int
agg_akey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	if (entry->ie_child_type == VOS_ITER_SINGLE) {
		*acts |= VOS_ITER_CB_SKIP;
		return 0;
	}
	if (agg_key_compare(agg_entry->ae_akey, entry->ie_key)) {
		agg_entry->ae_akey = entry->ie_key;
		agg_entry->ae_thdl = ih;
	} else {
		D_DEBUG(DB_EPC, "Skip akey: "DF_KEY" ec agg on re-probe\n",
			DP_KEY(&entry->ie_key));
		*acts |= VOS_ITER_CB_SKIP;
	}

	return 0;
}

/* Invokes the yield function pointer.
*/
static inline bool
ec_aggregate_yield(struct ec_agg_param *agg_param)
{
	D_ASSERT(agg_param->ap_yield_func != NULL);

	return agg_param->ap_yield_func(agg_param->ap_yield_arg);
}

/* Post iteration call back for outer iterator
*/
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
		break;
	case VOS_ITER_DKEY:
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

/* Initializes the struct holding the iteration state (ec_agg_entry).
*/
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
		agg_reset_pos(VOS_ITER_OBJ, agg_entry);
		agg_entry->ae_codec = NULL;
	}
	if (oca)
		agg_entry->ae_oca	= *oca;

	if (daos_handle_is_valid(agg_entry->ae_obj_hdl)) {
		dsc_obj_close(agg_entry->ae_obj_hdl);
		agg_entry->ae_obj_hdl = DAOS_HDL_INVAL;
	}
	agg_reset_pos(VOS_ITER_DKEY, agg_entry);
	agg_reset_pos(VOS_ITER_AKEY, agg_entry);
	memset(agg_entry->ae_peer_pshards, 0,
	       (OBJ_EC_MAX_P) * sizeof(struct daos_shard_loc));

	agg_entry->ae_cur_stripe.as_stripenum	= 0UL;
	agg_entry->ae_cur_stripe.as_hi_epoch	= 0UL;
	agg_entry->ae_cur_stripe.as_stripe_fill = 0UL;
	agg_entry->ae_cur_stripe.as_extent_cnt	= 0U;
	agg_entry->ae_cur_stripe.as_offset	= 0U;
}

/* Iterator pre-callback for objects. Determines if object is subject
 * to aggregation. Skips objects that are not EC, or are not led by
 * this target.
 */
static int
agg_object(daos_handle_t ih, vos_iter_entry_t *entry,
	   struct ec_agg_param *agg_param, unsigned int *acts)
{
	struct ec_agg_pool_info *info = &agg_param->ap_pool_info;
	struct daos_oclass_attr  oca;
	int			 rc = 0;

	if (!daos_unit_oid_compare(agg_param->ap_agg_entry.ae_oid,
				   entry->ie_oid)) {
		D_DEBUG(DB_EPC, "Skip oid:"DF_UOID" ec agg on re-probe.\n",
			DP_UOID(entry->ie_oid));
		*acts |= VOS_ITER_CB_SKIP;
		goto out;
	}

	rc = dsc_obj_id2oc_attr(entry->ie_oid.id_pub, &info->api_props, &oca);
	if (rc) {
		D_ERROR("SKip object("DF_OID") with unknown class(%d)\n",
			DP_OID(entry->ie_oid.id_pub),
			daos_obj_id2class(entry->ie_oid.id_pub));
		*acts |= VOS_ITER_CB_SKIP;
		goto out;
	}

	if (!daos_oclass_is_ec(&oca)) { /* Skip non-EC object */
		D_DEBUG(DB_EPC, "Skip oid:"DF_UOID" non-ec obj\n",
			DP_UOID(entry->ie_oid));
		*acts |= VOS_ITER_CB_SKIP;
		goto out;
	}

	rc = ds_pool_check_dtx_leader(info->api_pool, &entry->ie_oid,
				      info->api_pool->sp_map_version, true);
	if (rc == 1 && entry->ie_oid.id_shard >= oca.u.ec.e_k) {
		D_DEBUG(DB_EPC, "oid:"DF_UOID" ec agg starting\n",
			DP_UOID(entry->ie_oid));

		agg_reset_entry(&agg_param->ap_agg_entry, entry, &oca);
		rc = 0;
		goto out;
	} else {
		if (rc < 0) {
			D_ERROR("oid:"DF_UOID" ds_pool_check_leader failed "
				DF_RC"\n", DP_UOID(entry->ie_oid), DP_RC(rc));
			rc = 0;
		}
		*acts |= VOS_ITER_CB_SKIP;
	}
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

	switch (type) {
	case VOS_ITER_OBJ:
		agg_param->ap_epr = param->ip_epr;
		rc = agg_object(ih, entry, agg_param, acts);
		break;
	case VOS_ITER_DKEY:
		rc = agg_dkey(ih, entry, agg_entry, acts);
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

	agg_param->ap_credits++;
	if (agg_param->ap_credits > agg_param->ap_credits_max) {
		agg_param->ap_credits = 0;
		*acts |= VOS_ITER_CB_YIELD;
		D_DEBUG(DB_EPC, "EC aggregation yield type %d. acts %u\n",
			type, *acts);
		if (!(*acts & VOS_ITER_CB_SKIP))
			agg_reset_pos(type, agg_entry);
		if (ec_aggregate_yield(agg_param)) {
			D_DEBUG(DB_EPC, "EC aggregation aborted\n");
			rc = 1;
		}
	}

	return rc;
}

struct agg_iv_ult_arg {
	struct ec_agg_param	*param;
	ABT_eventual		eventual;
};

/* Captures the IV values need for pool and container open. Runs in
 * system xstream.
 */
static void
agg_iv_ult(void *arg)
{
	struct agg_iv_ult_arg	*ult_arg = arg;
	struct ec_agg_param	*agg_param = ult_arg->param;
	struct daos_prop_entry	*entry = NULL;
	daos_prop_t		*prop;
	int			 rc = 0;

	rc = ds_pool_iv_srv_hdl_fetch(agg_param->ap_pool_info.api_pool,
				      &agg_param->ap_pool_info.api_poh_uuid,
				      &agg_param->ap_pool_info.api_coh_uuid);
	if (rc)
		goto out;

	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		D_ERROR("Property allocation failed\n");
		rc = -DER_NOMEM;
		goto out;
	}

	rc = ds_pool_iv_prop_fetch(agg_param->ap_pool_info.api_pool, prop);
	if (rc) {
		D_ERROR("ds_pool_iv_prop_fetch failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_LIST);
	D_ASSERT(entry != NULL);
	rc = d_rank_list_dup(&agg_param->ap_pool_info.api_svc_list,
			     (d_rank_list_t *)entry->dpe_val_ptr);
	if (rc) {
		D_ERROR("Failed to duplicate service list\n");
		goto out;
	}
	daos_prop_free(prop);
out:
	D_DEBUG(DB_IO, DF_UUID" get iv for agg: %d\n",
		DP_UUID(agg_param->ap_pool_info.api_cont_uuid), rc);
	ABT_eventual_set(ult_arg->eventual, (void *)&rc, sizeof(rc));
}

static void
ec_agg_param_fini(struct ec_agg_param *agg_param)
{
	if (daos_handle_is_valid(agg_param->ap_pool_info.api_cont_hdl))
		dsc_cont_close(agg_param->ap_pool_info.api_pool_hdl,
			       agg_param->ap_pool_info.api_cont_hdl);

	d_sgl_fini(&agg_param->ap_agg_entry.ae_sgl, true);
	if (daos_handle_is_valid(agg_param->ap_pool_info.api_pool_hdl))
		dsc_pool_close(agg_param->ap_pool_info.api_pool_hdl);

	if (agg_param->ap_pool_info.api_svc_list)
		d_rank_list_free(agg_param->ap_pool_info.api_svc_list);

	memset(agg_param, 0, sizeof(*agg_param));
}

static void
ec_agg_param_init(struct ds_cont_child *cont, struct agg_param *param)
{
	struct ec_agg_param	*agg_param = param->ap_data;
	struct ec_agg_pool_info *info = &agg_param->ap_pool_info;
	struct agg_iv_ult_arg	arg;
	ABT_eventual		eventual;
	int			*status;
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
	D_INIT_LIST_HEAD(&agg_param->ap_agg_entry.ae_cur_stripe.as_hoextents);

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	arg.param = agg_param;
	arg.eventual = eventual;
	rc = dss_ult_periodic(agg_iv_ult, &arg, DSS_XS_SYS, 0, 0, NULL);
	if (rc)
		D_GOTO(free_eventual, rc = dss_abterr2der(rc));

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(free_eventual, rc = dss_abterr2der(rc));

	if (*status != 0)
		D_GOTO(free_eventual, rc = *status);

	rc = dsc_pool_open(info->api_pool_uuid,
			   info->api_poh_uuid, DAOS_PC_RW,
			   NULL, info->api_pool->sp_map,
			   info->api_svc_list, &info->api_pool_hdl);
	if (rc) {
		D_ERROR("dsc_pool_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(free_eventual, rc);
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
free_eventual:
	ABT_eventual_free(&eventual);
out:
	if (rc) {
		D_DEBUG(DB_EPC, "aggregate param init failed: %d\n", rc);
		ec_agg_param_fini(agg_param);
	} else {
		agg_param->ap_initialized = 1;
	}
}

/* Iterates entire VOS. Invokes nested iterator to recurse through trees
 * for all objects meeting the criteria: object is EC, and this target is
 * leader.
 */
static int
cont_ec_aggregate_cb(struct ds_cont_child *cont, daos_epoch_range_t *epr,
		     bool full_scan, struct agg_param *agg_param)
{
	struct ec_agg_param	 *ec_agg_param = agg_param->ap_data;
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_iter_anchors  anchors = { 0 };
	struct dtx_handle	 dth = { 0 };
	struct dtx_id		 dti = { 0 };
	struct dtx_epoch	 epoch = { 0 };
	daos_unit_oid_t		 oid = { 0 };
	int			 rc = 0;

	/*
	 * Avoid calling into vos_aggregate() when aborting aggregation
	 * on ds_cont_child purging.
	 */
	D_ASSERT(cont->sc_ec_agg_req != NULL);
	if (dss_ult_exiting(cont->sc_ec_agg_req))
		return 1;

	if (!ec_agg_param->ap_initialized)
		ec_agg_param_init(cont, agg_param);

	iter_param.ip_hdl		= cont->sc_hdl;
	iter_param.ip_epr.epr_lo	= epr->epr_lo;
	iter_param.ip_epr.epr_hi	= epr->epr_hi;
	iter_param.ip_epc_expr		= VOS_IT_EPC_RR;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx	= 0ULL;
	iter_param.ip_recx.rx_nr	= ~PARITY_INDICATOR;

	agg_reset_entry(&ec_agg_param->ap_agg_entry, NULL, NULL);

	rc = dtx_begin(cont->sc_hdl, &dti, &epoch, 0, 0, &oid,
		       NULL, 0, 0, NULL, &dth);
	if (rc != 0) {
		D_ERROR("Fail to start DTX for EC aggregation: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	ec_agg_param->ap_dth = &dth;

again:
	rc = vos_iterate(&iter_param, VOS_ITER_OBJ, true, &anchors,
			 agg_iterate_pre_cb, agg_iterate_post_cb,
			 ec_agg_param, &dth);
	if (obj_dtx_need_refresh(&dth, rc)) {
		rc = dtx_refresh(&dth, cont);
		if (rc == -DER_AGAIN) {
			anchors.ia_reprobe_co = 0;
			anchors.ia_reprobe_obj = 0;
			anchors.ia_reprobe_dkey = 0;
			anchors.ia_reprobe_akey = 0;
			anchors.ia_reprobe_sv = 0;
			anchors.ia_reprobe_ev = 0;
			goto again;
		}
	}

	dtx_end(&dth, cont, rc);

	if (daos_handle_is_valid(ec_agg_param->ap_agg_entry.ae_obj_hdl)) {
		dsc_obj_close(ec_agg_param->ap_agg_entry.ae_obj_hdl);
		ec_agg_param->ap_agg_entry.ae_obj_hdl = DAOS_HDL_INVAL;
	}

	if (rc == 0)
		cont->sc_ec_agg_eph = max(cont->sc_ec_agg_eph, epr->epr_hi);

	return rc;
}

static uint64_t
cont_ec_agg_start_eph_get(struct ds_cont_child *cont)
{
	return cont->sc_ec_agg_eph;
}

void
ds_obj_ec_aggregate(void *arg)
{
	struct ds_cont_child	*cont = arg;
	struct ec_agg_param	agg_param = { 0 };
	struct agg_param	param = { 0 };

	D_DEBUG(DB_EPC, "start EC aggregation "DF_UUID"\n",
		DP_UUID(cont->sc_uuid));
	param.ap_data = &agg_param;
	param.ap_start_eph_get = cont_ec_agg_start_eph_get;
	param.ap_cont = cont;
	param.ap_req = cont->sc_ec_agg_req;
	ec_agg_param_init(cont, &param);

	cont_aggregate_interval(cont, cont_ec_aggregate_cb, &param);

	ec_agg_param_fini(&agg_param);
}
