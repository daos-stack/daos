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

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <gurt/types.h>
#include <daos/common.h>
#include <daos/compression.h>
#include <daos/cont_props.h>

/** ISA-L compression function table implemented in compression_isal.c */
extern struct compress_ft *isal_compress_algo_table[];

/** Container Property knowledge */
enum DAOS_COMPRESS_TYPE
daos_contprop2compresstype(int contprop_compress_val)
{
	switch (contprop_compress_val) {
	case DAOS_PROP_CO_COMPRESS_LZ4:
		return COMPRESS_TYPE_LZ4;
	case DAOS_PROP_CO_COMPRESS_GZIP1:
		return COMPRESS_TYPE_GZIP1;
	case DAOS_PROP_CO_COMPRESS_GZIP2:
		return COMPRESS_TYPE_GZIP2;
	case DAOS_PROP_CO_COMPRESS_GZIP3:
		return COMPRESS_TYPE_GZIP3;
	case DAOS_PROP_CO_COMPRESS_GZIP4:
		return COMPRESS_TYPE_GZIP4;
	case DAOS_PROP_CO_COMPRESS_GZIP5:
		return COMPRESS_TYPE_GZIP5;
	case DAOS_PROP_CO_COMPRESS_GZIP6:
		return COMPRESS_TYPE_GZIP6;
	case DAOS_PROP_CO_COMPRESS_GZIP7:
		return COMPRESS_TYPE_GZIP7;
	case DAOS_PROP_CO_COMPRESS_GZIP8:
		return COMPRESS_TYPE_GZIP8;
	case DAOS_PROP_CO_COMPRESS_GZIP9:
		return COMPRESS_TYPE_GZIP9;
	default:
		return COMPRESS_TYPE_UNKNOWN;
	}
}

uint32_t
daos_compresstype2contprop(enum DAOS_COMPRESS_TYPE daos_compress_type)
{
	switch (daos_compress_type) {
	case COMPRESS_TYPE_LZ4:
		return DAOS_PROP_CO_COMPRESS_LZ4;
	case COMPRESS_TYPE_GZIP1:
		return DAOS_PROP_CO_COMPRESS_GZIP1;
	case COMPRESS_TYPE_GZIP2:
		return DAOS_PROP_CO_COMPRESS_GZIP2;
	case COMPRESS_TYPE_GZIP3:
		return DAOS_PROP_CO_COMPRESS_GZIP3;
	case COMPRESS_TYPE_GZIP4:
		return DAOS_PROP_CO_COMPRESS_GZIP4;
	case COMPRESS_TYPE_GZIP5:
		return DAOS_PROP_CO_COMPRESS_GZIP5;
	case COMPRESS_TYPE_GZIP6:
		return DAOS_PROP_CO_COMPRESS_GZIP6;
	case COMPRESS_TYPE_GZIP7:
		return DAOS_PROP_CO_COMPRESS_GZIP7;
	case COMPRESS_TYPE_GZIP8:
		return DAOS_PROP_CO_COMPRESS_GZIP8;
	case COMPRESS_TYPE_GZIP9:
		return DAOS_PROP_CO_COMPRESS_GZIP9;
	default:
		return DAOS_PROP_CO_COMPRESS_OFF;
	}
}

/** ------------------------------------------------------------- */

/**
 * Use ISA-L table by default, this needs to be modified in the future for QAT
 * and other accelerator support.
 */
static struct compress_ft **algo_table = isal_compress_algo_table;

struct compress_ft *
daos_compress_type2algo(enum DAOS_COMPRESS_TYPE type)
{
	struct compress_ft *result = NULL;

	if (type > COMPRESS_TYPE_UNKNOWN && type < COMPRESS_TYPE_END) {
		result = algo_table[type - 1];
	}
	if (result && result->cf_type == COMPRESS_TYPE_UNKNOWN)
		result->cf_type = type;
	return result;
}

int
daos_str2compresscontprop(const char *value)
{
	int t;

	for (t = COMPRESS_TYPE_UNKNOWN + 1; t < COMPRESS_TYPE_END; t++) {
		char *name = algo_table[t - 1]->cf_name;

		if (!strncmp(name, value,
			     min(strlen(name), strlen(value)) + 1)) {
			return daos_compresstype2contprop(t);
		}
	}

	if (!strncmp(value, "off", min(strlen("off"), strlen(value)) + 1))
		return DAOS_PROP_CO_COMPRESS_OFF;

	return -DER_INVAL;
}
