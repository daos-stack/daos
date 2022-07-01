/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_IOSRV_CHECKSUM_H__
#define __DAOS_IOSRV_CHECKSUM_H__

#include <daos_srv/bio.h>
#include <daos/checksum.h>

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
ds_csum_add2iod(daos_iod_t *iod, struct daos_csummer *csummer, struct bio_sglist *bsgl,
		struct dcs_ci_list *biov_csums, size_t *biov_csums_used,
		struct dcs_iod_csums *iod_csums);

/**
 * Allocate the memory for and populate the IO Maps structure. This structure is used to identify
 * the parts of the iods' recxes for which there is data and which part are holes.
 *
 * @param biod[in]	contains the extents and info on holes
 * @param iods[in]	IO Descriptor
 * @param iods_nr[in]	Number of iods
 * @param flags[in]	if ORF_CREATE_MAP_DETAIL is set, then all mapped extents are requested,
 *			not just the low and high extents.
 * @param p_maps[out]	pointer to the resulting structures
 *
 * @return		0 on success, else error
 */
int
ds_iom_create(struct bio_desc *biod, daos_iod_t *iods, uint32_t iods_nr, uint32_t flags,
	      daos_iom_t **p_maps);

void
ds_iom_free(daos_iom_t **p_maps, uint64_t map_nr);

#endif
