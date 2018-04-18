/**
 * (C) Copyright 2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include "vea_internal.h"

int
verify_free_entry(uint64_t *off, struct vea_free_extent *vfe)
{
	D_ASSERT(vfe != NULL);
	if ((off != NULL && *off != vfe->vfe_blk_off) ||
	    vfe->vfe_blk_off == VEA_HINT_OFF_INVAL) {
		D_CRIT("corrupted free entry, off: "DF_U64" != "DF_U64"\n",
		       *off, vfe->vfe_blk_off);
		return -DER_INVAL;
	}

	if (vfe->vfe_blk_cnt == 0) {
		D_CRIT("corrupted free entry, cnt:, %u\n",
		       vfe->vfe_blk_cnt);
		return -DER_INVAL;
	}

	return 0;
}

int
verify_vec_entry(uint64_t *off, struct vea_ext_vector *vec)
{
	int i;
	uint64_t prev_off = 0;

	D_ASSERT(vec != NULL);
	if (vec->vev_size == 0 || vec->vev_size > VEA_EXT_VECTOR_MAX) {
		D_CRIT("corrupted vector entry, sz: %u\n", vec->vev_size);
		return -DER_INVAL;
	}

	if (off != NULL && *off != vec->vev_blk_off[0]) {
		D_CRIT("corrupted vector entry, off: "DF_U64" != "DF_U64"\n",
		       *off, vec->vev_blk_off[0]);
		return -DER_INVAL;
	}

	for (i = 0; i < vec->vev_size; i++) {
		if (vec->vev_blk_off[i] <= prev_off) {
			D_CRIT("corrupted vector entry[%d],"
			       " "DF_U64" <= "DF_U64"\n",
			       i, vec->vev_blk_off[i], prev_off);
			return -DER_INVAL;
		}
		if (vec->vev_blk_cnt[i] == 0) {
			D_CRIT("corrupted vector entry[%d], %u\n",
			       i, vec->vev_blk_cnt[i]);
			return -DER_INVAL;
		}
	}

	return 0;
}

/**
 * Check if current extent is adjacent with next one.
 * returns	1 - Adjacent
 *		0 - Not adjacent
 *		-DER_INVAL - Overlapping or @cur is behind of @next
 */
int
ext_adjacent(struct vea_free_extent *cur, struct vea_free_extent *next)
{
	uint64_t cur_end = cur->vfe_blk_off + cur->vfe_blk_cnt;

	if (cur_end == next->vfe_blk_off)
		return 1;
	else if (cur_end < next->vfe_blk_off)
		return 0;

	/* Overlapped extents! */
	D_CRIT("corrupted free extents ["DF_U64", %u], ["DF_U64", %u]\n",
	       cur->vfe_blk_off, cur->vfe_blk_cnt,
	       next->vfe_blk_off, next->vfe_blk_cnt);

	return -DER_INVAL;
}

int
verify_resrvd_ext(struct vea_resrvd_ext *resrvd)
{
	if (resrvd->vre_blk_off == VEA_HINT_OFF_INVAL) {
		D_CRIT("invalid blk_off "DF_U64"\n", resrvd->vre_blk_off);
		return -DER_INVAL;
	} else if (resrvd->vre_blk_cnt == 0) {
		D_CRIT("invalid blk_cnt %u\n", resrvd->vre_blk_cnt);
		return -DER_INVAL;
	} else if (resrvd->vre_vector != NULL) {
		/* Vector allocation isn't supported yet. */
		D_CRIT("vector isn't NULL?\n");
		return -DER_NOSYS;
	}

	return 0;
}
