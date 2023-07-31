/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "cli_csum.h"
#include "obj_internal.h"

int
dc_obj_csum_update(struct daos_csummer *csummer, struct cont_props props, daos_obj_id_t oid,
		   daos_key_t *dkey, daos_iod_t *iods, d_sg_list_t *sgls, const uint32_t iod_nr,
		   struct dcs_layout *layout, struct dcs_csum_info **dkey_csum,
		   struct dcs_iod_csums **iod_csums)
{
	struct daos_csummer	*csummer_copy = NULL;
	int			 rc;

	D_DEBUG(DB_CSUM, DF_C_OID_DKEY " UPDATE - csummer: %p, "
				       "csum_type: %d, csum_enabled: %s\n",
		DP_C_OID_DKEY(oid, dkey),
		csummer, props.dcp_csum_type,
		DP_BOOL(props.dcp_csum_enabled));

	if (!daos_csummer_initialized(csummer)) /** Not configured */
		return 0;

	if (*dkey_csum != NULL) {
		/** already calculated - don't need to do it again */
		return 0;
	}

	/** Used to do actual checksum calculations. This prevents conflicts
	 * between tasks
	 */
	csummer_copy = daos_csummer_copy(csummer);
	if (csummer_copy == NULL)
		return -DER_NOMEM;

	/** Calc 'd' key checksum */
	rc = daos_csummer_calc_key(csummer_copy, dkey, dkey_csum);
	if (rc != 0) {
		daos_csummer_destroy(&csummer_copy);
		return rc;
	}

	/** Calc 'a' key checksum and value checksum */
	rc = daos_csummer_calc_iods(csummer_copy, sgls, iods, NULL,
				    iod_nr, false,
				    layout,
				    -1, iod_csums);
	if (rc != 0) {
		daos_csummer_free_ci(csummer_copy, dkey_csum);
		daos_csummer_destroy(&csummer_copy);
		D_ERROR("daos_csummer_calc_iods error: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	daos_csummer_destroy(&csummer_copy);

	/** fault injection - corrupt data and/or keys after calculating
	 * checksum - simulates corruption over network
	 */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE_DKEY))
		((char *)dkey->iov_buf)[0]++;
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE_AKEY))
		((char *)(*iod_csums)[0].ic_akey.cs_csum)[0]++;
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE))
		dcf_corrupt(sgls, iod_nr);

	return 0;
}

int
dc_obj_csum_fetch(struct daos_csummer *csummer, daos_key_t *dkey, daos_iod_t *iods,
		  d_sg_list_t *sgls, const uint32_t iod_nr, struct dcs_layout *layout,
		  struct dcs_csum_info **dkey_csum, struct dcs_iod_csums **iod_csums)
{
	struct daos_csummer	*csummer_copy;
	int			 rc;

	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_data_verify)
		/* csummer might be initialized by dedup, but checksum
		 * feature is turned off ...
		 */
		return 0;

	if (*dkey_csum != NULL)
		/* already calculated - don't need to do it again */
		return 0;

	/* Used to do actual checksum calculations. This prevents conflicts
	 * between tasks
	 */
	csummer_copy = daos_csummer_copy(csummer);
	if (csummer_copy == NULL)
		return -DER_NOMEM;

	/* dkey */
	rc = daos_csummer_calc_key(csummer_copy, dkey, dkey_csum);
	if (rc != 0) {
		daos_csummer_destroy(&csummer_copy);
		return rc;
	}

	/* akeys (1 for each iod) */
	rc = daos_csummer_calc_iods(csummer_copy, sgls, iods, NULL, iod_nr,
				    true, layout,
				    -1, iod_csums);
	if (rc != 0) {
		D_ERROR("daos_csummer_calc_iods error: "DF_RC"\n", DP_RC(rc));
		daos_csummer_free_ci(csummer_copy, dkey_csum);
		daos_csummer_destroy(&csummer_copy);
		return rc;
	}

	daos_csummer_destroy(&csummer_copy);

	/*
	 * fault injection - corrupt keys after calculating checksum -
	 * simulates corruption over network
	 */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH_DKEY) && (*dkey_csum) != NULL)
		(*dkey_csum)->cs_csum[0]++;
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH_AKEY))
		(*iod_csums)[0].ic_akey.cs_csum[0]++;

	return 0;
}

static struct dcs_layout *
dc_rw_cb_singv_lo_get(daos_iod_t *iods, d_sg_list_t *sgls, uint32_t iod_nr,
		      struct obj_reasb_req *reasb_req)
{
	struct dcs_layout	*singv_lo, *singv_los;
	daos_iod_t		*iod;
	d_sg_list_t		*sgl;
	uint32_t		 i;

