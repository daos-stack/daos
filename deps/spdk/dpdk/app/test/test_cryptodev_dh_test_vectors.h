/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Cavium Networks
 */

#ifndef TEST_CRYPTODEV_DH_TEST_VECTORS_H_
#define TEST_CRYPTODEV_DH_TEST_VECTORS_H_

#include "rte_crypto_asym.h"

#define TEST_DATA_SIZE 4096
#define TEST_DH_MOD_LEN 1024


struct dh_test_param {
	rte_crypto_param priv_key;
};

uint8_t dh_priv[] = {
	0x46, 0x3c, 0x7b, 0x43, 0xd1, 0xb8, 0xd4, 0x7a,
	0x56, 0x28, 0x85, 0x79, 0xcc, 0xd8, 0x90, 0x03,
	0x0c, 0x4b, 0xc6, 0xd3, 0x7f, 0xb3, 0x19, 0x84,
	0x8a, 0xc6, 0x0d, 0x24, 0x5e, 0xaa, 0x7e, 0x7a,
	0x73, 0x88, 0xa6, 0x47, 0x7c, 0x42, 0x78, 0x63,
	0x11, 0x12, 0xd3, 0xa0, 0xc5, 0xfe, 0xfd, 0xf2,
	0x9e, 0x17, 0x90, 0xe5, 0x6d, 0xcc, 0x20, 0x6f,
	0xe8, 0x82, 0x28, 0xbf, 0x5c, 0xe6, 0xd4, 0x86,
	0x5c, 0x35, 0x32, 0x97, 0xc2, 0x86, 0x1b, 0xc5,
	0x59, 0x1c, 0x0b, 0x1b, 0xec, 0x60, 0x3c, 0x1d,
	0x8d, 0x7f, 0xf0, 0xc7, 0x48, 0x3a, 0x51, 0x09,
	0xf2, 0x3e, 0x9e, 0x35, 0x74, 0x98, 0x4d, 0xad,
	0x39, 0xa7, 0xf2, 0xd2, 0xb4, 0x32, 0xd3, 0xc8,
	0xe9, 0x45, 0xbe, 0x56, 0xe7, 0x87, 0xe0, 0xa0,
	0x97, 0x6b, 0x5f, 0x99, 0x5e, 0x41, 0x59, 0x33,
	0x95, 0x64, 0x0d, 0xe9, 0x58, 0x5b, 0xa6, 0x38
};

uint8_t dh_p[] = {
	0xef, 0xee, 0x8c, 0x8b, 0x3f, 0x85, 0x95, 0xcd,
	0x4d, 0x68, 0x5d, 0x4a, 0x5d, 0x1f, 0x2a, 0x2e,
	0xdd, 0xcf, 0xef, 0x1b, 0x3b, 0xe9, 0x7b, 0x0c,
	0x13, 0xee, 0x76, 0xd5, 0x93, 0xca, 0x8b, 0xc8,
	0x0b, 0x97, 0x00, 0xec, 0x1f, 0x34, 0xa2, 0xce,
	0x83, 0x8d, 0x80, 0xea, 0xfe, 0x11, 0xed, 0x28,
	0xdd, 0x32, 0x22, 0x77, 0x96, 0x4e, 0xc5, 0xed,
	0xc8, 0x7a, 0x52, 0x10, 0x22, 0xcc, 0xb2, 0x4d,
	0xd3, 0xda, 0x03, 0xf5, 0x1e, 0xa8, 0x79, 0x23,
	0x8b, 0xe1, 0x78, 0x47, 0x07, 0x5b, 0x26, 0xbb,
	0x53, 0x46, 0x0b, 0x18, 0x5c, 0x07, 0x4e, 0xb6,
	0x76, 0xc9, 0xa8, 0xd5, 0x30, 0xa3, 0xbe, 0x8d,
	0xae, 0xcd, 0x34, 0x68, 0x62, 0x5f, 0xb9, 0x5c,
	0x34, 0x90, 0xf0, 0xda, 0x47, 0x86, 0x36, 0x04,
	0x28, 0xbc, 0x7d, 0xae, 0x9d, 0x4e, 0x61, 0x28,
	0x70, 0xdb, 0xa6, 0x55, 0x04, 0x46, 0x27, 0xe3
};

uint8_t dh_g[] = {0x02};

struct dh_test_param dh_test_params = {
	.priv_key = {
		.data = dh_priv,
		.length = sizeof(dh_priv)
	}
};

struct rte_crypto_asym_xform dh_xform = {
	.next = NULL,
	.xform_type = RTE_CRYPTO_ASYM_XFORM_DH,
	.dh = {
		.p = {
			.data = dh_p,
			.length = sizeof(dh_p)
		},
		.g = {
			.data = dh_g,
			.length = sizeof(dh_g)
		},
	}
};

#endif /* TEST_CRYPTODEV_DH_TEST_VECTORS_H__ */
