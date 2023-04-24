/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS server erasure-coded object IO handling.
 *
 * src/object/srv_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <stddef.h>
#include <stdio.h>
#include <daos/rpc.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "srv_internal.h"

static void
_obj_ec_metrics_process(daos_iod_t *iod, struct obj_io_desc *oiod, struct daos_oclass_attr *oca,
			struct obj_pool_metrics *opm)
{
	struct obj_shard_iod	*siod;
	daos_recx_t		*recx, *recx0;
	uint32_t		 cell_size, nr;
	uint32_t		 i, j;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		if (iod->iod_size == DAOS_REC_ANY)
			return;
		if (iod->iod_size <= OBJ_EC_SINGV_EVENDIST_SZ(obj_ec_data_tgt_nr(oca)))
			d_tm_inc_counter(opm->opm_update_ec_partial, 1);
		else
			d_tm_inc_counter(opm->opm_update_ec_full, 1);

		return;
	}

	/* only when IOD with all full-stripe update, count for opm_update_ec_full.
	 * if the iod with all partial update, or mixed with partial update and full-stripe update
	 * count for opm_update_ec_partial.
	 */
	if (oiod->oiod_nr < obj_ec_tgt_nr(oca)) {
		d_tm_inc_counter(opm->opm_update_ec_partial, 1);
		return;
	}

	cell_size = obj_ec_cell_rec_nr(oca);
	nr = 0;
	for (i = 0; i < obj_ec_tgt_nr(oca); i++) {
		siod = &oiod->oiod_siods[i];
		if (i == 0) {
			nr = siod->siod_nr;
			for (j = 0; j < nr; j++) {
				D_ASSERT(siod->siod_idx + j < iod->iod_nr);
				recx = &iod->iod_recxs[siod->siod_idx + j];
				if (recx->rx_idx % cell_size != 0 ||
				    recx->rx_nr % cell_size != 0) {
					d_tm_inc_counter(opm->opm_update_ec_partial, 1);
					return;
				}
			}
			continue;
		}
		D_ASSERT(nr > 0);
		if (siod->siod_nr != nr) {
			d_tm_inc_counter(opm->opm_update_ec_partial, 1);
			return;
		}
		for (j = 0; j < nr; j++) {
			D_ASSERT(siod->siod_idx + j < iod->iod_nr);
			recx0 = &iod->iod_recxs[j];
			recx = &iod->iod_recxs[siod->siod_idx + j];
			if ((recx->rx_nr != recx0->rx_nr) ||
			    ((recx->rx_idx & (~PARITY_INDICATOR)) !=
			     (recx0->rx_idx & (~PARITY_INDICATOR)))) {
			d_tm_inc_counter(opm->opm_update_ec_partial, 1);
			return;
			}
		}
	}

	d_tm_inc_counter(opm->opm_update_ec_full, 1);
}

void
obj_ec_metrics_process(struct obj_iod_array *iod_array, struct obj_io_context *ioc)
{
	struct obj_pool_metrics *opm;
	int			i;

	D_ASSERT(ioc->ioc_opc == DAOS_OBJ_RPC_UPDATE);
	if (iod_array->oia_iods == NULL || !daos_oclass_is_ec(&ioc->ioc_oca))
		return;

	opm = ioc->ioc_coc->sc_pool->spc_metrics[DAOS_OBJ_MODULE];
	for (i = 0; i < iod_array->oia_oiod_nr; i++) {
		daos_iod_t		*iod;
		struct obj_io_desc	*oiod;

		iod = &iod_array->oia_iods[i];
		oiod = &iod_array->oia_oiods[i];

		_obj_ec_metrics_process(iod, oiod, &ioc->ioc_oca, opm);
	}
}
