/**
 * (C) Copyright 2019 Intel Corporation.
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

#ifndef __DAOS_VOS_IO_CHECKSUM_H__
#define __DAOS_VOS_IO_CHECKSUM_H__

#include "evt_priv.h"

/**
 * Determine if the saved checksum for a chunk can be used, or if a
 * new checksum is required.
 *
 * @param biov			Source data
 * @param biov_bytes_used	How many bytes have already been used for
 *				previous chunk considerations
 * @param chunk_bytes		Number of bytes needed for chunk. Won't always
 *				be chunksize because request might not be chunk
 *				aligned, or a previous \biov might have
 *				contributed to the current chunk.
 * @param biov_byte_start	Record aligned byte the biov starts on.
 * @param next_biov		Next biov in the bsgl. NULL if \biov is the last
 * @param csum_started		whether or not a previous biov for the current
 *				chunk exists and started a new checksum that
 *				\biov should contribute to
 * @param chunksize		configured chunksize
 *
 * @return			true if a checksum needs to be calculated
 *				false if can use stored checksum
 */
bool
vic_needs_new_csum(struct bio_iov *biov, daos_off_t biov_bytes_used,
		   uint32_t chunk_bytes, size_t biov_byte_start,
		   struct bio_iov *next_biov, bool csum_started,
		   uint32_t chunksize);

/**
 * will process the bsgl and create new checksums or use the stored
 * checksums for the bsgl as needed and appropriate. The result is the iod will
 * have checksums appropriate for the extents and data they represent
 */
int
vic_fetch_iod(daos_iod_t *iod, struct daos_csummer *csummer,
	      struct bio_sglist *bsgl, daos_csum_buf_t *biov_dcbs,
	      size_t *biov_dcbs_used);

/**
 * If checksums are enabled, then more data might be required than the request
 * so that appropriate chunk aligned data is verified when necessary.
 */
void
vic_update_biov(struct bio_iov *biov, struct evt_entry *ent,
		daos_size_t rsize, daos_csum_buf_t *dcbs,
		uint32_t *dcb_count);

#endif /* __DAOS_VOS_IO_CHECKSUM_H__ */
