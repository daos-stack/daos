/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
ds_csum_add2iod(daos_iod_t *iod, struct daos_csummer *csummer,
		struct bio_sglist *bsgl, struct dcs_csum_info *biov_csums,
		size_t *biov_csums_used, struct dcs_iod_csums *iod_csums);

/** handler for scrubber. will get called after each checksum calculated */
typedef int (*ds_progress_handler_t)();
/** handler for when corruption is discovered */
typedef int (*ds_corruption_handler_t)();

/**
 * Iterate over all objects in a container and verify the data stored is not
 * corrupt by calculating new checksums and comparing to stored checksum
 *
 * @param coh				container handle
 * @param csummer			daos_csummer for calculating checksums
 * @param progress_handler		will get called after each checksum
 *					is calculated
 * @param corruption_handler		will get called when corruption is
 *					discovered
 *
 * @return				0 on success, else error code
 */
int
ds_obj_csum_scrub(daos_handle_t coh, struct daos_csummer *csummer,
		  ds_progress_handler_t progress_handler,
		  ds_corruption_handler_t corruption_handler);

#endif
