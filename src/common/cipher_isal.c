/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <isa-l.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/cipher.h>

/**
 * ---------------------------------------------------------------------------
 * Algorithms
 * ---------------------------------------------------------------------------
 */

struct cipher_ft aes_xts128_algo = {
	.cf_name = "aes-xts128",
	.cf_type = CIPHER_TYPE_AES_XTS128
};

struct cipher_ft aes_xts256_algo = {
	.cf_name = "aes-xts256",
	.cf_type = CIPHER_TYPE_AES_XTS256
};

struct cipher_ft aes_cbc128_algo = {
	.cf_name = "aes-cbc128",
	.cf_type = CIPHER_TYPE_AES_CBC128
};

struct cipher_ft aes_cbc192_algo = {
	.cf_name = "aes-cbc192",
	.cf_type = CIPHER_TYPE_AES_CBC192
};

struct cipher_ft aes_cbc256_algo = {
	.cf_name = "aes-cbc256",
	.cf_type = CIPHER_TYPE_AES_CBC256
};

struct cipher_ft aes_gcm128_algo = {
	.cf_name = "aes-gcm128",
	.cf_type = CIPHER_TYPE_AES_GCM128
};

struct cipher_ft aes_gcm256_algo = {
	.cf_name = "aes-gcm256",
	.cf_type = CIPHER_TYPE_AES_GCM256
};

/** Index to algo table should align with enum DAOS_CIPHER_TYPE - 1 */
struct cipher_ft *isal_cipher_algo_table[] = {
	&aes_xts128_algo,
	&aes_xts256_algo,
	&aes_cbc128_algo,
	&aes_cbc192_algo,
	&aes_cbc256_algo,
	&aes_gcm128_algo,
	&aes_gcm256_algo,
};
