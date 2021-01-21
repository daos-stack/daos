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

#ifndef __DAOS_CONT_PROPS_H
#define __DAOS_CONT_PROPS_H

#include <daos_prop.h>

struct cont_props {
	uint32_t	 dcp_chunksize;
	uint32_t	 dcp_dedup_size;
	/**
	 * Use more bits for compression type since compression level is
	 * encoded in there.
	 */
	uint32_t	 dcp_compress_type;
	uint16_t	 dcp_csum_type;
	uint16_t	 dcp_encrypt_type;
	uint32_t	 dcp_redun_fac;
	uint32_t	 dcp_csum_enabled:1,
			 dcp_srv_verify:1,
			 dcp_dedup_enabled:1,
			 dcp_dedup_verify:1,
			 dcp_compress_enabled:1,
			 dcp_encrypt_enabled:1;
};

void
daos_props_2cont_props(daos_prop_t *props, struct cont_props *cont_prop);

/**
 * Checksum Properties
 */
uint16_t
daos_cont_prop2csum(daos_prop_t *props);

uint32_t
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

uint32_t
daos_cont_prop2dedupsize(daos_prop_t *props);

/**
 * Compression properties
 */

bool
daos_cont_compress_prop_is_enabled(uint16_t val);

uint32_t
daos_cont_prop2compress(daos_prop_t *props);

/**
 * Encryption properties
 */
bool
daos_cont_encrypt_prop_is_enabled(uint16_t val);

uint16_t
daos_cont_prop2encrypt(daos_prop_t *props);


/**
 * Redundancy properties
 */
uint32_t
daos_cont_prop2redunfac(daos_prop_t *props);

uint32_t
daos_cont_prop2redunlvl(daos_prop_t *props);

int
daos_cont_rf2allowedfailures(int rf);

#endif /** __DAOS_CONT_PROPS_H__ */
