/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

	CIPHER_TYPE_AES_XTS128 = 1,
	CIPHER_TYPE_AES_XTS256 = 2,
	CIPHER_TYPE_AES_CBC128 = 3,
	CIPHER_TYPE_AES_CBC192 = 4,
	CIPHER_TYPE_AES_CBC256 = 5,
	CIPHER_TYPE_AES_GCM128 = 6,
	CIPHER_TYPE_AES_GCM256 = 7,

	CIPHER_TYPE_END = 8,
};

/** Lookup the appropriate CIPHER_TYPE given daos container property */
enum DAOS_CIPHER_TYPE
daos_contprop2ciphertype(int contprop_encrypt_val);

struct cipher_ft {
	char                 *cf_name;
	enum DAOS_CIPHER_TYPE cf_type;
};

struct cipher_ft *
daos_cipher_type2algo(enum DAOS_CIPHER_TYPE type);

#endif /** __DAOS_CIPHER_H */
