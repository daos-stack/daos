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

#ifndef __DAOS_MULTIHASH_H
#define __DAOS_MULTIHASH_H

#include <daos_types.h>
#include <daos_obj.h>
#include <daos_prop.h>

/** Convert a string into a property value for csum property */
int
daos_str2csumcontprop(const char *value);

/** Type of checksums DAOS supports. Primarily used for looking up the
 * appropriate algorithm functions to be used for the csummer
 */
enum DAOS_HASH_TYPE {
	HASH_TYPE_UNKNOWN = 0,

	HASH_TYPE_CRC16	= 1,
	HASH_TYPE_CRC32	= 2,
	HASH_TYPE_CRC64	= 3,
	HASH_TYPE_SHA1	= 4,
	HASH_TYPE_SHA256 = 5,
	HASH_TYPE_SHA512 = 6,

	HASH_TYPE_END	= 7,
};

/** Lookup the appropriate HASH_TYPE given daos container property */
enum DAOS_HASH_TYPE daos_contprop2hashtype(int contprop_csum_val);

struct hash_ft {
	int		(*cf_init)(void **daos_mhash_ctx);
	void		(*cf_destroy)(void *daos_mhash_ctx);
	int		(*cf_finish)(void *daos_mhash_ctx, uint8_t *buf,
				     size_t buf_len);
	int		(*cf_update)(void *daos_mhash_ctx, uint8_t *buf,
				     size_t buf_len);
	int		(*cf_reset)(void *daos_mhash_ctx);
	void		(*cf_get)(void *daos_mhash_ctx);
	uint16_t	(*cf_get_size)(void *daos_mhash_ctx);
	bool		(*cf_compare)(void *daos_mhash_ctx,
				      uint8_t *buf1, uint8_t *buf2,
				      size_t buf_len);

	/** Len in bytes. Ft can either statically set csum_len or provide
	 *  a get_len function
	 */
	uint16_t	 cf_hash_len;
	char		*cf_name;
	enum DAOS_HASH_TYPE	cf_type;
};

struct hash_ft *
daos_mhash_type2algo(enum DAOS_HASH_TYPE type);

#endif /** __DAOS_MULTIHASH_H */
