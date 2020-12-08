/**
 * (C) Copyright 2020 Intel Corporation.
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
 "
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include <daos_srv/daos_server.h>
#include <daos_srv/srv_obj_ec.h>
#include "obj_ec.h"
#include "obj_internal.h"

#define EC_AGG_ITERATION_MAX 256

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
	d_rank_list_t	*api_svc_list;		/* service list               */
	struct ds_pool	*api_pool;		/* Used for IV fetch          */
	ABT_eventual	 api_eventual;		/* eventual for sys offload   */
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
	unsigned int	as_ho_ext_cnt;  /* number of holdover extents        */
	unsigned int	as_offset;      /* start offset in stripe            */
	bool		as_has_holes;   /* stripe includes holes             */
};

/* Aggregation state for an object.
 */
struct ec_agg_entry {
	daos_unit_oid_t		 ae_oid;	 /* OID of iteration entry    */
	struct daos_oclass_attr	*ae_oca;	 /* Object class of object    */
	struct obj_ec_codec	*ae_codec;	 /* Encode/decode for oclass  */
	d_sg_list_t		 ae_sgl;	 /* Mem for entry processing  */
	daos_handle_t		 ae_thdl;	 /* Iterator handle           */
	daos_key_t		 ae_dkey;	 /* Current dkey              */
	daos_key_t		 ae_akey;	 /* Current akey              */
	daos_size_t		 ae_rsize;	 /* Record size of cur array  */
	struct ec_agg_stripe	 ae_cur_stripe;  /* Struct for current stripe */
	struct ec_agg_par_extent ae_par_extent;	 /* Parity extent             */
	daos_handle_t		 ae_obj_hdl;	 /* Object handle for cur obj */
	struct daos_shard_loc	 ae_peer_pshards[OBJ_EC_MAX_P - 1];
};

/* Parameters used to drive iterate all.
 */
struct ec_agg_param {
	struct ec_agg_pool_info	 ap_pool_info;	 /* pool/cont info            */
	struct ec_agg_entry	 ap_agg_entry;	 /* entry used for each OID   */
	daos_epoch_range_t	 ap_epr;	 /* hi/lo extent threshold    */
	daos_prop_t		*ap_prop;        /* property for cont open    */
	daos_handle_t		 ap_cont_handle; /* VOS container handle      */
	bool			(*ap_yield_func)(void *arg); /* yield function*/
	void			*ap_yield_arg;   /* yield argument            */
	uint32_t		 ap_credits_max; /* # of tight loops to yield */
	uint32_t		 ap_credits;     /* # of tight loops          */
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
	ABT_eventual		 asu_eventual;  /* Eventual for offload  */
};

/* Represents an replicated data extent.
 */
struct ec_agg_extent {
	d_list_t	ae_link;        /* for extents list   */
	daos_recx_t	ae_recx;        /* idx, nr for extent */
	daos_recx_t	ae_orig_recx;   /* For removal        */
	daos_epoch_t	ae_epoch;       /* epoch for extent   */
	bool		ae_hole;        /* extent is a hole   */
};

/* Determines if the extent carries over into the next stripe.
 */
static unsigned int
agg_carry_over(struct ec_agg_entry *entry, struct ec_agg_extent *agg_extent)
{
	unsigned int	stripe_size = obj_ec_stripe_rec_nr(entry->ae_oca);

	daos_off_t	start_stripe = agg_extent->ae_recx.rx_idx / stripe_size;
	daos_off_t	end_stripe = (agg_extent->ae_recx.rx_idx +
				agg_extent->ae_recx.rx_nr - 1) / stripe_size;

	if (end_stripe > start_stripe) {
		D_ASSERT(end_stripe - start_stripe == 1);
		return agg_extent->ae_recx.rx_idx + agg_extent->ae_recx.rx_nr -
			end_stripe * stripe_size;

	/* What if an extent carries over, and the tail is the only extent in
	 * the next stripe? (Answer: we retain it, but this is okay, since in
	 * this case the carryover is a valid replica for the next stripe)
	 */
	}
	return 0;
}

/* Clears the extent list of all extents completed for the processed stripe.
 * Extents the carry over to the next stripe have the prior-stripe prefix
 * trimmed.
 */
static void
agg_clear_extents(struct ec_agg_entry *entry)
{
	struct ec_agg_extent	*extent, *ext_tmp;
	unsigned int		 tail, ptail = 0U;
	bool			 carry_is_hole = false;

	if (entry->ae_cur_stripe.as_ho_ext_cnt) {
		d_list_for_each_entry_safe(extent, ext_tmp,
					   &entry->ae_cur_stripe.as_hoextents,
					   ae_link) {
			d_list_del(&extent->ae_link);
			D_FREE_PTR(extent);
		}
		entry->ae_cur_stripe.as_ho_ext_cnt = 0;
	}

	d_list_for_each_entry_safe(extent, ext_tmp,
				   &entry->ae_cur_stripe.as_dextents,
				   ae_link) {
		unsigned long next_stripe_st =
			(entry->ae_cur_stripe.as_stripenum + 1) *
				 obj_ec_stripe_rec_nr(entry->ae_oca);

		/* Check for carry-over extent. */
		tail = agg_carry_over(entry, extent);
		/* At most one extent should carry over. */

		if (extent->ae_hole)
			carry_is_hole = true;

		if (tail) {
			D_ASSERT(ptail == 0U);
			ptail = tail;
			extent->ae_recx.rx_idx += extent->ae_recx.rx_nr - tail;

			D_ASSERT(extent->ae_recx.rx_idx == next_stripe_st);

			extent->ae_recx.rx_nr = tail;
			entry->ae_cur_stripe.as_hi_epoch = extent->ae_epoch;
		}

		if (extent->ae_orig_recx.rx_idx + extent->ae_orig_recx.rx_nr >
		    next_stripe_st) {
			entry->ae_cur_stripe.as_extent_cnt--;
			d_list_del(&extent->ae_link);
			d_list_add_tail(&extent->ae_link,
					&entry->ae_cur_stripe.as_hoextents);
			entry->ae_cur_stripe.as_ho_ext_cnt++;
		} else {
			entry->ae_cur_stripe.as_extent_cnt--;
			d_list_del(&extent->ae_link);
			D_FREE_PTR(extent);
		}
	}
	entry->ae_cur_stripe.as_offset = 0U;
	/* Account for carry over. */
	if (ptail) {
		D_ASSERT(entry->ae_cur_stripe.as_extent_cnt == 1);
		entry->ae_cur_stripe.as_stripenum++;
	} else {
		D_ASSERT(entry->ae_cur_stripe.as_extent_cnt == 0);
		entry->ae_cur_stripe.as_hi_epoch = 0UL;
	}
	entry->ae_cur_stripe.as_stripe_fill = ptail;
	entry->ae_cur_stripe.as_has_holes = carry_is_hole ? true : false;
}

