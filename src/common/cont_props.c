/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/cont_props.h>
#include <daos/common.h>

void
daos_props_2cont_props(daos_prop_t *props, struct cont_props *cont_prop)
{
	if (props == NULL || cont_prop == NULL) {
		D_DEBUG(DB_TRACE, "No props to set, props=%p, cont_prop=%p\n",
			props, cont_prop);
		return;
	}

	/* input props should cover needed entries, see ds_get_cont_props() */
	if (daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP_THRESHOLD) == NULL ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_SERVER_VERIFY)
	    == NULL							     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_CSUM) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_CHUNK_SIZE) == NULL ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_COMPRESS) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_ENCRYPT) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_REDUN_FAC) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_ALLOCED_OID) == NULL     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_EC_CELL_SZ) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_EC_PDA) == NULL	     ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_GLOBAL_VERSION) == NULL  ||
	    daos_prop_entry_get(props, DAOS_PROP_CO_RP_PDA) == NULL)
		D_DEBUG(DB_TRACE, "some prop entry type not found, "
			"use default value.\n");

	/** deduplication */
	cont_prop->dcp_dedup_enabled	= daos_cont_prop2dedup(props);
	cont_prop->dcp_dedup_size	= daos_cont_prop2dedupsize(props);
	cont_prop->dcp_dedup_verify	= daos_cont_prop2dedupverify(props);

	/** checksum */
	cont_prop->dcp_srv_verify	= daos_cont_prop2serververify(props);
	cont_prop->dcp_csum_type	= daos_cont_prop2csum(props);
	cont_prop->dcp_csum_enabled =
		daos_cont_csum_prop_is_enabled(cont_prop->dcp_csum_type);
	cont_prop->dcp_chunksize	= daos_cont_prop2chunksize(props);

	/** compression */
	cont_prop->dcp_compress_type	= daos_cont_prop2compress(props);
	cont_prop->dcp_compress_enabled	=
	       daos_cont_compress_prop_is_enabled(cont_prop->dcp_compress_type);

	/** encryption */
	cont_prop->dcp_encrypt_type	= daos_cont_prop2encrypt(props);
	cont_prop->dcp_encrypt_enabled	=
		daos_cont_encrypt_prop_is_enabled(cont_prop->dcp_encrypt_type);

	/** redundancy */
	cont_prop->dcp_redun_fac	= daos_cont_prop2redunfac(props);
	/** EC cell size */
	cont_prop->dcp_ec_cell_sz	= daos_cont_prop2ec_cell_sz(props);

	/** alloc'ed oid */
	cont_prop->dcp_alloced_oid	= daos_cont_prop2allocedoid(props);

	/** performance domain affinity level */
	cont_prop->dcp_ec_pda		= daos_cont_prop2ec_pda(props);
	cont_prop->dcp_rp_pda		= daos_cont_prop2rp_pda(props);

	/** global version */
	cont_prop->dcp_global_version   = daos_cont_prop2global_version(props);
}

uint16_t
daos_cont_prop2csum(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_CSUM);

	return prop == NULL ? DAOS_PROP_CO_CSUM_OFF : prop->dpe_val;
}

uint32_t
daos_cont_prop2chunksize(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_CHUNK_SIZE);

	return prop == NULL ? 0 : prop->dpe_val;
}

bool
daos_cont_prop2serververify(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_SERVER_VERIFY);

	return prop == NULL ? false : prop->dpe_val == DAOS_PROP_CO_CSUM_SV_ON;
}

bool
daos_cont_csum_prop_is_valid(uint16_t val)
{
	if (daos_cont_csum_prop_is_enabled(val) || val == DAOS_PROP_CO_CSUM_OFF)
		return true;
	return false;
}

bool
daos_cont_csum_prop_is_enabled(uint16_t val)
{
	if (val != DAOS_PROP_CO_CSUM_CRC16 &&
	    val != DAOS_PROP_CO_CSUM_CRC32 &&
	    val != DAOS_PROP_CO_CSUM_ADLER32 &&
	    val != DAOS_PROP_CO_CSUM_CRC64 &&
	    val != DAOS_PROP_CO_CSUM_SHA1 &&
	    val != DAOS_PROP_CO_CSUM_SHA256 &&
	    val != DAOS_PROP_CO_CSUM_SHA512)
		return false;
	return true;
}

