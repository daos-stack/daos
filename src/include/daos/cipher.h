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

#ifndef __DAOS_CIPHER_H
#define __DAOS_CIPHER_H

#include <daos_prop.h>

/**
 * -----------------------------------------------------------
 * Container Property Knowledge
 * -----------------------------------------------------------
 */

/** Convert a string into a property value for encryption property */
int
daos_str2encryptcontprop(const char *value);

/**
 * -----------------------------------------------------------
 * DAOS Cipherer
 * -----------------------------------------------------------
 */
/**
 * Type of cipher algorithms supported by DAOS.
 * Primarily used for looking up the appropriate algorithm functions to be used
 * for the encryptor.
 */
enum DAOS_CIPHER_TYPE {
	CIPHER_TYPE_UNKNOWN = 0,

	CIPHER_TYPE_AES_XTS128	= 1,
	CIPHER_TYPE_AES_XTS256	= 2,
	CIPHER_TYPE_AES_CBC128	= 3,
	CIPHER_TYPE_AES_CBC192	= 4,
	CIPHER_TYPE_AES_CBC256	= 5,
	CIPHER_TYPE_AES_GCM128	= 6,
	CIPHER_TYPE_AES_GCM256	= 7,

	CIPHER_TYPE_END	= 8,
};

/** Lookup the appropriate CIPHER_TYPE given daos container property */
enum DAOS_CIPHER_TYPE daos_contprop2ciphertype(int contprop_encrypt_val);

struct cipher_ft {
	char		*cf_name;
	enum DAOS_CIPHER_TYPE	cf_type;
};

#endif /** __DAOS_CIPHER_H */