	if (reasb_req == NULL)
		return NULL;

	singv_los = reasb_req->orr_singv_los;
	for (i = 0; i < iod_nr; i++) {
		singv_lo = &singv_los[i];
		iod = &iods[i];
		sgl = &sgls[i];
		if (singv_lo->cs_even_dist == 0 || singv_lo->cs_bytes != 0 ||
		    iod->iod_size == DAOS_REC_ANY)
			continue;
		/* the case of fetch singv with unknown rec size, now after the
		 * fetch need to re-calculate the singv_lo again
		 */
		if (obj_ec_singv_one_tgt(iod->iod_size, sgl,
					 reasb_req->orr_oca)) {
			singv_lo->cs_even_dist = 0;
			continue;
		}
		singv_lo->cs_bytes = obj_ec_singv_cell_bytes(iod->iod_size,
							     reasb_req->orr_oca);
	}
	return singv_los;
}

static int
iod_sgl_copy(daos_iod_t *iod, d_sg_list_t *sgl, daos_iod_t *cp_iod,
	     d_sg_list_t *cp_sgl, struct obj_shard_iod *siod,
	     uint64_t off)
{
	struct daos_sgl_idx	sgl_idx = {0};
	int			i, rc;

	cp_iod->iod_recxs = &iod->iod_recxs[siod->siod_idx];
	cp_iod->iod_nr = siod->siod_nr;

	rc = daos_sgl_processor(sgl, false, &sgl_idx, off, NULL, NULL);
	if (rc)
		return rc;

	if (sgl_idx.iov_idx >= sgl->sg_nr) {
		D_ERROR("bad sgl/siod, iov_idx %d, iov_offset "DF_U64
			", offset "DF_U64", tgt_idx %d\n", sgl_idx.iov_idx,
			sgl_idx.iov_offset, off, siod->siod_tgt_idx);
		return -DER_IO;
	}

	cp_sgl->sg_nr = sgl->sg_nr - sgl_idx.iov_idx;
	cp_sgl->sg_nr_out = cp_sgl->sg_nr;
	for (i = 0; i < cp_sgl->sg_nr; i++)
		cp_sgl->sg_iovs[i] = sgl->sg_iovs[sgl_idx.iov_idx + i];
	D_ASSERTF(sgl_idx.iov_offset < cp_sgl->sg_iovs[0].iov_len,
		  "iov_offset "DF_U64", iov_len "DF_U64"\n",
		  sgl_idx.iov_offset, cp_sgl->sg_iovs[0].iov_len);
	cp_sgl->sg_iovs[0].iov_buf += sgl_idx.iov_offset;
	cp_sgl->sg_iovs[0].iov_len -= sgl_idx.iov_offset;
	cp_sgl->sg_iovs[0].iov_buf_len = cp_sgl->sg_iovs[0].iov_len;

	return 0;
}

/**
 * If csums exist and the rw_args has a checksum iov to put checksums into,
 * then serialize the data checksums to the checksum iov. If the iov buffer
 * isn't large enough, then the checksums will be truncated. The iov len will
 * be the length needed. The caller can decide if it wants to grow the iov
 * buffer and call again.
 */
static void
store_csum(d_iov_t *csum_iov,
	   const struct dcs_iod_csums *iod_csum)
{
	int			 c, rc;
	int			 csum_iov_too_small = false;

	if (csum_iov == NULL || iod_csum == NULL)
		return;

	for (c = 0; c < iod_csum->ic_nr; c++) {
		if (!csum_iov_too_small) {
			rc = ci_serialize(&iod_csum->ic_data[c], csum_iov);
			csum_iov_too_small = rc == -DER_REC2BIG;
		}

		if (csum_iov_too_small) {
			D_DEBUG(DB_CSUM, "IOV is too small\n");
			csum_iov->iov_len += ci_size(iod_csum->ic_data[c]);
		}
	}
}