/* Returns the stripe number for the stripe containing ex_lo.
 */
static inline daos_off_t
agg_stripenum(struct ec_agg_entry *entry, daos_off_t ex_lo)
{
	return ex_lo / obj_ec_stripe_rec_nr(entry->ae_oca);
}

/* Call back for the nested iterator used to find the parity for a stripe.
 */
static int
agg_recx_iter_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		     vos_iter_type_t type, vos_iter_param_t *param,
		     void *cb_arg, unsigned int *acts)
{
	struct ec_agg_entry	*agg_entry = (struct ec_agg_entry *)cb_arg;
	int			 rc = 0;

	D_ASSERT(type == VOS_ITER_RECX);
	D_ASSERT(entry->ie_recx.rx_idx == (PARITY_INDICATOR |
		(agg_entry->ae_cur_stripe.as_stripenum *
			agg_entry->ae_oca->u.ec.e_len)));
	agg_entry->ae_par_extent.ape_recx = entry->ie_recx;
	agg_entry->ae_par_extent.ape_epoch = entry->ie_epoch;
	return rc;
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
	int		 rc = 0;

	if (align_data) {
		D_FREE(sgl->sg_iovs[iov_entry].iov_buf);
		sgl->sg_iovs[iov_entry].iov_buf =
			aligned_alloc(32, ent_buf_len);
		if (sgl->sg_iovs[iov_entry].iov_buf == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
	} else {
		unsigned int *buf = NULL;

		D_REALLOC(buf, sgl->sg_iovs[iov_entry].iov_buf, ent_buf_len);
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
	unsigned int	 len = entry->ae_oca->u.ec.e_len;
	unsigned int	 k = entry->ae_oca->u.ec.e_k;
	unsigned int	 p = entry->ae_oca->u.ec.e_p;
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
out:
	return rc;

}

/* Recovers allocated memory for sgl used in the aggregation process.
 */
static void
agg_sgl_fini(d_sg_list_t *sgl)
{
	int i;

	if (sgl->sg_iovs) {
		for (i = 0; i < AGG_IOV_CNT; i++)
			D_FREE(sgl->sg_iovs[i].iov_buf);
		D_FREE(sgl->sg_iovs);
	}
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
				D_ASSERT(fcbit_map);
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
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
	int			 i, j, rc = 0;
	bool			 opened = false;

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	if (daos_handle_is_inval(entry->ae_obj_hdl)) {
		rc = dsc_obj_open(agg_param->ap_pool_info.api_cont_hdl,
				  entry->ae_oid.id_pub, DAOS_OO_RW,
				  &entry->ae_obj_hdl);
		if (!rc)
			opened = true;
	}
	if (opened) {
		d_rank_t myrank;

		crt_group_rank(NULL, &myrank);
		dc_obj_layout_get(entry->ae_obj_hdl, &layout);
		for (i = 0; i < layout->ol_nr; i++)
			for (j = 0; j < layout->ol_shards[i]->os_replica_nr;
			     j++) {
				int p = 0;

				if (j >= k && layout->ol_shards[i]->
				    os_shard_loc[j].sd_rank != myrank) {
					entry->ae_peer_pshards[p].sd_rank
						= layout->ol_shards[i]->
						os_shard_loc[j].sd_rank;
					entry->ae_peer_pshards[p++].sd_tgt_idx
						= layout->ol_shards[i]->
						os_shard_loc[j].sd_tgt_idx;
				}
			}
		daos_obj_layout_free(layout);
	}
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
	daos_epoch_t		 epoch = { 0 };	/* epoch used for data fetch */
	daos_recx_t		*recxs = NULL;
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
	unsigned int		 i, j;
	int			 rc = 0;

	D_ALLOC_ARRAY(recxs, cell_cnt);
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

	iod.iod_name	= entry->ae_akey;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= entry->ae_rsize;
	iod.iod_nr	= cell_cnt;
	iod.iod_recxs	= recxs;
	entry->ae_sgl.sg_nr = 1;
	entry->ae_sgl.sg_iovs[AGG_IOV_ODATA].iov_len = cell_cnt * len *
								entry->ae_rsize;
	(entry->ae_sgl.sg_iovs)++;

	rc = agg_get_obj_handle(entry);
	if (rc) {
		D_ERROR("Failed to open object: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	epoch = is_recalc ? entry->ae_cur_stripe.as_hi_epoch :
					 entry->ae_par_extent.ape_epoch;
	rc = dsc_obj_fetch(entry->ae_obj_hdl, epoch, &entry->ae_dkey, 1, &iod,
			   &entry->ae_sgl, NULL, 0, NULL);
	if (rc)
		D_ERROR("dsc_obj_fetch failed: "DF_RC"\n", DP_RC(rc));

	(entry->ae_sgl.sg_iovs)--;
	entry->ae_sgl.sg_nr = AGG_IOV_CNT;
out:
	D_FREE(recxs);
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
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
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
			   entry->ae_cur_stripe.as_hi_epoch,
			   VOS_OF_FETCH_RECX_LIST, &entry->ae_dkey, 1, &iod,
			   &entry->ae_sgl);
	if (rc)
		D_ERROR("vos_obj_fetch failed: "DF_RC"\n", DP_RC(rc));
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
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
	unsigned int		 p = entry->ae_oca->u.ec.e_p;
	unsigned int		 cell_bytes = len * entry->ae_rsize;
	unsigned char		*data[OBJ_EC_MAX_K];
	unsigned char		*parity_bufs[OBJ_EC_MAX_P];
	unsigned char		*buf;
	int			 i, rc = 0;

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	for (i = 0; i < k; i++)
		data[i] = &buf[i * cell_bytes];

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	for (i = p - 1; i >= 0; i--)
		parity_bufs[i] = &buf[i * cell_bytes];

	if (entry->ae_codec == NULL)
		entry->ae_codec =
		obj_ec_codec_get(daos_obj_id2class(entry->ae_oid.id_pub));
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
			    DSS_ULT_EC, tid, 0, NULL);
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
agg_data_is_newer(struct ec_agg_entry *entry)
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
 * constitute a full stripe. If parity exists for the the stripe, the replicas
 * making up the full stripe must be a later epoch that the parity.
 */
static bool
agg_stripe_is_filled(struct ec_agg_entry *entry, bool has_parity)
{
	bool	is_filled, rc = false;

	D_ASSERT(entry->ae_cur_stripe.as_stripe_fill <=
		 obj_ec_stripe_rec_nr(entry->ae_oca));

	is_filled = entry->ae_cur_stripe.as_stripe_fill ==
		obj_ec_stripe_rec_nr(entry->ae_oca);

	if (is_filled)
		if (!has_parity || agg_data_is_newer(entry))
			rc = true;
	return rc;
}

/* True if all original extents are contained within the current stripe.
*/
static bool
agg_contained(struct ec_agg_entry *entry)
{
	struct ec_agg_extent	*extent;
	unsigned int		 ss, se;

	ss = entry->ae_oca->u.ec.e_len * entry->ae_oca->u.ec.e_k *
		entry->ae_cur_stripe.as_stripenum;
	se = ss + entry->ae_oca->u.ec.e_len * entry->ae_oca->u.ec.e_k;
	d_list_for_each_entry(extent, &entry->ae_cur_stripe.as_dextents,
			      ae_link)
		if (extent->ae_orig_recx.rx_idx < ss ||
		    extent->ae_orig_recx.rx_idx + extent->ae_orig_recx.rx_nr >
		    se)
			return false;
	return true;
}

/* Removes from VOS any holdover extents from prior stripe.
 */
static int
agg_remove_holdovers(struct ec_agg_entry *entry)
{
	daos_epoch_range_t	 epoch_range = { 0 };
	struct ec_agg_extent	*ext;
	struct ec_agg_param	*ap;
	int			 rc = 0;

	ap = container_of(entry, struct ec_agg_param, ap_agg_entry);
	d_list_for_each_entry(ext, &entry->ae_cur_stripe.as_hoextents,
			      ae_link) {
		int		erc = 0;

		epoch_range.epr_lo = epoch_range.epr_hi = ext->ae_epoch;
		erc = vos_obj_array_remove(ap->ap_cont_handle,
					   entry->ae_oid,
					   &epoch_range,
					   &entry->ae_dkey,
					   &entry->ae_akey,
					   &ext->ae_orig_recx);
			if (erc)
				D_ERROR("array_remov failed: "DF_RC"\n",
					DP_RC(erc));
			if (!rc && erc)
				rc = erc;
	}
	return rc;
}

/* Writes updated parity to VOS, and removes replicas fully contained
 * in the processed stripe.
 */
static int
agg_update_vos(struct ec_agg_entry *entry, bool write_parity)
{
	daos_recx_t		 recx = { 0 };
	daos_epoch_range_t	 epoch_range = { 0 };
	struct ec_agg_param	*ap;
	struct ec_agg_extent	*ext;
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	int			 rc = 0;

	ap = container_of(entry, struct ec_agg_param, ap_agg_entry);

	if (write_parity) {
		d_sg_list_t	 sgl = { 0 };
		daos_iod_t	 iod = { 0 };
		d_iov_t		*iov;

		iov = &entry->ae_sgl.sg_iovs[AGG_IOV_PARITY];
		sgl.sg_iovs = iov;
		sgl.sg_nr = 1;
		iod.iod_nr = 1;
		iod.iod_size = entry->ae_rsize;
		iod.iod_name = entry->ae_akey;
		iod.iod_type = DAOS_IOD_ARRAY;
		iod.iod_recxs = &recx;
		recx.rx_idx = (entry->ae_cur_stripe.as_stripenum * len) |
							PARITY_INDICATOR;
		recx.rx_nr = len;
		rc = vos_obj_update(ap->ap_cont_handle, entry->ae_oid,
				    entry->ae_cur_stripe.as_hi_epoch, 0, 0,
				    &entry->ae_dkey, 1, &iod, NULL, &sgl);
		if (rc) {
			D_ERROR("vos_obj_update failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
	}

	if (entry->ae_cur_stripe.as_ho_ext_cnt == 0 && agg_contained(entry)) {
		epoch_range = ap->ap_epr;
		recx.rx_idx = entry->ae_cur_stripe.as_stripenum *
			obj_ec_stripe_rec_nr(entry->ae_oca);
		recx.rx_nr = obj_ec_stripe_rec_nr(entry->ae_oca);
		rc = vos_obj_array_remove(ap->ap_cont_handle,
					  entry->ae_oid, &epoch_range,
					  &entry->ae_dkey, &entry->ae_akey,
					  &recx);

	} else {
		d_list_for_each_entry(ext, &entry->ae_cur_stripe.as_dextents,
				      ae_link) {
			int		erc = 0;
			unsigned int	se = entry->ae_oca->u.ec.e_len *
				entry->ae_oca->u.ec.e_k *
				(entry->ae_cur_stripe.as_stripenum + 1);

			if (ext->ae_orig_recx.rx_idx +
			    ext->ae_orig_recx.rx_nr <= se) {
				epoch_range.epr_lo = epoch_range.epr_hi =
					ext->ae_epoch;

				erc = vos_obj_array_remove(ap->ap_cont_handle,
							   entry->ae_oid,
							   &epoch_range,
							   &entry->ae_dkey,
							   &entry->ae_akey,
							   &ext->ae_orig_recx);
				if (erc)
					D_ERROR("array_remove failed: "DF_RC"\n",
						DP_RC(erc));
				if (!rc && erc)
					rc = erc;
			}
		}

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
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
	unsigned int		 i, j;
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
	buf = (unsigned char *)entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	for (i = 0; i < cell_cnt; i++) {
		d_iov_set(&sgl.sg_iovs[i], &buf[i * len], len);
		/* zeroed to ensure correctness when cell is partially filled */
		memset(sgl.sg_iovs[i].iov_buf, 0, len);
	}

	/* fetch the local parity */
	if (!is_recalc)
		/* This is correct for p > 1, since the leader is last, but we
		 * reverse the order in the parity buf.
		 */
		d_iov_set(&sgl.sg_iovs[cell_cnt],
			  entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf, len);

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
	unsigned int	 len = entry->ae_oca->u.ec.e_len;
	unsigned int	 p = entry->ae_oca->u.ec.e_p;
	unsigned int	 pshard  = entry->ae_oid.id_shard - 1;
	int		 i, rc = 0;

	/* Only called when p > 1. */
	D_ASSERT(p > 1);

	recx.rx_idx = (entry->ae_cur_stripe.as_stripenum * len)
							| PARITY_INDICATOR;
	/* set up the iod */
	recx.rx_nr = len;
	iod.iod_recxs = &recx;
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = 1;

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	for (i = 1; i < p; i++) {
		D_ASSERT(pshard >= entry->ae_oca->u.ec.e_k);
		d_iov_set(&iov, &buf[i * len], len);
		rc = dsc_obj_fetch(entry->ae_obj_hdl,
				   entry->ae_par_extent.ape_epoch,
				   &entry->ae_dkey, 1, &iod, &sgl, NULL,
				   DIOF_TO_SPEC_SHARD, &pshard);
		if (rc)
			goto out;
		pshard--;
	}
out:
	return rc;
}

/* Performs an incremental update of the existing parity for the stripe.
 */
static int
agg_update_parity(struct ec_agg_entry *entry, uint8_t *bit_map,
		  unsigned int cell_cnt)
{
	unsigned int	 len = entry->ae_oca->u.ec.e_len;
	unsigned int	 k = entry->ae_oca->u.ec.e_k;
	unsigned int	 p = entry->ae_oca->u.ec.e_p;
	unsigned int	 cell_bytes = len * entry->ae_rsize;
	unsigned char	*parity_bufs[OBJ_EC_MAX_P];
	unsigned char	*vects[3];
	unsigned char	*buf = NULL;
	unsigned char	*obuf = NULL;
	unsigned char	*old = NULL;
	unsigned char	*new = NULL;
	unsigned char	*diff = NULL;
	int		 i, j, rc = 0;

	if (entry->ae_codec == NULL)
		entry->ae_codec =
		obj_ec_codec_get(daos_obj_id2class(entry->ae_oid.id_pub));

	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;

	for (i = p - 1, j = 0; i >= 0; i--)
		parity_bufs[j++] = &buf[i * cell_bytes];

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
	unsigned int	 len = entry->ae_oca->u.ec.e_len;
	unsigned int	 k = entry->ae_oca->u.ec.e_k;
	unsigned int	 p = entry->ae_oca->u.ec.e_p;
	unsigned int	 cell_bytes = len * entry->ae_rsize;
	unsigned char	*parity_bufs[OBJ_EC_MAX_P];
	unsigned char	*data[OBJ_EC_MAX_K];
	unsigned char	*buf;
	unsigned char	*rbuf = entry->ae_sgl.sg_iovs[AGG_IOV_ODATA].iov_buf;
	unsigned char	*lbuf = entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_buf;
	int		 i, j, r, l = 0;

	for (i = 0, r = 0; i < k; i++) {
		if (isset(bit_map, i))
			data[i] = &rbuf[r++ * cell_bytes];
		 else
			data[i] = &lbuf[l++ * cell_bytes];
	}
	D_ASSERT(r == cell_cnt);
	buf = entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
	D_ASSERT(p > 0);
	for (i = p - 1, j = 0; i >= 0; i--)
		parity_bufs[j++] = &buf[i * cell_bytes];

	if (entry->ae_codec == NULL)
		entry->ae_codec =
		obj_ec_codec_get(daos_obj_id2class(entry->ae_oid.id_pub));
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
	unsigned int		 p = entry->ae_oca->u.ec.e_p;
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
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
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
			    DSS_ULT_EC, tid, 0, NULL);
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
	d_iov_t			 iov = { 0 };
	d_sg_list_t		 sgl = { 0 };
	crt_endpoint_t		 tgt_ep = { 0 };
	unsigned char		*buf = NULL;
	struct obj_ec_agg_in	*ec_agg_in = NULL;
	struct obj_ec_agg_out	*ec_agg_out = NULL;
	struct ec_agg_param	*agg_param;
	crt_rpc_t		*rpc;
	int			 rc = 0;

	tgt_ep.ep_rank = entry->ae_peer_pshards[0].sd_rank;
	tgt_ep.ep_tag = entry->ae_peer_pshards[0].sd_tgt_idx;
	rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
			    DAOS_OBJ_RPC_EC_AGGREGATE, &rpc);
	if (rc) {
		D_ERROR("obj_req_create failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	ec_agg_in = crt_req_get(rpc);
	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	uuid_copy(ec_agg_in->ea_pool_uuid,
		  agg_param->ap_pool_info.api_pool_uuid);
	uuid_copy(ec_agg_in->ea_coh_uuid,
		  agg_param->ap_pool_info.api_poh_uuid);
	uuid_copy(ec_agg_in->ea_cont_uuid,
		  agg_param->ap_pool_info.api_cont_uuid);
	uuid_copy(ec_agg_in->ea_coh_uuid,
		  agg_param->ap_pool_info.api_coh_uuid);
	ec_agg_in->ea_oid = entry->ae_oid;
	ec_agg_in->ea_oid.id_shard--;
	ec_agg_in->ea_dkey = entry->ae_dkey;
	ec_agg_in->ea_akey = entry->ae_akey;
	ec_agg_in->ea_epoch_range.epr_lo = agg_param->ap_epr.epr_lo;
	ec_agg_in->ea_epoch_range.epr_hi = entry->ae_cur_stripe.as_hi_epoch;
	ec_agg_in->ea_stripenum = entry->ae_cur_stripe.as_stripenum;
	ec_agg_in->ea_map_ver =
		agg_param->ap_pool_info.api_pool->sp_map_version;

	if (stripe_ud->asu_write_par) {
		ec_agg_in->ea_rsize = entry->ae_rsize;
		buf = (unsigned char *)
			entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf;
		d_iov_set(&iov,
			  &buf[entry->ae_oca->u.ec.e_len * entry->ae_rsize],
			  entry->ae_oca->u.ec.e_len * entry->ae_rsize);
		sgl.sg_iovs = &iov;
		sgl.sg_nr = sgl.sg_nr_out = 1;
		rc = crt_bulk_create(dss_get_module_info()->dmi_ctx, &sgl,
				     CRT_BULK_RW, &ec_agg_in->ea_bulk);
		if (rc) {
			D_ERROR("crt_bulk_create failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
	}

	if (entry->ae_cur_stripe.as_ho_ext_cnt || !agg_contained(entry)) {
		struct ec_agg_extent	*ext;
		int			 i = 0;

		ec_agg_in->ea_remove_nr = entry->ae_cur_stripe.as_ho_ext_cnt +
			entry->ae_cur_stripe.as_extent_cnt;
		D_ALLOC_ARRAY(ec_agg_in->ea_remove_recxs.ca_arrays,
			      ec_agg_in->ea_remove_nr);
		if (ec_agg_in->ea_remove_recxs.ca_arrays == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		D_ALLOC_ARRAY(ec_agg_in->ea_remove_eps.ca_arrays,
			      ec_agg_in->ea_remove_nr);
		if (ec_agg_in->ea_remove_eps.ca_arrays == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}

		d_list_for_each_entry(ext, &entry->ae_cur_stripe.as_dextents,
				      ae_link) {
			ec_agg_in->ea_remove_recxs.ca_arrays[i] =
				ext->ae_orig_recx;
			ec_agg_in->ea_remove_eps.ca_arrays[i++] =
				ext->ae_epoch;
		}
		if (entry->ae_cur_stripe.as_ho_ext_cnt) {
			d_list_for_each_entry(ext,
					      &entry->
					      ae_cur_stripe.as_hoextents,
					      ae_link) {
				ec_agg_in->ea_remove_recxs.ca_arrays[i] =
					ext->ae_orig_recx;
				ec_agg_in->ea_remove_eps.ca_arrays[i++] =
					ext->ae_epoch;
			}
		}
	}

	rc = dss_rpc_send(rpc);
	if (rc) {
		D_ERROR("dss_rpc_send failed: "DF_RC"\n", DP_RC(rc));
		if (stripe_ud->asu_write_par)
			crt_bulk_free(ec_agg_in->ea_bulk);
		goto out;
	}
	ec_agg_out = crt_reply_get(rpc);
	rc = ec_agg_out->ea_status;
	if (rc)
		D_ERROR("remote update rpc failed: "DF_RC"\n", DP_RC(rc));

	if (stripe_ud->asu_write_par)
		crt_bulk_free(ec_agg_in->ea_bulk);
out:
	if (ec_agg_in->ea_remove_nr) {
		D_FREE(ec_agg_in->ea_remove_recxs.ca_arrays);
		D_FREE(ec_agg_in->ea_remove_eps.ca_arrays);
	}
	if (rpc)
		crt_req_decref(rpc);
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
	unsigned int		 failed_tgts_cnt = 0;
	int			 tid, rc = 0;

	D_ASSERT(!write_parity ||
		 entry->ae_sgl.sg_iovs[AGG_IOV_PARITY].iov_buf);

	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = pool_map_find_failed_tgts(agg_param->ap_pool_info.api_pool->sp_map,
				       &targets, &failed_tgts_cnt);
	if (rc) {
		D_ERROR("pool_map_find_failed_tgts failed: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}
	if (targets != NULL) {
		int i;

		for (i = 0; i < failed_tgts_cnt; i++) {
			if (targets[i].ta_comp.co_rank ==
			    entry->ae_peer_pshards[0].sd_rank) {
				D_ERROR("peer parity tgt failed\n");
				rc = -1;
				goto out;
			}
		}
	}
	if (write_parity)
		stripe_ud.asu_write_par = true;

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
			    DSS_ULT_EC, tid, 0, NULL);
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

static void
agg_process_holes_ult(void *arg)
{
	daos_iod_t		 iod = { 0 };
	crt_endpoint_t		 tgt_ep = { 0 };
	struct ec_agg_stripe_ud	*stripe_ud = (struct ec_agg_stripe_ud *)arg;
	struct ec_agg_entry	*entry = stripe_ud->asu_agg_entry;
	struct ec_agg_extent	*agg_extent, *ext_tmp;
	struct pool_target	*targets;
	struct ec_agg_param	*agg_param;
	struct obj_ec_rep_in	*ec_rep_in = NULL;
	struct obj_ec_rep_out	*ec_rep_out = NULL;
	crt_rpc_t		*rpc;
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
	unsigned long		 ss = entry->ae_cur_stripe.as_stripenum *
					k * len;
	unsigned int		 last_ext_end = 0;
	unsigned int		 ext_cnt = 0;
	unsigned int		 ext_tot_len = 0;
	unsigned int		 failed_tgts_cnt = 0;
	int			 rc = 0;

	D_ALLOC_ARRAY(stripe_ud->asu_recxs,
		      entry->ae_cur_stripe.as_extent_cnt + 1);
	if (stripe_ud->asu_recxs == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	/* Process extent list to find what to re-replicate -- build recx array
	 */
	d_list_for_each_entry_safe(agg_extent, ext_tmp,
				   &entry->ae_cur_stripe.as_dextents,
				   ae_link) {
		if (agg_extent->ae_hole ||
		    agg_extent->ae_epoch < entry->ae_par_extent.ape_epoch)
			continue;
		if (agg_extent->ae_recx.rx_idx - ss > last_ext_end) {
			stripe_ud->asu_recxs[ext_cnt].rx_idx =
				ss + last_ext_end;
			stripe_ud->asu_recxs[ext_cnt].rx_nr =
				agg_extent->ae_recx.rx_idx -
				ss - last_ext_end;
			ext_tot_len +=
			stripe_ud->asu_recxs[ext_cnt++].rx_nr;
		}
		last_ext_end += agg_extent->ae_recx.rx_idx +
				agg_extent->ae_recx.rx_nr - ss;
	}

	if (last_ext_end < k * len) {
		stripe_ud->asu_recxs[ext_cnt].rx_idx =
			ss + last_ext_end;
		stripe_ud->asu_recxs[ext_cnt].rx_nr = ss + k * len -
			last_ext_end;
		ext_tot_len += stripe_ud->asu_recxs[ext_cnt++].rx_nr;
	}
	stripe_ud->asu_cell_cnt = ext_cnt;
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = ext_cnt;
	iod.iod_recxs = stripe_ud->asu_recxs;
	entry->ae_sgl.sg_nr = 1;
	entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_len = ext_cnt * ext_tot_len *
								entry->ae_rsize;
	D_ASSERT(entry->ae_sgl.sg_iovs[AGG_IOV_DATA].iov_len >= k * len);
	/* Pull data via dsc_obj_fetch */
	rc = dsc_obj_fetch(entry->ae_obj_hdl, entry->ae_cur_stripe.as_hi_epoch,
			   &entry->ae_dkey, 1, &iod, &entry->ae_sgl, NULL, 0,
			   NULL);
	if (rc) {
		D_ERROR("dsc_obj_fetch failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/* Invoke peer re-replicate */
	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = pool_map_find_failed_tgts(agg_param->ap_pool_info.api_pool->sp_map,
				       &targets, &failed_tgts_cnt);
	if (rc) {
		D_ERROR("pool_map_find_failed_tgts failed: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	if (targets != NULL) {
		int i;

		for (i = 0; i < failed_tgts_cnt; i++) {
			if (targets[i].ta_comp.co_rank ==
			    entry->ae_peer_pshards[0].sd_rank) {
				D_ERROR("peer parity tgt failed\n");
				rc = -1;
				goto out;
			}
		}
	}

	tgt_ep.ep_rank = entry->ae_peer_pshards[0].sd_rank;
	tgt_ep.ep_tag = entry->ae_peer_pshards[0].sd_tgt_idx;
	rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
			    DAOS_OBJ_RPC_EC_REPLICATE, &rpc);
	if (rc) {
		D_ERROR("obj_req_create failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	ec_rep_in = crt_req_get(rpc);
	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
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
	ec_rep_in->er_iod = iod;
	ec_rep_in->er_stripenum = entry->ae_cur_stripe.as_stripenum;
	ec_rep_in->er_epoch = entry->ae_cur_stripe.as_hi_epoch;
	ec_rep_in->er_map_ver =
		agg_param->ap_pool_info.api_pool->sp_map_version;
	entry->ae_sgl.sg_nr_out = 1;
	rc = crt_bulk_create(dss_get_module_info()->dmi_ctx, &entry->ae_sgl,
			     CRT_BULK_RW, &ec_rep_in->er_bulk);
	if (rc) {
		D_ERROR("crt_bulk_create returned: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	rc = dss_rpc_send(rpc);
	if (rc) {
		D_ERROR("dss_rpc_send failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	ec_rep_out = crt_reply_get(rpc);
	rc = ec_rep_out->er_status;
	if (rc)
		D_ERROR("remote update rpc failed: "DF_RC"\n", DP_RC(rc));

out:
	D_FREE(stripe_ud->asu_recxs);
	entry->ae_sgl.sg_nr = AGG_IOV_CNT;
	ABT_eventual_set(stripe_ud->asu_eventual, (void *)&rc, sizeof(rc));
}

static int
agg_process_holes(struct ec_agg_entry *entry)
{
	struct ec_agg_stripe_ud	 stripe_ud = { 0 };
	daos_iod_t		 iod = { 0 };
	daos_recx_t		 recx = { 0 };
	daos_epoch_range_t	 epoch_range = { 0 };
	struct ec_agg_param	*agg_param;
	int			 tid, rc = 0;
	int			*status;

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
			    DSS_ULT_EC, tid, 0, NULL);
	if (rc)
		goto ev_out;
	rc = ABT_eventual_wait(stripe_ud.asu_eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto ev_out;
	}
	if (*status != 0)
		rc = *status;
	/* Update local vos with replicate */
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = stripe_ud.asu_cell_cnt;
	iod.iod_recxs = stripe_ud.asu_recxs;
	entry->ae_sgl.sg_nr = 1;
	/* write the reps to vos */
	agg_param = container_of(entry, struct ec_agg_param, ap_agg_entry);
	rc = vos_obj_update(agg_param->ap_cont_handle, entry->ae_oid,
			    entry->ae_cur_stripe.as_hi_epoch, 0, 0,
			    &entry->ae_dkey, 1, &iod, NULL,
			    &entry->ae_sgl);
	if (rc) {
		D_ERROR("vos_update_begin failed: "DF_RC"\n", DP_RC(rc));
		goto ev_out;
	}
	/* Delete parity */
	epoch_range.epr_lo = agg_param->ap_epr.epr_lo;
	epoch_range.epr_hi = entry->ae_cur_stripe.as_hi_epoch;
	recx.rx_idx = (entry->ae_cur_stripe.as_stripenum *
		       entry->ae_oca->u.ec.e_len) | PARITY_INDICATOR;
	recx.rx_nr = entry->ae_oca->u.ec.e_len;
	rc = vos_obj_array_remove(agg_param->ap_cont_handle, entry->ae_oid,
				  &epoch_range, &entry->ae_dkey,
				  &entry->ae_akey, &recx);
ev_out:
	entry->ae_sgl.sg_nr = AGG_IOV_CNT;
	ABT_eventual_free(&stripe_ud.asu_eventual);

out:
	D_FREE(stripe_ud.asu_recxs);
	return rc;
}

/* Process the prior stripe. Invoked when the iterator has moved to the first
 * extent in the subsequent.
 */
static int
agg_process_stripe(struct ec_agg_entry *entry)
{
	vos_iter_param_t	iter_param = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	bool			update_vos = true;
	bool			write_parity = true;
	bool			process_holes = false;
	int			rc = 0;

	entry->ae_par_extent.ape_epoch	= ~(0ULL);

	iter_param.ip_hdl		= DAOS_HDL_INVAL;
	iter_param.ip_ih		= entry->ae_thdl;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx	= PARITY_INDICATOR |
					  (entry->ae_cur_stripe.as_stripenum *
						entry->ae_oca->u.ec.e_len);
	iter_param.ip_recx.rx_nr	= entry->ae_oca->u.ec.e_len;

	D_DEBUG(DB_TRACE, "Querying parity for stripe: %lu, offset: %lu\n",
		entry->ae_cur_stripe.as_stripenum,
		iter_param.ip_recx.rx_idx);

	/* Query the parity */
	rc = vos_iterate(&iter_param, VOS_ITER_RECX, false, &anchors,
			 agg_recx_iter_pre_cb, NULL, entry, NULL);
	/* entry->ae_par_extent.ape_epoch has been set to the parity extent's
	 * epoch
	 */
	if (rc != 0)
		goto out;

	if (entry->ae_par_extent.ape_epoch >= entry->ae_cur_stripe.as_hi_epoch
	    && entry->ae_par_extent.ape_epoch != ~(0ULL)) {
		/* Parity is as new, or newer than data, so delete the replicas.
		 */
		update_vos = true;
		write_parity = false;
		goto out;
	}

	if ((entry->ae_par_extent.ape_epoch == ~(0ULL) &&
	     agg_stripe_is_filled(entry, false)) ||
	    agg_stripe_is_filled(entry, true)) {
		/* Replicas constitute a full stripe, all extents are new than
		 * parity, if parity exists for the stripe
		 */
		rc = agg_encode_local_parity(entry);
		goto out;
	}

	if (entry->ae_par_extent.ape_epoch == ~(0ULL)) {
		/* No parity, partial-stripe worth of replica, nothing to do */
		update_vos = false;
		goto out;
	}
	/* Parity, some later replicas, possibly holes, not full stripe. */
	if (entry->ae_cur_stripe.as_has_holes)
		process_holes = true;
	else
		rc = agg_process_partial_stripe(entry);

out:
	if (process_holes && rc == 0)
		rc = agg_process_holes(entry);
	else if (update_vos && rc == 0) {
		if (rc == 0 && entry->ae_oca->u.ec.e_p > 1)  {
			/* offload of ds_obj_update to push remote parity */
			rc = agg_peer_update(entry, write_parity);
			if (rc)
				D_ERROR("agg_peer_update fail: "DF_RC"\n",
					DP_RC(rc));
		}

		if (rc == 0) {
			rc = agg_update_vos(entry, write_parity);
			if (rc)
				D_ERROR("agg_update_vos failed: "DF_RC"\n",
					DP_RC(rc));
		}
	}
	if (rc == 0 && entry->ae_cur_stripe.as_ho_ext_cnt) {
		if (entry->ae_oca->u.ec.e_p > 1 && !update_vos)  {
			/* No VOS update, but holdovers.
			 * Didn't call peer update,
			 * so send holdover list to peer for
			 * removal
			 */
			rc = agg_peer_update(entry, false);
			if (rc)
				D_ERROR("agg_peer_update failed: "DF_RC"\n",
					DP_RC(rc));
		}
		if (rc == 0) {
			rc = agg_remove_holdovers(entry);
			if (rc)
				D_ERROR("remove_holdovers failed: "DF_RC"\n",
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
	unsigned int		len = entry->ae_oca->u.ec.e_len;
	unsigned int		k = entry->ae_oca->u.ec.e_k;
	daos_off_t		stripe = recx->rx_idx / (len * k);
	daos_off_t		stripe_end = (stripe + 1) * len * k;

	if (recx->rx_idx + recx->rx_nr > stripe_end)
		return stripe_end - recx->rx_idx;
	else
		return recx->rx_nr;
}

/* Iterator call back sub-function for handling data extents.
 */
static int
agg_data_extent(vos_iter_entry_t *entry, struct ec_agg_entry *agg_entry,
		unsigned int *acts)
{
	struct ec_agg_extent	*extent = NULL;
	daos_off_t		 cur_stripenum, this_stripenum;
	int			 rc = 0;

	D_ASSERT(!(entry->ie_recx.rx_idx & PARITY_INDICATOR));

	this_stripenum = agg_stripenum(agg_entry, entry->ie_recx.rx_idx);

	if (this_stripenum != agg_entry->ae_cur_stripe.as_stripenum) {
		/* Iterator has reached next stripe */
		if (agg_entry->ae_cur_stripe.as_extent_cnt) {
			cur_stripenum = agg_entry->ae_cur_stripe.as_stripenum;
			rc = agg_process_stripe(agg_entry);
			if (rc)
				D_ERROR("Process stripe returned "DF_RC"\n",
					DP_RC(rc));
		/* Error leaves data covered by replicas vulnerable to vos
		 * delete, so don't advance coordination epoch.
		 */
			rc = 0;
			if (cur_stripenum <
			    agg_entry->ae_cur_stripe.as_stripenum &&
			agg_entry->ae_cur_stripe.as_stripenum <
			this_stripenum) {
				/* Handle holdover stripe */
				rc = agg_process_stripe(agg_entry);
				if (rc)
					D_ERROR("Holdover returned "DF_RC"\n",
						DP_RC(rc));
				rc = 0;
			}
		}
		agg_entry->ae_cur_stripe.as_stripenum = this_stripenum;
	}

	/* Add the extent to the entry, for the current stripe */
	D_ALLOC_PTR(extent);
	if (extent == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	if (entry->ie_csum.cs_type)
		return 1;

	extent->ae_recx = entry->ie_recx;
	extent->ae_orig_recx = entry->ie_orig_recx;
	extent->ae_epoch = entry->ie_epoch;
	agg_entry->ae_rsize = entry->ie_rsize;

	d_list_add_tail(&extent->ae_link,
			&agg_entry->ae_cur_stripe.as_dextents);

	if (!agg_entry->ae_cur_stripe.as_extent_cnt)
		/* first extent in stripe: save the start offset */
		agg_entry->ae_cur_stripe.as_offset =  extent->ae_recx.rx_idx -
			rounddown(extent->ae_recx.rx_idx,
				  obj_ec_stripe_rec_nr(agg_entry->ae_oca));

	agg_entry->ae_cur_stripe.as_extent_cnt++;
	if (entry->ie_biov.bi_addr.ba_hole) {
		extent->ae_hole = true;
		agg_entry->ae_cur_stripe.as_has_holes = true;
	} else {
		agg_entry->ae_cur_stripe.as_stripe_fill +=
			agg_in_stripe(agg_entry, &entry->ie_recx);
	}

	if (extent->ae_epoch > agg_entry->ae_cur_stripe.as_hi_epoch)
		agg_entry->ae_cur_stripe.as_hi_epoch = extent->ae_epoch;

	D_DEBUG(DB_TRACE, "adding extent %lu,%lu, to stripe  %lu, shard: %u\n",
		extent->ae_recx.rx_idx, extent->ae_recx.rx_nr,
		agg_stripenum(agg_entry, extent->ae_recx.rx_idx),
		agg_entry->ae_oid.id_shard);
out:
	return rc;
}

/* Post iteration call back for akey.
 */
static int
agg_akey_post(daos_handle_t ih, vos_iter_entry_t *entry,
	      struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	daos_off_t	cur_stripenum;
	int		rc = 0;

	if (agg_entry->ae_cur_stripe.as_extent_cnt) {
		cur_stripenum = agg_entry->ae_cur_stripe.as_stripenum;
		rc = agg_process_stripe(agg_entry);
		if (rc)
			D_ERROR("Process stripe returned "DF_RC"\n",
				DP_RC(rc));
		rc = 0;
		if (cur_stripenum < agg_entry->ae_cur_stripe.as_stripenum) {
			/* Handle holdover stripe */
			rc = agg_process_stripe(agg_entry);
			if (rc)
				D_ERROR("Holdover returned "DF_RC"\n",
					DP_RC(rc));
			rc = 0;
		}
		if (agg_entry->ae_cur_stripe.as_ho_ext_cnt) {
			if (agg_entry->ae_oca->u.ec.e_p > 1) {
				rc = agg_peer_update(agg_entry, false);
				if (rc)
					D_ERROR("peer update failed: "DF_RC"\n",
						DP_RC(rc));
			}
			if (rc == 0) {
				rc = agg_remove_holdovers(agg_entry);
				if (rc)
					D_ERROR("holdover failed: "DF_RC"\n",
						DP_RC(rc));
			}
			rc = 0;
		}

		agg_entry->ae_cur_stripe.as_stripenum	= 0UL;
		agg_entry->ae_cur_stripe.as_hi_epoch	= 0UL;
		agg_entry->ae_cur_stripe.as_stripe_fill = 0UL;
		agg_entry->ae_cur_stripe.as_offset	= 0U;
		D_ASSERT(agg_entry->ae_cur_stripe.as_ho_ext_cnt == 0);

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

/* Handles dkeys returned by the per-object nested iteratior.
*/
static int
agg_dkey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	if (agg_key_compare(agg_entry->ae_dkey, entry->ie_key))
		agg_entry->ae_dkey	= entry->ie_key;
	else
		*acts |= VOS_ITER_CB_SKIP;

	return 0;
}

/* Handles akeys returned by the per-object nested iteratior.
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
		memset(&agg_entry->ae_akey, 0, sizeof(agg_entry->ae_akey));
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
		rc = agg_akey_post(ih, entry, agg_entry, acts);
		break;
	case VOS_ITER_RECX:
		break;
	default:
		break;
	}

	agg_param->ap_credits++;

	if (agg_param->ap_credits > agg_param->ap_credits_max) {
		agg_param->ap_credits = 0;
		*acts |= VOS_ITER_CB_YIELD;
		if (ec_aggregate_yield(agg_param)) {
			D_DEBUG(DB_EPC, "EC aggregation aborted\n");
			rc = 1;
		}
	}

	return rc;
}

/* Initializes the struct holding the iteration state (ec_agg_entry).
*/
static void
agg_reset_entry(struct ec_agg_entry *agg_entry,
		vos_iter_entry_t *entry, struct daos_oclass_attr *oca)
{
	agg_entry->ae_oid		= entry->ie_oid;
	agg_entry->ae_oca		= oca;
	agg_entry->ae_codec		= NULL;
	agg_entry->ae_rsize		= 0UL;
	if (!daos_handle_is_inval(agg_entry->ae_obj_hdl)) {
		dsc_obj_close(agg_entry->ae_obj_hdl);
		agg_entry->ae_obj_hdl = DAOS_HDL_INVAL;
	}
	memset(&agg_entry->ae_dkey, 0, sizeof(agg_entry->ae_dkey));
	memset(&agg_entry->ae_akey, 0, sizeof(agg_entry->ae_akey));
	memset(agg_entry->ae_peer_pshards, 0,
	       (OBJ_EC_MAX_P - 1) * sizeof(struct daos_shard_loc));

	agg_entry->ae_cur_stripe.as_stripenum	= 0UL;
	agg_entry->ae_cur_stripe.as_hi_epoch	= 0UL;
	agg_entry->ae_cur_stripe.as_stripe_fill = 0UL;
	agg_entry->ae_cur_stripe.as_extent_cnt	= 0U;
	agg_entry->ae_cur_stripe.as_offset	= 0U;
}

/* Iterator pre-callback for objects. Determines if object is subject
 * to aggregation. Skips objects that are not EC, or not led by
 * this target.
 */
static int
agg_object(daos_handle_t ih, vos_iter_entry_t *entry,
	   struct ec_agg_param *agg_param, unsigned int *acts)
{
	struct daos_oclass_attr *oca;
	int			 rc = 0;

	if (!daos_unit_oid_compare(agg_param->ap_agg_entry.ae_oid,
				   entry->ie_oid)) {
		*acts |= VOS_ITER_CB_SKIP;
		goto out;
	}

	if (!daos_oclass_is_ec(entry->ie_oid.id_pub, &oca)) {
		*acts |= VOS_ITER_CB_SKIP;
		goto out;
	}

	rc = ds_pool_check_dtx_leader(agg_param->ap_pool_info.api_pool,
				      &entry->ie_oid, agg_param->
				      ap_pool_info.api_pool->sp_map_version);

	if (rc == 1 && entry->ie_oid.id_shard >= oca->u.ec.e_k) {
		agg_reset_entry(&agg_param->ap_agg_entry, entry, oca);
		rc = 0;
		goto out;
	} else {
		if (rc < 0) {
			D_ERROR("ds_pool_check_leader failed "DF_RC"\n",
				DP_RC(rc));
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
		rc = agg_data_extent(entry, agg_entry, acts);
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

	return rc;
}

/* Captures the IV values need for pool and container open. Runs in
 * system xstream.
 */
static void
agg_iv_ult(void *arg)
{
	struct ec_agg_param	*agg_param = (struct ec_agg_param *)arg;
	struct daos_prop_entry	*entry = NULL;
	int			 rc = 0;

	rc = ds_pool_iv_srv_hdl_fetch(agg_param->ap_pool_info.api_pool,
				      &agg_param->ap_pool_info.api_poh_uuid,
				      &agg_param->ap_pool_info.api_coh_uuid);
	if (rc) {
		D_ERROR("ds_pool_iv_srv_hdl_fetch failed: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	D_ALLOC_PTR(agg_param->ap_prop);
	if (agg_param->ap_prop == NULL) {
		D_ERROR("Property allocation failed\n");
		rc = -DER_NOMEM;
		goto out;
	}

	rc = ds_pool_iv_prop_fetch(agg_param->ap_pool_info.api_pool,
				   agg_param->ap_prop);
	if (rc) {
		D_ERROR("ds_pool_iv_prop_fetch failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	entry = daos_prop_entry_get(agg_param->ap_prop, DAOS_PROP_PO_SVC_LIST);
	D_ASSERT(entry != NULL);
	agg_param->ap_pool_info.api_svc_list =
		(d_rank_list_t *)entry->dpe_val_ptr;

out:
	ABT_eventual_set(agg_param->ap_pool_info.api_eventual,
			 (void *)&rc, sizeof(rc));
}

/* Iterates entire VOS. Invokes nested iterator to recurse through trees
 * for all objects meeting the criteria: object is EC, and this target is
 * leader.
 */
int
ds_obj_ec_aggregate(struct ds_cont_child *cont, daos_epoch_range_t *epr,
		    bool (*yield_func)(void *arg), void *yield_arg)
{
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_iter_anchors  anchors = { 0 };
	struct ec_agg_param	 agg_param = { 0 };
	daos_handle_t		 ph = DAOS_HDL_INVAL;
	int			*status;
	int			 rc = 0;

	uuid_copy(agg_param.ap_pool_info.api_pool_uuid,
		  cont->sc_pool->spc_uuid);
	uuid_copy(agg_param.ap_pool_info.api_cont_uuid, cont->sc_uuid);
	agg_param.ap_pool_info.api_pool = cont->sc_pool->spc_pool;
	agg_param.ap_cont_handle	= cont->sc_hdl;
	agg_param.ap_yield_func		= yield_func;
	agg_param.ap_yield_arg		= yield_arg;
	agg_param.ap_credits_max	= EC_AGG_ITERATION_MAX;

	rc = ABT_eventual_create(sizeof(*status),
				 &agg_param.ap_pool_info.api_eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = dss_ult_create(agg_iv_ult, &agg_param, DSS_ULT_POOL_SRV, 0, 0,
			    NULL);
	if (rc)
		goto out;
	rc = ABT_eventual_wait(agg_param.ap_pool_info.api_eventual,
			       (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}
	if (*status != 0) {
		rc = *status;
		goto out;
	}

	rc = dsc_pool_open(agg_param.ap_pool_info.api_pool_uuid,
			   agg_param.ap_pool_info.api_poh_uuid, DAOS_PC_RW,
			   NULL, agg_param.ap_pool_info.api_pool->sp_map,
			   agg_param.ap_pool_info.api_svc_list, &ph);
	if (rc) {
		D_ERROR("dsc_pool_open failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = dsc_cont_open(ph, agg_param.ap_pool_info.api_cont_uuid,
			   agg_param.ap_pool_info.api_coh_uuid, DAOS_COO_RW,
			   &agg_param.ap_pool_info.api_cont_hdl);
	if (rc) {
		D_ERROR("dsc_cont_open failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	D_INIT_LIST_HEAD(&agg_param.ap_agg_entry.ae_cur_stripe.as_dextents);
	D_INIT_LIST_HEAD(&agg_param.ap_agg_entry.ae_cur_stripe.as_hoextents);

	iter_param.ip_hdl		= cont->sc_hdl;
	iter_param.ip_epr.epr_lo	= epr->epr_lo;
	iter_param.ip_epr.epr_hi	= epr->epr_hi;
	iter_param.ip_epc_expr		= VOS_IT_EPC_RR;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx	= 0ULL;
	iter_param.ip_recx.rx_nr	= ~PARITY_INDICATOR;

	rc = vos_iterate(&iter_param, VOS_ITER_OBJ, true, &anchors,
			 agg_iterate_pre_cb, agg_iterate_post_cb,
			 &agg_param, NULL);

	dsc_cont_close(ph, agg_param.ap_pool_info.api_cont_hdl);
out:
	daos_prop_free(agg_param.ap_prop);
	ABT_eventual_free(&agg_param.ap_pool_info.api_eventual);
	agg_sgl_fini(&agg_param.ap_agg_entry.ae_sgl);
	dsc_pool_close(ph);
	return rc;

}
