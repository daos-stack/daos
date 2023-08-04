/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/cipher.h>
#include <daos/compression.h>
#include <daos/multihash.h>
#include <daos/dtx.h>
#include <daos_security.h>
#include <daos/cont_props.h>
#include <daos_srv/policy.h>
#include <daos/pool.h>
#include <daos/pool_map.h>

D_CASSERT((int)DAOS_PROP_PERF_DOMAIN_ROOT == (int)PO_COMP_TP_ROOT);
D_CASSERT((int)DAOS_PROP_PERF_DOMAIN_GROUP == (int)PO_COMP_TP_GRP);

daos_prop_t *
daos_prop_alloc(uint32_t entries_nr)
{
	daos_prop_t	*prop;

	if (entries_nr > DAOS_PROP_ENTRIES_MAX_NR) {
		D_ERROR("cannot create daos_prop_t with %d entries(> %d).\n",
			entries_nr, DAOS_PROP_ENTRIES_MAX_NR);
		return NULL;
	}

	D_ALLOC_PTR(prop);
	if (prop == NULL)
		return NULL;

	if (entries_nr > 0) {
		D_ALLOC_ARRAY(prop->dpp_entries, entries_nr);
		if (prop->dpp_entries == NULL) {
			D_FREE(prop);
			return NULL;
		}
	}
	prop->dpp_nr = entries_nr;
	return prop;
}

bool
daos_prop_has_str(struct daos_prop_entry *entry)
{
	switch (entry->dpe_type) {
	case DAOS_PROP_PO_LABEL:
	case DAOS_PROP_CO_LABEL:
	case DAOS_PROP_PO_OWNER:
	case DAOS_PROP_CO_OWNER:
	case DAOS_PROP_PO_OWNER_GROUP:
	case DAOS_PROP_CO_OWNER_GROUP:
	case DAOS_PROP_PO_POLICY:
		return true;
	}
	return false;
}

bool
daos_prop_has_ptr(struct daos_prop_entry *entry)
{
	switch (entry->dpe_type) {
	case DAOS_PROP_PO_SVC_LIST:
	case DAOS_PROP_PO_ACL:
	case DAOS_PROP_CO_ACL:
	case DAOS_PROP_CO_ROOTS:
		return true;
	}
	return false;
}

static void
daos_prop_entry_free_value(struct daos_prop_entry *entry)
{
	if (entry->dpe_type == DAOS_PROP_PO_SVC_LIST) {
		if (entry->dpe_val_ptr)
			d_rank_list_free((d_rank_list_t *)entry->dpe_val_ptr);
		return;
	}

	if (daos_prop_has_str(entry)) {
		if (entry->dpe_str) {
			D_FREE(entry->dpe_str);
		}
		return;
	}

	if (daos_prop_has_ptr(entry)) {
		D_FREE(entry->dpe_val_ptr);
		return;
	}
}

void
daos_prop_fini(daos_prop_t *prop)
{
	int	i;

	if (!prop->dpp_entries)
		goto out;

	for (i = 0; i < prop->dpp_nr; i++) {
		struct daos_prop_entry *entry;

		entry = &prop->dpp_entries[i];
		daos_prop_entry_free_value(entry);
	}

	D_FREE(prop->dpp_entries);
out:
	prop->dpp_nr = 0;
}

void
daos_prop_free(daos_prop_t *prop)
{
	if (prop == NULL)
		return;

	daos_prop_fini(prop);
	D_FREE(prop);
}

int
daos_prop_merge2(daos_prop_t *old_prop, daos_prop_t *new_prop, daos_prop_t **out_prop)
{
	daos_prop_t		*result;
	int			rc;
	uint32_t		result_nr;
	uint32_t		i, result_i;
	struct daos_prop_entry	*entry;

	if (old_prop == NULL || new_prop == NULL) {
		D_ERROR("NULL input\n");
		return -DER_INVAL;
	}

	/*
	 * We might override some values in the old prop. Need to account for that in the final prop
	 * count.
	 */
	result_nr = old_prop->dpp_nr;
	for (i = 0; i < new_prop->dpp_nr; i++) {
		entry = daos_prop_entry_get(old_prop, new_prop->dpp_entries[i].dpe_type);
		if (entry == NULL) /* New entry isn't a duplicate of old */
			result_nr++;
	}

	result = daos_prop_alloc(result_nr);
	if (result == NULL)
		return -DER_NOMEM;

	if (result->dpp_nr == 0) /* Nothing more to do */
		D_GOTO(out, rc = -DER_SUCCESS);

	result_i = 0;
	for (i = 0; i < old_prop->dpp_nr; i++, result_i++) {
		rc = daos_prop_entry_copy(&old_prop->dpp_entries[i],
					  &result->dpp_entries[result_i]);
		if (rc != 0)
			goto err;
	}

	/*  Either add or update based on the values of the new prop entries */
	for (i = 0; i < new_prop->dpp_nr; i++) {
		entry = daos_prop_entry_get(result, new_prop->dpp_entries[i].dpe_type);
		if (entry == NULL) {
			D_ASSERT(result_i < result_nr);
			entry = &result->dpp_entries[result_i];
			result_i++;
		}
		rc = daos_prop_entry_copy(&new_prop->dpp_entries[i], entry);
		if (rc != 0)
			goto err;
	}
out:
	*out_prop = result;
	return 0;

err:
	daos_prop_free(result);
	return rc;
}