int
dc_rw_cb_csum_verify(struct dc_csum_veriry_args *args)
{
	struct daos_csummer	*csummer_copy = NULL;
	struct dcs_layout	*singv_lo, *singv_los;
	int			 rc = 0;
	int			 i;

	if (!daos_csummer_initialized(args->csummer) || args->csummer->dcs_skip_data_verify)
		return 0;

	D_ASSERTF(args->maps_nr == args->iod_nr, "maps_nr(%lu) == iod_nr(%d)",
		  args->maps_nr, args->iod_nr);

	/** currently don't verify echo classes */
	if ((daos_obj_is_echo(args->oid.id_pub)) || (args->sgls == NULL))
		return 0;

	/** Used to do actual checksum calculations. This prevents conflicts
	 * between tasks
	 */
	csummer_copy = daos_csummer_copy(args->csummer);
	if (csummer_copy == NULL)
		return -DER_NOMEM;

	/** fault injection - corrupt data after getting from server and before
	 * verifying on client - simulates corruption over network
	 */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH)) {
		struct dcs_iod_csums	*tmp_iod_csum;

		/** Got csum successfully from server. Now poison it!! */
		for (i = 0; i < args->iod_nr; i++) {
			tmp_iod_csum = &args->iods_csums[i];
			if (tmp_iod_csum->ic_data != NULL &&
			    tmp_iod_csum->ic_data->cs_csum != NULL) {
				tmp_iod_csum->ic_data->cs_csum[0]++;
				break;
			}
		}
	}

	singv_los = dc_rw_cb_singv_lo_get(args->iods, args->sgls, args->iod_nr, args->reasb_req);

	D_DEBUG(DB_CSUM, DF_C_UOID_DKEY" VERIFY %d iods dkey_hash "DF_U64"\n",
		DP_C_UOID_DKEY(args->oid, args->dkey), args->iod_nr, args->dkey_hash);

	for (i = 0; i < args->iod_nr; i++) {
#define IOV_INLINE	8
		daos_iod_t		*iod = &args->iods[i];
		daos_iod_t		 shard_iod = *iod;
		d_iov_t			 iovs_inline[IOV_INLINE];
		d_iov_t			*iovs_alloc = NULL;
		d_sg_list_t		 shard_sgl = args->sgls[i];
		struct dcs_iod_csums	*iod_csum = &args->iods_csums[i];
		daos_iom_t		*map = &args->maps[i];

		if (!csum_iod_is_supported(iod))
			continue;

		shard_iod.iod_size = args->sizes[i];
		if (iod->iod_type == DAOS_IOD_ARRAY && args->oiods != NULL) {
			if (args->sgls[i].sg_nr <= IOV_INLINE) {
				shard_sgl.sg_iovs = iovs_inline;
			} else {
				D_ALLOC_ARRAY(iovs_alloc, args->sgls[i].sg_nr);
				if (iovs_alloc == NULL) {
					rc = -DER_NOMEM;
					break;
				}
				shard_sgl.sg_iovs = iovs_alloc;
			}
			rc = iod_sgl_copy(iod, &args->sgls[i], &shard_iod, &shard_sgl,
					  args->oiods[i].oiod_siods, args->shard_offs[i]);
			if (rc != 0) {
				D_ERROR("iod_sgl_copy failed (obj: "DF_OID"): "DF_RC"\n",
					DP_OID(args->oid.id_pub), DP_RC(rc));
				D_FREE(iovs_alloc);
				break;
			}
		}

		singv_lo = (singv_los == NULL || iod->iod_type == DAOS_IOD_ARRAY) ?
			   NULL : &singv_los[i];
		if (singv_lo != NULL) {
			/* Single-value csum layout not needed for short single value that only
			 * stored on one data shard.
			 */
			if (obj_ec_singv_one_tgt(iod->iod_size, NULL, args->oc_attr))
				singv_lo = NULL;
			else
				singv_lo->cs_cell_align = 1;
		}
		rc = daos_csummer_verify_iod(csummer_copy, &shard_iod, &shard_sgl, iod_csum,
					     singv_lo, args->shard_idx, map);
		D_FREE(iovs_alloc);
		if (rc != 0) {

			if (iod->iod_type == DAOS_IOD_SINGLE) {
				D_ERROR("Data Verification failed (object: "
					DF_C_UOID_DKEY" shard %d): "DF_RC"\n",
					DP_C_UOID_DKEY(args->oid, args->dkey),
					args->shard_idx,
					DP_RC(rc));
			} else  if (iod->iod_type == DAOS_IOD_ARRAY) {
				D_ERROR("Data Verification failed (object: "
					DF_C_UOID_DKEY" shard %d, extent: "
					DF_RECX"): "
					DF_RC"\n",
					DP_C_UOID_DKEY(args->oid, args->dkey),
					args->shard_idx, DP_RECX(iod->iod_recxs[0]),
					DP_RC(rc));
			}

			break;
		}

		store_csum(args->iov_csum, iod_csum);
	}
	daos_csummer_destroy(&csummer_copy);

	return rc;
}
