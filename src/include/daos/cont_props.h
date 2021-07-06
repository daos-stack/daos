/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CONT_PROPS_H
#define __DAOS_CONT_PROPS_H

#include <daos_prop.h>

struct cont_props {
	uint32_t	 dcp_chunksize;
	uint32_t	 dcp_dedup_size;
	uint64_t	 dcp_alloced_oid;
	/**
	 * Use more bits for compression type since compression level is
	 * encoded in there.
	 */
	uint32_t	 dcp_compress_type;
	uint16_t	 dcp_csum_type;
	uint16_t	 dcp_encrypt_type;
	uint32_t	 dcp_redun_fac;
	uint32_t	 dcp_ec_cell_sz;
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

uint32_t
daos_cont_prop2ec_cell_sz(daos_prop_t *props);

/*
 * alloc'ed oid property
 */
uint64_t
daos_cont_prop2allocedoid(daos_prop_t *props);

#endif /** __DAOS_CONT_PROPS_H__ */
