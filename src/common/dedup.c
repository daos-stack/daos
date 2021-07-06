/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/checksum.h>
#include <daos/dedup.h>

int
dedup_get_csum_algo(struct cont_props *cont_props)
{
	if (cont_props->dcp_dedup_enabled && cont_props->dcp_dedup_verify)
		return DAOS_PROP_CO_CSUM_CRC64;
	if (cont_props->dcp_dedup_enabled)
		return  DAOS_PROP_CO_CSUM_SHA256;

	return DAOS_PROP_CO_CSUM_OFF;
}

void
dedup_configure_csummer(struct daos_csummer *csummer,
			struct cont_props *cont_props)
{
	if (!cont_props->dcp_csum_enabled && cont_props->dcp_dedup_enabled) {
		csummer->dcs_skip_data_verify = true;
		csummer->dcs_skip_key_calc = true;
		csummer->dcs_skip_key_verify = true;

		if (csummer->dcs_chunk_size == 0)
			csummer->dcs_chunk_size = 32 * 1024;
		else if (csummer->dcs_chunk_size < cont_props->dcp_dedup_size)
			csummer->dcs_chunk_size = cont_props->dcp_dedup_size;
	}
}
