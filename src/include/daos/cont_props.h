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

#ifndef __DAOS_M_CONT_PROPS_H
#define __DAOS_M_CONT_PROPS_H


#include <daos_prop.h>

struct cont_props {
	uint64_t	 dcp_chunksize;
	uint32_t	 dcp_dedup_size;
	uint32_t	 dcp_csum_type;
	bool		 dcp_csum_enabled;
	bool		 dcp_srv_verify;
	bool		 dcp_dedup;
	bool		 dcp_dedup_verify;
};

void
daos_props_2cont_props(daos_prop_t *props, struct cont_props *cont_prop);

/**
 * Checksum Properties
 */
uint32_t
daos_cont_prop2csum(daos_prop_t *props);

uint64_t
daos_cont_prop2chunksize(daos_prop_t *props);

bool
daos_cont_prop2serververify(daos_prop_t *props);

bool
daos_cont_csum_prop_is_valid(uint16_t val);

bool
daos_cont_csum_prop_is_enabled(uint16_t val);

/**
 * Dedup Properties
 */
bool
daos_cont_prop2dedup(daos_prop_t *props);

bool
daos_cont_prop2dedupverify(daos_prop_t *props);

uint64_t
daos_cont_prop2dedupsize(daos_prop_t *props);

#endif /** __DAOS_M_CONT_PROPS_H__ */
