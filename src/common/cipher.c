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
#include <daos/cipher.h>
#include <daos/cont_props.h>

/** ISA-L cipher function table implemented in cipher_isal.c */
extern struct cipher_ft *isal_cipher_algo_table[];

/** Container Property knowledge */
enum DAOS_CIPHER_TYPE
daos_contprop2ciphertype(int contprop_encrypt_val)
{
	switch (contprop_encrypt_val) {
	case DAOS_PROP_CO_ENCRYPT_AES_XTS128:
		return CIPHER_TYPE_AES_XTS128;
	case DAOS_PROP_CO_ENCRYPT_AES_XTS256:
		return CIPHER_TYPE_AES_XTS256;
	case DAOS_PROP_CO_ENCRYPT_AES_CBC128:
		return CIPHER_TYPE_AES_CBC128;
	case DAOS_PROP_CO_ENCRYPT_AES_CBC192:
		return CIPHER_TYPE_AES_CBC192;
	case DAOS_PROP_CO_ENCRYPT_AES_CBC256:
		return CIPHER_TYPE_AES_CBC256;
	case DAOS_PROP_CO_ENCRYPT_AES_GCM128:
		return CIPHER_TYPE_AES_GCM128;
	case DAOS_PROP_CO_ENCRYPT_AES_GCM256:
		return CIPHER_TYPE_AES_GCM256;
	default:
		return CIPHER_TYPE_UNKNOWN;
	}
}

uint32_t
daos_ciphertype2contprop(enum DAOS_CIPHER_TYPE daos_cipher_type)
{
	switch (daos_cipher_type) {
	case CIPHER_TYPE_AES_XTS128:
		return DAOS_PROP_CO_ENCRYPT_AES_XTS128;
	case CIPHER_TYPE_AES_XTS256:
		return DAOS_PROP_CO_ENCRYPT_AES_XTS256;
	case CIPHER_TYPE_AES_CBC128:
		return DAOS_PROP_CO_ENCRYPT_AES_CBC128;
	case CIPHER_TYPE_AES_CBC192:
		return DAOS_PROP_CO_ENCRYPT_AES_CBC192;
	case CIPHER_TYPE_AES_CBC256:
		return DAOS_PROP_CO_ENCRYPT_AES_CBC256;
	case CIPHER_TYPE_AES_GCM128:
		return DAOS_PROP_CO_ENCRYPT_AES_GCM128;
	case CIPHER_TYPE_AES_GCM256:
		return DAOS_PROP_CO_ENCRYPT_AES_GCM256;
	default:
		return CIPHER_TYPE_UNKNOWN;
	}
}

/** ------------------------------------------------------------- */

/**
 * Use ISA-L table by default, this needs to be modified in the future for QAT
 * and other accelerator support.
 */
static struct cipher_ft **algo_table = isal_cipher_algo_table;

struct cipher_ft *
daos_cipher_type2algo(enum DAOS_CIPHER_TYPE type)
{
	struct cipher_ft *result = NULL;

	if (type > CIPHER_TYPE_UNKNOWN && type < CIPHER_TYPE_END) {
		result = algo_table[type - 1];
	}
	if (result && result->cf_type == CIPHER_TYPE_UNKNOWN)
		result->cf_type = type;
	return result;
}

int
daos_str2encryptcontprop(const char *value)
{
	int t;

	for (t = CIPHER_TYPE_UNKNOWN + 1; t < CIPHER_TYPE_END; t++) {
		char *name = algo_table[t - 1]->cf_name;

		if (!strncmp(name, value,
			     min(strlen(name), strlen(value)) + 1)) {
			return daos_ciphertype2contprop(t);
		}
	}

	if (!strncmp(value, "off", min(strlen("off"), strlen(value)) + 1))
		return DAOS_PROP_CO_ENCRYPT_OFF;

	return -DER_INVAL;
}
