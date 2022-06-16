/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos_srv/bio.h>
#include "srv_internal.h"

static void
map_add_recx(daos_iom_t *map, const struct bio_iov *biov, uint64_t rec_idx)
{
	map->iom_recxs[map->iom_nr_out].rx_idx = rec_idx;
	map->iom_recxs[map->iom_nr_out].rx_nr = bio_iov2req_len(biov) / map->iom_size;
	map->iom_nr_out++;
}

int
ds_iom_create(struct bio_desc *biod, daos_iod_t *iods, uint32_t iods_nr, uint32_t flags,
		 daos_iom_t **p_maps)
{
	daos_iom_t		*maps;

	daos_iom_t		*map;
	daos_iod_t		*iod;
	struct bio_iov		*biov;
	uint32_t		 i, r;
	uint64_t		 rec_idx;
	uint32_t		 bsgl_iov_idx;
	struct bio_sglist	*bsgl;

	D_ASSERT(p_maps);

	D_ALLOC_ARRAY(maps, iods_nr);
	if (maps == NULL)
		return -DER_NOMEM;

	for (i = 0; i <  iods_nr; i++) {
		bsgl = bio_iod_sgl(biod, i); /** 1 bsgl per iod */
		iod = &iods[i];
		map = &maps[i];
		map->iom_nr = bsgl->bs_nr_out - bio_sgl_holes(bsgl);

		D_ALLOC_ARRAY(map->iom_recxs, map->iom_nr);
		if (map->iom_recxs == NULL) {
			for (r = 0; r < i; r++)
				D_FREE(maps[r].iom_recxs);
			D_FREE(maps);
			return -DER_NOMEM;
		}

		map->iom_size = iod->iod_size;
		map->iom_type = iod->iod_type;

		if (map->iom_type != DAOS_IOD_ARRAY || bsgl->bs_nr_out == 0)
			continue;

		/** start rec_idx at first record of iod.recxs */
		bsgl_iov_idx = 0;
		for (r = 0; r < iod->iod_nr; r++) {
			daos_recx_t recx = iod->iod_recxs[r];

			D_DEBUG(DB_CSUM, "processing recx[%d]: "DF_RECX"\n",
				r, DP_RECX(recx));
			rec_idx = recx.rx_idx;

			while (rec_idx <= recx.rx_idx + recx.rx_nr - 1) {
				biov = bio_sgl_iov(bsgl, bsgl_iov_idx);
				if (biov == NULL) /** reached end of bsgl */
					break;
				if (!bio_addr_is_hole(&biov->bi_addr))
					map_add_recx(map, biov, rec_idx);

				rec_idx += (bio_iov2req_len(biov) /
					    map->iom_size);
				bsgl_iov_idx++;
			}
		}

		daos_iom_sort(map);

		/** allocated and used should be the same */
		D_ASSERTF(map->iom_nr == map->iom_nr_out,
			  "map->iom_nr(%d) == map->iom_nr_out(%d)",
			  map->iom_nr, map->iom_nr_out);
		map->iom_recx_lo = map->iom_recxs[0];
		map->iom_recx_hi = map->iom_recxs[map->iom_nr - 1];
		if (flags & ORF_CREATE_MAP_DETAIL)
			map->iom_flags = DAOS_IOMF_DETAIL;
	}
	*p_maps = maps;

	return 0;
}

void
ds_iom_free(daos_iom_t **p_maps, uint64_t map_nr)
{
	daos_iom_t	*maps = *p_maps;
	int		 i;

	for (i = 0; i < map_nr; i++)
		D_FREE(maps[i].iom_recxs);

	D_FREE(*p_maps);
}
