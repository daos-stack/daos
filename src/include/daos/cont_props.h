/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CONT_PROPS_H
#define __DAOS_CONT_PROPS_H

#include <daos_prop.h>

/** DAOS container property entry names used to set properties using the daos tool */
#define DAOS_PROP_ENTRY_LABEL		"label"
#define DAOS_PROP_ENTRY_CKSUM		"cksum"
#define DAOS_PROP_ENTRY_CKSUM_SIZE	"cksum_size"
#define DAOS_PROP_ENTRY_SRV_CKSUM	"srv_cksum"
#define DAOS_PROP_ENTRY_DEDUP		"dedup"
#define DAOS_PROP_ENTRY_DEDUP_THRESHOLD	"dedup_threshold"
#define DAOS_PROP_ENTRY_COMPRESS	"compression"
#define DAOS_PROP_ENTRY_ENCRYPT		"encryption"
#define DAOS_PROP_ENTRY_REDUN_FAC	"rd_fac"
#define DAOS_PROP_ENTRY_STATUS		"status"
#define DAOS_PROP_ENTRY_EC_CELL_SZ	"ec_cell_sz"
#define DAOS_PROP_ENTRY_LAYOUT_TYPE	"layout_type"
#define DAOS_PROP_ENTRY_LAYOUT_VER	"layout_version"
#define DAOS_PROP_ENTRY_REDUN_LVL	"rd_lvl"
#define DAOS_PROP_ENTRY_SNAPSHOT_MAX	"max_snapshot"
#define DAOS_PROP_ENTRY_ALLOCED_OID	"alloc_oid"
#define DAOS_PROP_ENTRY_OWNER		"owner"
#define DAOS_PROP_ENTRY_GROUP		"group"
#define DAOS_PROP_ENTRY_EC_PDA		"ec_pda"
#define DAOS_PROP_ENTRY_RP_PDA		"rp_pda"
#define DAOS_PROP_ENTRY_GLOBAL_VERSION	"global_version"
#define DAOS_PROP_ENTRY_OBJ_VERSION	"obj_version"
#define DAOS_PROP_ENTRY_PERF_DOMAIN	"perf_domain"

/** DAOS deprecated property entry names keeped for backward compatibility */
#define DAOS_PROP_ENTRY_REDUN_FAC_OLD	"rf"
#define DAOS_PROP_ENTRY_REDUN_LVL_OLD	"rf_lvl"

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
	uint32_t	 dcp_redun_lvl;
	uint32_t	 dcp_redun_fac;
	uint32_t	 dcp_ec_cell_sz;
	uint32_t	 dcp_ec_pda;
	uint32_t	 dcp_rp_pda;
	uint32_t	 dcp_perf_domain;
	uint32_t	 dcp_global_version;
	uint32_t	 dcp_obj_version;
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

/**
 * Performance Domain Affinity level properties
 */
uint32_t
daos_cont_prop2ec_pda(daos_prop_t *prop);

uint32_t
daos_cont_prop2rp_pda(daos_prop_t *prop);

uint32_t
daos_cont_prop2perf_domain(daos_prop_t *prop);

/**
 * Global version properties
 */
uint32_t
daos_cont_prop2global_version(daos_prop_t *prop);

/**
 * object layout version properties
 */
uint32_t
daos_cont_prop2obj_version(daos_prop_t *prop);

static inline uint32_t
daos_cont_props2pda(struct cont_props *props, bool is_ec_obj)
{
	return is_ec_obj ? props->dcp_ec_pda : props->dcp_rp_pda;
}

#endif /** __DAOS_CONT_PROPS_H__ */
