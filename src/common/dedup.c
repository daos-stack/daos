/**
 * (C) Copyright 2020 Intel Corporation.
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

#include <daos/checksum.h>
#include <daos/dedup.h>

int
dedup_get_csum_algo(struct cont_props *cont_props)
{
	if (cont_props->dcp_dedup && cont_props->dcp_dedup_verify)
		return DAOS_PROP_CO_CSUM_CRC64;
	if (cont_props->dcp_dedup)
		return  DAOS_PROP_CO_CSUM_SHA256;

	return DAOS_PROP_CO_CSUM_OFF;
}

void
dedup_configure_csummer(struct daos_csummer *csummer,
			struct cont_props *cont_props)
{
	if (!cont_props->dcp_csum_enabled && cont_props->dcp_dedup) {
		csummer->dcs_skip_data_verify = true;
		csummer->dcs_skip_key_calc = true;
		csummer->dcs_skip_key_verify = true;

		if (csummer->dcs_chunk_size == 0)
			csummer->dcs_chunk_size = 32 * 1024;
		else if (csummer->dcs_chunk_size < cont_props->dcp_dedup_size)
			csummer->dcs_chunk_size = cont_props->dcp_dedup_size;
	}
}
