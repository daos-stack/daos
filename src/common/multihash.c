/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <gurt/types.h>
#include <daos/common.h>
#include <daos/multihash.h>
#include <daos/cont_props.h>


/** ISA-L hash function table implemented in multihash_isal.c */
extern struct hash_ft *isal_hash_algo_table[];

/** Container Property knowledge */
enum DAOS_HASH_TYPE
daos_contprop2hashtype(int contprop_csum_val)
{
	switch (contprop_csum_val) {
	case DAOS_PROP_CO_CSUM_CRC16:
		return HASH_TYPE_CRC16;
	case DAOS_PROP_CO_CSUM_CRC32:
		return HASH_TYPE_CRC32;
	case DAOS_PROP_CO_CSUM_ADLER32:
		return HASH_TYPE_ADLER32;
	case DAOS_PROP_CO_CSUM_CRC64:
		return HASH_TYPE_CRC64;
	case DAOS_PROP_CO_CSUM_SHA1:
		return HASH_TYPE_SHA1;
	case DAOS_PROP_CO_CSUM_SHA256:
		return HASH_TYPE_SHA256;
	case DAOS_PROP_CO_CSUM_SHA512:
		return HASH_TYPE_SHA512;
	default:
		return HASH_TYPE_UNKNOWN;
	}
}

static uint32_t
daos_hashtype2contprop(enum DAOS_HASH_TYPE daos_hash_type)
{
	switch (daos_hash_type) {
	case HASH_TYPE_CRC16:
		return DAOS_PROP_CO_CSUM_CRC16;
	case HASH_TYPE_CRC32:
		return DAOS_PROP_CO_CSUM_CRC32;
	case HASH_TYPE_ADLER32:
		return DAOS_PROP_CO_CSUM_ADLER32;
	case HASH_TYPE_CRC64:
		return DAOS_PROP_CO_CSUM_CRC64;
	case HASH_TYPE_SHA1:
		return DAOS_PROP_CO_CSUM_SHA1;
	case HASH_TYPE_SHA256:
		return DAOS_PROP_CO_CSUM_SHA256;
	case HASH_TYPE_SHA512:
		return DAOS_PROP_CO_CSUM_SHA512;
	default:
		return DAOS_PROP_CO_CSUM_OFF;
	}
}


/**
 * Use ISA-L table by default, this needs to be modified in the future for QAT
 * and other accelerator support.
 */
static struct hash_ft **algo_table = isal_hash_algo_table;

struct hash_ft *
daos_mhash_type2algo(enum DAOS_HASH_TYPE type)
{
	struct hash_ft *result = NULL;

	if (type > HASH_TYPE_UNKNOWN && type < HASH_TYPE_END) {
		result = algo_table[type - 1];
	}
	if (result && result->cf_type == HASH_TYPE_UNKNOWN)
		result->cf_type = type;
	return result;
}

int
daos_str2csumcontprop(const char *value)
{
	int t;

	for (t = HASH_TYPE_UNKNOWN + 1; t < HASH_TYPE_END; t++) {
		char *name = algo_table[t - 1]->cf_name;

		if (!strncmp(name, value,
			     min(strlen(name), strlen(value)) + 1)) {
			return daos_hashtype2contprop(t);
		}
	}

	if (!strncmp(value, "off", min(strlen("off"), strlen(value)) + 1))
		return DAOS_PROP_CO_CSUM_OFF;

	return -DER_INVAL;
}
