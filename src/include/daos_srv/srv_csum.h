/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_IOSRV_CHECKSUM_H__
#define __DAOS_IOSRV_CHECKSUM_H__

#include <daos_srv/bio.h>
#include <daos/checksum.h>

/**
 * Determine if the saved checksum for a chunk can be used, or if a
 * new checksum is required.
 * @param raw_ext		Range of the raw (actual) extent (should map
 *				to evt_entry.en_ext)
 * @param req_ext		Range of the requested extent (should map
 *				to evt_entry.en_sel_ext)
 * @param chunk			Range of the chunk under investigation
 * @param csum_started		whether or not a previous biov for the current
 *				chunk exists and started a new checksum that
 *				\biov should contribute to
 * @param has_next_biov		Is there another extent following
 * @return
 */
bool
ds_csum_calc_needed(struct daos_csum_range *raw_ext,
		    struct daos_csum_range *req_ext,
		    struct daos_csum_range *chunk,
		    bool csum_started,
		    bool has_next_biov);

/**
 * Process the bsgl and create new checksums or use the stored
 * checksums for the bsgl as needed and appropriate. The result is the iod will
 * have checksums appropriate for the extents and data they represent
 *
 * @param iod[in]			I/O Descriptor that will receive the
 *					csums
 * @param csummer[in]			csummer object for calculating and csum
 *					logic
 * @param bsgl[in]			bio scatter gather list with the data
 * @param biov_csums[in]			list csum info for each \bsgl
 * @param biov_csums_used[in/out]	track the number of csums used
 * @return
 */
int
ds_csum_add2iod(daos_iod_t *iod, struct daos_csummer *csummer,
		struct bio_sglist *bsgl, struct dcs_csum_info *biov_csums,
		size_t *biov_csums_used, struct dcs_iod_csums *iod_csums);

#endif