bool
daos_cont_prop2dedup(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP);

	return prop == NULL ? false : prop->dpe_val != DAOS_PROP_CO_DEDUP_OFF;
}

bool
daos_cont_prop2dedupverify(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP);

	return prop == NULL ? false
			    : prop->dpe_val == DAOS_PROP_CO_DEDUP_MEMCMP;
}

uint32_t
daos_cont_prop2dedupsize(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP_THRESHOLD);

	return prop == NULL ? 0 : prop->dpe_val;
}

uint64_t
daos_cont_prop2allocedoid(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_ALLOCED_OID);

	return prop == NULL ? 0 : prop->dpe_val;
}

bool
daos_cont_compress_prop_is_enabled(uint16_t val)
{
	if (val != DAOS_PROP_CO_COMPRESS_LZ4 &&
	    val != DAOS_PROP_CO_COMPRESS_DEFLATE &&
	    val != DAOS_PROP_CO_COMPRESS_DEFLATE1 &&
	    val != DAOS_PROP_CO_COMPRESS_DEFLATE2 &&
	    val != DAOS_PROP_CO_COMPRESS_DEFLATE3 &&
	    val != DAOS_PROP_CO_COMPRESS_DEFLATE4)
		return false;
	return true;
}

uint32_t
daos_cont_prop2compress(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_COMPRESS);

	return prop == NULL ? DAOS_PROP_CO_COMPRESS_OFF
			    : (uint32_t)prop->dpe_val;
}

bool
daos_cont_encrypt_prop_is_enabled(uint16_t val)
{
	if (val != DAOS_PROP_CO_ENCRYPT_AES_XTS128 &&
	    val != DAOS_PROP_CO_ENCRYPT_AES_XTS256 &&
	    val != DAOS_PROP_CO_ENCRYPT_AES_CBC128 &&
	    val != DAOS_PROP_CO_ENCRYPT_AES_CBC192 &&
	    val != DAOS_PROP_CO_ENCRYPT_AES_CBC256 &&
	    val != DAOS_PROP_CO_ENCRYPT_AES_GCM128 &&
	    val != DAOS_PROP_CO_ENCRYPT_AES_GCM256)
		return false;
	return true;
}

uint16_t
daos_cont_prop2encrypt(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_ENCRYPT);

	return prop == NULL ? false : prop->dpe_val != DAOS_PROP_CO_ENCRYPT_OFF;
}

/** Get the redundancy factor from a containers properites. */
uint32_t
daos_cont_prop2redunfac(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_REDUN_FAC);

	return prop == NULL ? DAOS_PROP_CO_REDUN_RF0 : (uint32_t)prop->dpe_val;
}

/** Get the redundancy level from a containers properites. */
uint32_t
daos_cont_prop2redunlvl(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_REDUN_LVL);

	return prop == NULL ? DAOS_PROP_CO_REDUN_RANK : (uint32_t)prop->dpe_val;
}

/** Get the EC cell size from a containers properites. */
uint32_t
daos_cont_prop2ec_cell_sz(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_EC_CELL_SZ);

	return prop == NULL ? 0 : (uint32_t)prop->dpe_val;
}

/** Get ec pda from a container properties */
uint32_t
daos_cont_prop2ec_pda(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_EC_PDA);

	return prop == NULL ? DAOS_PROP_PO_EC_PDA_DEFAULT :
			      (uint32_t)prop->dpe_val;
}

/** Get rp pda from a container properties */
uint32_t
daos_cont_prop2rp_pda(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_RP_PDA);

	return prop == NULL ? DAOS_PROP_PO_RP_PDA_DEFAULT :
			      (uint32_t)prop->dpe_val;
}

uint32_t
daos_cont_prop2global_version(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_GLOBAL_VERSION);

	return prop == NULL ? 0 : (uint32_t)prop->dpe_val;
}

/** Convert the redun_fac to number of allowed failures */
int
daos_cont_rf2allowedfailures(int rf)
{
	int	rc;

	switch (rf) {
	case DAOS_PROP_CO_REDUN_RF0:
		rc = 0;
		break;
	case DAOS_PROP_CO_REDUN_RF1:
		rc = 1;
		break;
	case DAOS_PROP_CO_REDUN_RF2:
		rc = 2;
		break;
	case DAOS_PROP_CO_REDUN_RF3:
		rc = 3;
		break;
	case DAOS_PROP_CO_REDUN_RF4:
		rc = 4;
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

	return rc;
}
