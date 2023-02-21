/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_IOSRV_CHECKSUM_H__
#define __DAOS_IOSRV_CHECKSUM_H__

#include <daos/object.h>
#include <daos_srv/bio.h>
#include <daos_srv/pool.h>
#include <daos/checksum.h>
#include <gurt/telemetry_producer.h>

/**
 * Process the bsgl and create new checksums or use the stored
 * checksums for the bsgl as needed and appropriate. The result is the iod will
 * have checksums appropriate for the extents and data they represent
 *
 * @param[in]		iod		I/O Descriptor that will receive the
 *					csums
 * @param[in]		csummer		csummer object for calculating and csum
 *					logic
 * @param[in]		bsgl		bio scatter gather list with the data
 * @param[in]		biov_csums	list csum info for each \bsgl
 * @param[in/out]	biov_csums_used	track the number of csums used
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
 * @param[in]	biod	contains the extents and info on holes
 * @param[in]	iods	IO Descriptor
 * @param[in]	iods_nr	Number of iods
 * @param[in]	flags	if ORF_CREATE_MAP_DETAIL is set, then all mapped extents are requested,
 *			not just the low and high extents.
 * @param[out]	p_maps	pointer to the resulting structures
 *
 * @return		0 on success, else error
 */
int
ds_iom_create(struct bio_desc *biod, daos_iod_t *iods, uint32_t iods_nr, uint32_t flags,
		 daos_iom_t **p_maps);

void
ds_iom_free(daos_iom_t **p_maps, uint64_t map_nr);


/* For the pool target to start and stop the scrubbing ult */
int
ds_start_scrubbing_ult(struct ds_pool_child *child);
void
ds_stop_scrubbing_ult(struct ds_pool_child *child);

int
ds_csum_verify_keys(struct daos_csummer *csummer, daos_key_t *dkey,
		    struct dcs_csum_info *dkey_csum,
		    daos_iod_t *iods, struct dcs_iod_csums *iod_csums, uint32_t nr,
		    daos_unit_oid_t *uoid);

#endif