daos_prop_t *
daos_prop_merge(daos_prop_t *old_prop, daos_prop_t *new_prop)
{
	daos_prop_t *out_prop;
	int          rc;

	rc = daos_prop_merge2(old_prop, new_prop, &out_prop);
	if (rc == -DER_SUCCESS)
		return out_prop;
	return NULL;
}

static bool
str_valid(const char *str, const char *prop_name, size_t max_len)
{
	size_t len;

	if (unlikely(str == NULL)) {
		D_ERROR("invalid NULL %s\n", prop_name);
		return false;
	}
	/* Detect if it's longer than max_len */
	len = strnlen(str, max_len + 1);
	if (len == 0 || len > max_len) {
		D_ERROR("invalid %s len=%lu, max=%lu\n", prop_name, len,
			max_len);
		return false;
	}

	return true;
}

static bool
daos_prop_owner_valid(d_string_t owner)
{
	/* Max length passed in doesn't include the null terminator */
	return str_valid(owner, "owner", DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static bool
daos_prop_owner_group_valid(d_string_t owner)
{
	/* Max length passed in doesn't include the null terminator */
	return str_valid(owner, "owner-group", DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static bool
daos_prop_policy_valid(d_string_t policy_str)
{
	if (!daos_policy_try_parse(policy_str, NULL))
		return false;

	return true;
}

/**
 * Check if the input daos_prop_t parameter is valid
 * \a pool true for pool properties, false for container properties.
 * \a input true for input properties that should with reasonable value,
 *          false for output that need not check the value.
 */
bool
daos_prop_valid(daos_prop_t *prop, bool pool, bool input)
{
	uint32_t		type;
	uint64_t		val;
	struct daos_acl		*acl_ptr;
	int			i;

	if (prop == NULL) {
		D_ERROR("NULL properties\n");
		return false;
	}
	if (prop->dpp_nr > DAOS_PROP_ENTRIES_MAX_NR) {
		D_ERROR("invalid ddp_nr %d (> %d).\n",
			prop->dpp_nr, DAOS_PROP_ENTRIES_MAX_NR);
		return false;
	}
	if (prop->dpp_nr == 0) {
		if (prop->dpp_entries != NULL)
			D_ERROR("invalid properties, NON-NULL dpp_entries with zero dpp_nr.\n");
		return prop->dpp_entries == NULL;
	}
	if (prop->dpp_entries == NULL) {
		D_ERROR("invalid properties, NULL dpp_entries with non-zero dpp_nr.\n");
		return false;
	}
	for (i = 0; i < prop->dpp_nr; i++) {
		struct daos_co_status co_status;
		int                   rc;

		type = prop->dpp_entries[i].dpe_type;
		if (pool) {
			if (type <= DAOS_PROP_PO_MIN ||
			    type >= DAOS_PROP_PO_MAX) {
				D_ERROR("invalid type %d for pool.\n", type);
				return false;
			}
		} else {
			if (type <= DAOS_PROP_CO_MIN ||
			    type >= DAOS_PROP_CO_MAX) {
				D_ERROR("invalid type %d for container.\n",
					type);
				return false;
			}
		}
		/* for output parameter need not check entry value */
		if (!input)
			continue;
		switch (type) {
		/* pool properties */
		case DAOS_PROP_PO_LABEL:
		case DAOS_PROP_CO_LABEL:
			if (!daos_label_is_valid(prop->dpp_entries[i].dpe_str)) {
				D_ERROR("invalid label \"%s\"\n", prop->dpp_entries[i].dpe_str);
				return false;
			}
			break;
		case DAOS_PROP_PO_POLICY:
			if (!daos_prop_policy_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_PO_ACL:
		case DAOS_PROP_CO_ACL:
			acl_ptr = prop->dpp_entries[i].dpe_val_ptr;
			/* This can fail with out of memory errors */
			rc = daos_acl_validate(acl_ptr);
			if (rc == -DER_NOMEM)
				rc = daos_acl_validate(acl_ptr);
			if (rc != 0)
				return false;
			break;
		case DAOS_PROP_PO_SPACE_RB:
			val = prop->dpp_entries[i].dpe_val;
			if (val > 100) {
				D_ERROR("invalid space_rb "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_REDUN_FAC:
			val = prop->dpp_entries[i].dpe_val;
			if (!daos_rf_is_valid(val)) {
				D_ERROR("invalid rf "DF_U64"\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_PERF_DOMAIN:
		case DAOS_PROP_CO_PERF_DOMAIN:
			val = prop->dpp_entries[i].dpe_val;
			if (val != PO_COMP_TP_ROOT &&
			    val != PO_COMP_TP_GRP) {
				D_ERROR("invalid perf domain "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_SELF_HEAL:
		case DAOS_PROP_PO_EC_CELL_SZ:
		case DAOS_PROP_PO_EC_PDA:
		case DAOS_PROP_PO_RP_PDA:
		case DAOS_PROP_PO_GLOBAL_VERSION:
		case DAOS_PROP_PO_OBJ_VERSION:
			break;
		case DAOS_PROP_PO_UPGRADE_STATUS:
			val = prop->dpp_entries[i].dpe_val;
			if (val > DAOS_UPGRADE_STATUS_COMPLETED) {
				D_ERROR("invalid pool upgrade status "DF_U64".\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_PO_RECLAIM:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_RECLAIM_DISABLED &&
			    val != DAOS_RECLAIM_LAZY &&
			    val != DAOS_RECLAIM_SNAPSHOT &&
			    val != DAOS_RECLAIM_BATCH &&
			    val != DAOS_RECLAIM_TIME) {
				D_ERROR("invalid reclaim "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_OWNER:
		case DAOS_PROP_CO_OWNER:
			if (!daos_prop_owner_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
		case DAOS_PROP_CO_OWNER_GROUP:
			if (!daos_prop_owner_group_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_PO_SVC_LIST:
			break;
		case DAOS_PROP_PO_SCRUB_MODE:
			val = prop->dpp_entries[i].dpe_val;
			if (val >= DAOS_SCRUB_MODE_INVALID) {
				D_ERROR("invalid scrub mode: "DF_U64"\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_SCRUB_FREQ:
			/* accepting any number of seconds for now */
			break;
		case DAOS_PROP_PO_SCRUB_THRESH:
			/* accepting any number for threshold for now */
			break;
		case DAOS_PROP_PO_SVC_REDUN_FAC:
			val = prop->dpp_entries[i].dpe_val;
			if (!daos_svc_rf_is_valid(val)) {
				D_ERROR("invalid svc_rf "DF_U64"\n", val);
				return false;
			}
			break;
		/* container-only properties */
		case DAOS_PROP_CO_LAYOUT_TYPE:
			val = prop->dpp_entries[i].dpe_val;
			if (val >= DAOS_PROP_CO_LAYOUT_MAX) {
				D_ERROR("invalid layout type "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			break;
		case DAOS_PROP_CO_CSUM:
			val = prop->dpp_entries[i].dpe_val;
			if (!daos_cont_csum_prop_is_valid(val)) {
				D_ERROR("invalid checksum type "DF_U64".\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			/** Chunk size is encoded on 32 bits */
			val = prop->dpp_entries[i].dpe_val;
			if (val >= (1ULL << 32)) {
				D_ERROR("invalid chunk size " DF_U64 ". Should be < 2GiB\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_SCRUBBER_DISABLED:
			/* Placeholder */
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_CSUM_SV_OFF &&
			    val != DAOS_PROP_CO_CSUM_SV_ON) {
				D_ERROR("invalid csum server verify property " DF_U64 ".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_DEDUP:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_DEDUP_OFF &&
			    val != DAOS_PROP_CO_DEDUP_MEMCMP &&
			    val != DAOS_PROP_CO_DEDUP_HASH) {
				D_ERROR("invalid deduplication parameter " DF_U64 ".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			val = prop->dpp_entries[i].dpe_val;
			if (val < 4096 || val >= (1ULL << 32)) {
				D_ERROR("invalid deduplication threshold "DF_U64
					". Should be >= 4KiB and < 4GiB\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_CO_ALLOCED_OID:
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_REDUN_RF0 &&
			    val != DAOS_PROP_CO_REDUN_RF1 &&
			    val != DAOS_PROP_CO_REDUN_RF2 &&
			    val != DAOS_PROP_CO_REDUN_RF3 &&
			    val != DAOS_PROP_CO_REDUN_RF4) {
				D_ERROR("invalid redundancy factor "DF_U64".\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			val = prop->dpp_entries[i].dpe_val;
			if (val < DAOS_PROP_CO_REDUN_MIN ||
			    val > DAOS_PROP_CO_REDUN_MAX) {
				D_ERROR("invalid redundancy level "DF_U64
					", must be within [%d - %d]\n",
					val, DAOS_PROP_CO_REDUN_RANK,
					DAOS_PROP_CO_REDUN_MAX);
				return false;
			}
			break;
		case DAOS_PROP_CO_COMPRESS:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_COMPRESS_OFF &&
			    val != DAOS_PROP_CO_COMPRESS_LZ4 &&
			    val != DAOS_PROP_CO_COMPRESS_DEFLATE &&
			    val != DAOS_PROP_CO_COMPRESS_DEFLATE1 &&
			    val != DAOS_PROP_CO_COMPRESS_DEFLATE2 &&
			    val != DAOS_PROP_CO_COMPRESS_DEFLATE3 &&
			    val != DAOS_PROP_CO_COMPRESS_DEFLATE4) {
				D_ERROR("invalid compression parameter " DF_U64 ".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_ENCRYPT:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_ENCRYPT_OFF &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_XTS128 &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_XTS256 &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_CBC128 &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_CBC192 &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_CBC256 &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_GCM128 &&
			    val != DAOS_PROP_CO_ENCRYPT_AES_GCM256) {
				D_ERROR("invalid encryption parameter " DF_U64 ".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_STATUS:
			val = prop->dpp_entries[i].dpe_val;
			daos_prop_val_2_co_status(val, &co_status);
			if (co_status.dcs_status != DAOS_PROP_CO_HEALTHY &&
			    co_status.dcs_status != DAOS_PROP_CO_UNCLEAN) {
				D_ERROR("invalid container status %d\n",
					co_status.dcs_status);
				return false;
			}
			break;
		case DAOS_PROP_PO_CHECKPOINT_MODE:
			val = prop->dpp_entries[i].dpe_val;
			if (val > DAOS_CHECKPOINT_LAZY) {
				D_ERROR("invalid checkpoint mode: " DF_U64 "\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_CHECKPOINT_FREQ:
		case DAOS_PROP_PO_CHECKPOINT_THRESH:
		case DAOS_PROP_CO_SNAPSHOT_MAX:
		case DAOS_PROP_CO_ROOTS:
		case DAOS_PROP_CO_EC_CELL_SZ:
		case DAOS_PROP_CO_EC_PDA:
		case DAOS_PROP_CO_RP_PDA:
		case DAOS_PROP_CO_GLOBAL_VERSION:
		case DAOS_PROP_CO_OBJ_VERSION:
			break;
		default:
			D_ERROR("invalid dpe_type %d.\n", type);
			return false;
		}
	}
	return true;
}

int
daos_prop_entry_copy(struct daos_prop_entry *entry,
		     struct daos_prop_entry *entry_dup)
{
	struct daos_acl		*acl_ptr;
	const d_rank_list_t	*svc_list;
	d_rank_list_t		*dst_list;
	int			rc;

	D_ASSERT(entry != NULL);
	D_ASSERT(entry_dup != NULL);

	/* Clean up the entry we're copying to, first */
	daos_prop_entry_free_value(entry_dup);

	entry_dup->dpe_type = entry->dpe_type;
	entry_dup->dpe_flags = entry->dpe_flags;
	switch (entry->dpe_type) {
	case DAOS_PROP_PO_LABEL:
	case DAOS_PROP_CO_LABEL:
		if (entry->dpe_str) {
			D_STRNDUP(entry_dup->dpe_str, entry->dpe_str,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (entry_dup->dpe_str == NULL) {
				return -DER_NOMEM;
			}
		}
		break;
	case DAOS_PROP_PO_ACL:
	case DAOS_PROP_CO_ACL:
		acl_ptr = entry->dpe_val_ptr;
		entry_dup->dpe_val_ptr = daos_acl_dup(acl_ptr);
		if (entry_dup->dpe_val_ptr == NULL) {
			return -DER_NOMEM;
		}
		break;
	case DAOS_PROP_PO_OWNER:
	case DAOS_PROP_CO_OWNER:
	case DAOS_PROP_PO_OWNER_GROUP:
	case DAOS_PROP_CO_OWNER_GROUP:
		D_STRNDUP(entry_dup->dpe_str, entry->dpe_str,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (entry_dup->dpe_str == NULL) {
			return -DER_NOMEM;
		}
		break;
	case DAOS_PROP_PO_SVC_LIST:
		svc_list = entry->dpe_val_ptr;

		rc = d_rank_list_dup(&dst_list, svc_list);
		if (rc) {
			D_ERROR("failed dup rank list: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
		entry_dup->dpe_val_ptr = dst_list;
		break;
	case DAOS_PROP_CO_ROOTS:
		rc = daos_prop_entry_dup_co_roots(entry_dup, entry);
		if (rc) {
			D_ERROR("failed to dup roots: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
		break;
	case DAOS_PROP_PO_POLICY:
		D_STRNDUP(entry_dup->dpe_str, entry->dpe_str,
			  DAOS_PROP_POLICYSTR_MAX_LEN);
		if (entry_dup->dpe_str == NULL)
			return -DER_NOMEM;
		break;
	default:
		entry_dup->dpe_val = entry->dpe_val;
		break;
	}

	return 0;
}

/**
 * duplicate the properties
 * \a pool true for pool properties, false for container properties.
 */
daos_prop_t *
daos_prop_dup(daos_prop_t *prop, bool pool, bool input)
{
	daos_prop_t		*prop_dup;
	struct daos_prop_entry	*entry, *entry_dup;
	int			 i, j;
	int			 rc;
	int			 valid_nr = 0;

	if (!daos_prop_valid(prop, pool, input))
		return NULL;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		if (daos_prop_is_set(entry))
			valid_nr++;
	}
	if (valid_nr == 0)
		return NULL;
	prop_dup = daos_prop_alloc(valid_nr);
	if (prop_dup == NULL)
		return NULL;

	j = 0;
	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		if (!daos_prop_is_set(entry))
			continue;
		entry_dup = &prop_dup->dpp_entries[j++];
		rc = daos_prop_entry_copy(entry, entry_dup);
		if (rc != 0) {
			daos_prop_free(prop_dup);
			return NULL;
		}
	}

	return prop_dup;
}

/**
 * Get the property entry of \a type in \a prop
 * return NULL if not found.
 */
struct daos_prop_entry *
daos_prop_entry_get(daos_prop_t *prop, uint32_t type)
{
	int i;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return NULL;
	for (i = 0; i < prop->dpp_nr; i++) {
		if (prop->dpp_entries[i].dpe_type == type)
			return &prop->dpp_entries[i];
	}
	return NULL;
}

int
daos_prop_set_str(daos_prop_t *prop, uint32_t type, const char *str, daos_size_t len)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(prop, type);
	if (entry == NULL)
		return -DER_NONEXIST;

	return daos_prop_entry_set_str(entry, str, len);
}

int
daos_prop_entry_set_str(struct daos_prop_entry *entry, const char *str, daos_size_t len)
{
	if (entry == NULL)
		return -DER_INVAL;
	if (!daos_prop_has_str(entry)) {
		D_ERROR("Entry type does not expect a string value\n");
		return -DER_INVAL;
	}
	D_FREE(entry->dpe_str);
	if (str == NULL || len == 0)
		return 0;

	D_STRNDUP(entry->dpe_str, str, len);
	if (entry->dpe_str == NULL)
		return -DER_NOMEM;

	return 0;
}

int
daos_prop_set_ptr(daos_prop_t *prop, uint32_t type, const void *ptr, daos_size_t size)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(prop, type);
	if (entry == NULL)
		return -DER_NONEXIST;

	return daos_prop_entry_set_ptr(entry, ptr, size);
}

int
daos_prop_entry_set_ptr(struct daos_prop_entry *entry, const void *ptr, daos_size_t size)
{
	if (entry == NULL)
		return -DER_INVAL;
	if (!daos_prop_has_ptr(entry)) {
		D_ERROR("Entry type does not expect a ptr value\n");
		return -DER_INVAL;
	}
	D_FREE(entry->dpe_val_ptr);
	if (ptr == NULL || size == 0)
		return 0;

	D_ALLOC(entry->dpe_val_ptr, size);
	if (entry->dpe_val_ptr == NULL)
		return -DER_NOMEM;
	memcpy(entry->dpe_val_ptr, ptr, size);

	return 0;
}

static void
free_str_prop_entry(daos_prop_t *prop, uint32_t type)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(prop, type);
	if (entry != NULL)
		D_FREE(entry->dpe_str);
}

static void
free_ptr_prop_entry(daos_prop_t *prop, uint32_t type)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(prop, type);
	if (entry != NULL)
		D_FREE(entry->dpe_val_ptr);
}

/**
 * Copy properties from \a prop_reply to \a prop_req.
 * Used to copy the properties from pool query or container query to user's
 * properties. If user provided \a prop_req with zero dpp_nr (and NULL
 * dpp_entries), it will allocate needed buffer and assign to user's daos_prop_t
 * struct, the needed buffer to store label will be allocated as well.
 * User can free properties buffer by calling daos_prop_free().
 */
int
daos_prop_copy(daos_prop_t *prop_req, daos_prop_t *prop_reply)
{
	struct daos_prop_entry	*entry_req, *entry_reply;
	bool			 entries_alloc = false;
	bool			 label_alloc = false;
	bool			 acl_alloc = false;
	bool			 owner_alloc = false;
	bool			 group_alloc = false;
	bool			 svc_list_alloc = false;
	bool			 roots_alloc = false;
	bool			 policy_alloc = false;
	struct daos_acl		*acl;
	d_rank_list_t		*dst_list;
	uint32_t		 type;
	int			 i;
	int			 rc = 0;

	if (prop_req == NULL) {
		D_ERROR("no prop in req.\n");
		return -DER_INVAL;
	}
	if (prop_reply == NULL || prop_reply->dpp_nr == 0 ||
	    prop_reply->dpp_entries == NULL) {
		D_ERROR("no prop or empty prop in reply.\n");
		return -DER_PROTO;
	}
	if (prop_req->dpp_nr == 0) {
		prop_req->dpp_nr = prop_reply->dpp_nr;
		D_ALLOC_ARRAY(prop_req->dpp_entries, prop_req->dpp_nr);
		if (prop_req->dpp_entries == NULL)
			return -DER_NOMEM;
		entries_alloc = true;
	}

	for (i = 0; i < prop_req->dpp_nr; i++) {
		entry_req = &prop_req->dpp_entries[i];
		type = entry_req->dpe_type;
		if (type == 0) {
			/* req doesn't have any entry type populated yet */
			if (i < prop_reply->dpp_nr) {
				type = prop_reply->dpp_entries[i].dpe_type;
				entry_req->dpe_type = type;
			} else {
				return 0;
			}
		}
		/* this is possible now */
		entry_reply = daos_prop_entry_get(prop_reply, type);
		if (entry_reply == NULL) {
			entry_req->dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
			continue;
		}
		entry_req->dpe_flags = entry_reply->dpe_flags;
		if (type == DAOS_PROP_PO_LABEL || type == DAOS_PROP_CO_LABEL) {
			D_STRNDUP(entry_req->dpe_str, entry_reply->dpe_str,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			label_alloc = true;
		} else if (type == DAOS_PROP_PO_ACL ||
			   type == DAOS_PROP_CO_ACL) {
			acl = entry_reply->dpe_val_ptr;
			entry_req->dpe_val_ptr = daos_acl_dup(acl);
			if (entry_req->dpe_val_ptr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			acl_alloc = true;
		} else if (type == DAOS_PROP_PO_OWNER ||
			   type == DAOS_PROP_CO_OWNER) {
			D_STRNDUP(entry_req->dpe_str,
				  entry_reply->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			owner_alloc = true;
		} else if (type == DAOS_PROP_PO_OWNER_GROUP ||
			   type == DAOS_PROP_CO_OWNER_GROUP) {
			D_STRNDUP(entry_req->dpe_str,
				  entry_reply->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			group_alloc = true;
		} else if (type == DAOS_PROP_PO_SVC_LIST) {
			d_rank_list_t *svc_list = entry_reply->dpe_val_ptr;

			rc = d_rank_list_dup(&dst_list, svc_list);
			if (rc)
				D_GOTO(out, rc);
			svc_list_alloc = true;
			entry_req->dpe_val_ptr = dst_list;
		} else if (type == DAOS_PROP_CO_ROOTS) {
			rc = daos_prop_entry_dup_co_roots(entry_req,
							  entry_reply);
			if (rc)
				D_GOTO(out, rc);

			roots_alloc = true;
		} else if (type == DAOS_PROP_PO_POLICY) {
			D_STRNDUP(entry_req->dpe_str, entry_reply->dpe_str,
				  DAOS_PROP_POLICYSTR_MAX_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			policy_alloc = true;
		} else {
			entry_req->dpe_val = entry_reply->dpe_val;
		}
	}

out:
	if (rc) {
		if (label_alloc) {
			free_str_prop_entry(prop_req, DAOS_PROP_PO_LABEL);
			free_str_prop_entry(prop_req, DAOS_PROP_CO_LABEL);
		}
		if (acl_alloc) {
			free_ptr_prop_entry(prop_req, DAOS_PROP_PO_ACL);
			free_ptr_prop_entry(prop_req, DAOS_PROP_CO_ACL);
		}
		if (owner_alloc) {
			free_str_prop_entry(prop_req, DAOS_PROP_PO_OWNER);
			free_str_prop_entry(prop_req, DAOS_PROP_CO_OWNER);
		}
		if (group_alloc) {
			free_str_prop_entry(prop_req, DAOS_PROP_PO_OWNER_GROUP);
			free_str_prop_entry(prop_req, DAOS_PROP_CO_OWNER_GROUP);
		}
		if (svc_list_alloc) {
			entry_req = daos_prop_entry_get(prop_req,
						DAOS_PROP_PO_SVC_LIST);
			d_rank_list_free(entry_req->dpe_val_ptr);
		}
		if (roots_alloc)
			free_ptr_prop_entry(prop_req, DAOS_PROP_CO_ROOTS);

		if (policy_alloc)
			free_ptr_prop_entry(prop_req, DAOS_PROP_PO_POLICY);

		if (entries_alloc)
			D_FREE(prop_req->dpp_entries);
	}
	return rc;
}

int
daos_prop_entry_dup_co_roots(struct daos_prop_entry *dst,
			     struct daos_prop_entry *src)
{
	if (dst->dpe_val_ptr == NULL) {
		D_ALLOC(dst->dpe_val_ptr, sizeof(struct daos_prop_co_roots));

		if (dst->dpe_val_ptr == NULL)
			return -DER_NOMEM;
	}

	memcpy(dst->dpe_val_ptr,
	       src->dpe_val_ptr,
	       sizeof(struct daos_prop_co_roots));
	return 0;
}

int
daos_prop_entry_dup_ptr(struct daos_prop_entry *entry_dst,
			struct daos_prop_entry *entry_src, size_t len)
{
	D_ASSERT(entry_src != NULL);
	D_ASSERT(entry_dst != NULL);

	D_ALLOC(entry_dst->dpe_val_ptr, len);
	if (entry_dst->dpe_val_ptr == NULL)
		return -DER_NOMEM;

	memcpy(entry_dst->dpe_val_ptr, entry_src->dpe_val_ptr, len);
	return 0;
}

int
daos_prop_entry_cmp_acl(struct daos_prop_entry *entry1,
			struct daos_prop_entry *entry2)
{
	struct daos_acl *acl1;
	size_t		acl1_size;
	struct daos_acl *acl2;
	size_t		acl2_size;

	/* Never call this with entries not known to be ACL types */
	D_ASSERT(entry1->dpe_type == DAOS_PROP_PO_ACL ||
		 entry1->dpe_type == DAOS_PROP_CO_ACL);
	D_ASSERT(entry2->dpe_type == DAOS_PROP_PO_ACL ||
		 entry2->dpe_type == DAOS_PROP_CO_ACL);

	if (entry1->dpe_val_ptr == NULL && entry2->dpe_val_ptr == NULL)
		return 0;

	if (entry1->dpe_val_ptr == NULL || entry2->dpe_val_ptr == NULL) {
		D_ERROR("ACL mismatch, NULL ptr\n");
		return -DER_MISMATCH;
	}

	acl1 = entry1->dpe_val_ptr;
	acl2 = entry2->dpe_val_ptr;

	acl1_size = daos_acl_get_size(acl1);
	acl2_size = daos_acl_get_size(acl2);

	if (acl1_size != acl2_size) {
		D_ERROR("ACL len mismatch, %lu != %lu\n", acl1_size, acl2_size);
		return -DER_MISMATCH;
	}

	if (memcmp(acl1, acl2, acl1_size) != 0) {
		D_ERROR("ACL content mismatch\n");
		return -DER_MISMATCH;
	}

	return 0;
}

static int
parse_entry(char *str, struct daos_prop_entry *entry)
{
	char	*name;
	char	*val;
	char	*end_token = NULL;
	int	rc = 0;

	/** get prop_name */
	D_ASSERT(str != NULL);
	name = strtok_r(str, ":", &end_token);
	if (name == NULL)
		return -DER_INVAL;
	/** get prop value */
	val = strtok_r(NULL, ";", &end_token);
	if (val == NULL)
		return -DER_INVAL;

	if (strcmp(name, DAOS_PROP_ENTRY_LABEL) == 0) {
		entry->dpe_type = DAOS_PROP_CO_LABEL;
		rc = daos_prop_entry_set_str(entry, val, DAOS_PROP_LABEL_MAX_LEN);
	} else if (strcmp(name, DAOS_PROP_ENTRY_CKSUM) == 0) {
		entry->dpe_type = DAOS_PROP_CO_CSUM;
		rc = daos_str2csumcontprop(val);
		if (rc < 0)
			return rc;
		entry->dpe_val = rc;
		rc = 0;
	} else if (strcmp(name, DAOS_PROP_ENTRY_CKSUM_SIZE) == 0) {
		entry->dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
		entry->dpe_val = strtoull(val, NULL, 0);
	} else if (strcmp(name, DAOS_PROP_ENTRY_SRV_CKSUM) == 0) {
		entry->dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
		if (strcmp(val, "on") == 0)
			entry->dpe_val = DAOS_PROP_CO_CSUM_SV_ON;
		else if (strcmp(val, "off") == 0)
			entry->dpe_val = DAOS_PROP_CO_CSUM_SV_OFF;
		else
			rc = -DER_INVAL;
	} else if (strcmp(name, DAOS_PROP_ENTRY_DEDUP) == 0) {
		entry->dpe_type = DAOS_PROP_CO_DEDUP;
		if (strcmp(val, "off") == 0)
			entry->dpe_val = DAOS_PROP_CO_DEDUP_OFF;
		else if (strcmp(val, "memcmp") == 0)
			entry->dpe_val = DAOS_PROP_CO_DEDUP_MEMCMP;
		else if (strcmp(val, "hash") == 0)
			entry->dpe_val = DAOS_PROP_CO_DEDUP_HASH;
		else
			rc = -DER_INVAL;
	} else if (strcmp(name, DAOS_PROP_ENTRY_DEDUP_THRESHOLD) == 0) {
		entry->dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
		entry->dpe_val = strtoull(val, NULL, 0);
	} else if (strcmp(name, DAOS_PROP_ENTRY_COMPRESS) == 0) {
		entry->dpe_type = DAOS_PROP_CO_COMPRESS;
		rc = daos_str2compresscontprop(val);
		if (rc < 0)
			return rc;
		entry->dpe_val = rc;
		rc = 0;
	} else if (strcmp(name, DAOS_PROP_ENTRY_ENCRYPT) == 0) {
		entry->dpe_type = DAOS_PROP_CO_ENCRYPT;
		rc = daos_str2encryptcontprop(val);
		if (rc < 0)
			return rc;
		entry->dpe_val = rc;
		rc = 0;
	} else if (strcmp(name, DAOS_PROP_ENTRY_REDUN_FAC) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_REDUN_FAC_OLD) == 0) {
		entry->dpe_type = DAOS_PROP_CO_REDUN_FAC;
		if (!strcmp(val, "0"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF0;
		else if (!strcmp(val, "1"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF1;
		else if (!strcmp(val, "2"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF2;
		else if (!strcmp(val, "3"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF3;
		else if (!strcmp(val, "4"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF4;
		else {
			D_ERROR("presently supported redundancy factors (rf) are [0-4]\n");
			rc = -DER_INVAL;
		}
	} else if (strcmp(name, DAOS_PROP_ENTRY_EC_CELL_SZ) == 0) {
		entry->dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
		entry->dpe_val = strtoull(val, NULL, 0);
	} else if (strcmp(name, DAOS_PROP_ENTRY_EC_PDA) == 0) {
		entry->dpe_type = DAOS_PROP_CO_EC_PDA;
		entry->dpe_val = strtoull(val, NULL, 0);
	} else if (strcmp(name, DAOS_PROP_ENTRY_RP_PDA) == 0) {
		entry->dpe_type = DAOS_PROP_CO_RP_PDA;
		entry->dpe_val = strtoull(val, NULL, 0);
	} else if (strcmp(name, DAOS_PROP_ENTRY_PERF_DOMAIN) == 0) {
		entry->dpe_type = DAOS_PROP_CO_PERF_DOMAIN;
		entry->dpe_val = strtoull(val, NULL, 0);
	} else if (strcmp(name, DAOS_PROP_ENTRY_LAYOUT_TYPE) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_LAYOUT_VER) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_REDUN_LVL) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_REDUN_LVL_OLD) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_SNAPSHOT_MAX) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_ALLOCED_OID) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_STATUS) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_OWNER) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_GROUP) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_GLOBAL_VERSION) == 0 ||
		   strcmp(name, DAOS_PROP_ENTRY_OBJ_VERSION) == 0) {
		D_ERROR("Property %s is read only\n", name);
		rc = -DER_INVAL;
	} else {
		D_ERROR("Property %s is invalid\n", name);
		rc = -DER_INVAL;
	}

	return rc;
}

int
daos_prop_from_str(const char *str, daos_size_t len, daos_prop_t **_prop)
{
	daos_prop_t	*prop = NULL;
	char		*save_prop = NULL;
	char		*t;
	char		*local;
	uint32_t	n;
	int		rc = 0;

	if (str == NULL || len == 0 || _prop == NULL)
		return -DER_INVAL;

	/** count how many properties we have in str */
	D_STRNDUP(local, str, len);
	if (!local)
		return -DER_NOMEM;
	n = 0;
	if (strtok_r(local, ";", &save_prop) != NULL) {
		n++;
		while (strtok_r(NULL, ";", &save_prop) != NULL)
			n++;
	}
	D_FREE(local);

	if (n == 0) {
		D_ERROR("Invalid property format %s\n", str);
		return -DER_INVAL;
	}

	/** allocate a property with the number of entries needed */
	prop = daos_prop_alloc(n);
	if (prop == NULL)
		return -DER_NOMEM;

	D_STRNDUP(local, str, len);
	if (!local)
		D_GOTO(err_prop, rc = -DER_NOMEM);

	/** get a prop_name:value pair */
	t = strtok_r(local, ";", &save_prop);

	n = 0;
	while (t != NULL) {
		rc = parse_entry(t, &prop->dpp_entries[n]);
		if (rc)
			D_GOTO(err_prop, rc);
		t = strtok_r(NULL, ";", &save_prop);
		n++;
	}

	*_prop = prop;
out:
	D_FREE(local);
	return rc;
err_prop:
	daos_prop_free(prop);
	goto out;
}
