/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
/**
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/dtx.h>
#include <daos_security.h>
#include <daos/cont_props.h>

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
			D_FREE_PTR(prop);
			return NULL;
		}
	}
	prop->dpp_nr = entries_nr;
	return prop;
}

static void
daos_prop_entry_free_value(struct daos_prop_entry *entry)
{
	switch (entry->dpe_type) {
	case DAOS_PROP_PO_LABEL:
	case DAOS_PROP_CO_LABEL:
	case DAOS_PROP_PO_OWNER:
	case DAOS_PROP_CO_OWNER:
	case DAOS_PROP_PO_OWNER_GROUP:
	case DAOS_PROP_CO_OWNER_GROUP:
		if (entry->dpe_str)
			D_FREE(entry->dpe_str);
		break;
	case DAOS_PROP_PO_ACL:
	case DAOS_PROP_CO_ACL:
		if (entry->dpe_val_ptr)
			D_FREE(entry->dpe_val_ptr);
		break;
	case DAOS_PROP_PO_SVC_LIST:
		if (entry->dpe_val_ptr)
			d_rank_list_free(
				(d_rank_list_t *)entry->dpe_val_ptr);
		break;
	default:
		break;
	};
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
	prop->dpp_entries = NULL;
out:
	prop->dpp_nr = 0;
}

void
daos_prop_free(daos_prop_t *prop)
{
	if (prop) {
		daos_prop_fini(prop);
		D_FREE_PTR(prop);
	}
}

daos_prop_t *
daos_prop_merge(daos_prop_t *old_prop, daos_prop_t *new_prop)
{
	daos_prop_t		*result;
	int			rc;
	uint32_t		result_nr;
	uint32_t		i, result_i;
	struct daos_prop_entry	*entry;

	if (old_prop == NULL || new_prop == NULL) {
		D_ERROR("NULL input\n");
		return NULL;
	}

	/*
	 * We might override some values in the old prop. Need to account for
	 * that in the final prop count.
	 */
	result_nr = old_prop->dpp_nr;
	for (i = 0; i < new_prop->dpp_nr; i++) {
		entry = daos_prop_entry_get(old_prop,
					    new_prop->dpp_entries[i].dpe_type);
		if (entry == NULL) /* New entry isn't a duplicate of old */
			result_nr++;
	}

	result = daos_prop_alloc(result_nr);
	if (result == NULL)
		return NULL;

	if (result->dpp_nr == 0) /* Nothing more to do */
		return result;

	result_i = 0;
	for (i = 0; i < old_prop->dpp_nr; i++, result_i++) {
		rc = daos_prop_entry_copy(&old_prop->dpp_entries[i],
					  &result->dpp_entries[result_i]);
		if (rc != 0)
			goto err;
	}

	/*
	 * Either add or update based on the values of the new prop entries
	 */
	for (i = 0; i < new_prop->dpp_nr; i++) {
		entry = daos_prop_entry_get(result,
					    new_prop->dpp_entries[i].dpe_type);
		if (entry == NULL) {
			D_ASSERT(result_i < result_nr);
			entry = &result->dpp_entries[result_i];
			result_i++;
		}
		rc = daos_prop_entry_copy(&new_prop->dpp_entries[i], entry);
		if (rc != 0)
			goto err;
	}

	return result;

err:
	daos_prop_free(result);
	return NULL;
}

static bool
daos_prop_str_valid(d_string_t str, const char *prop_name, size_t max_len)
{
	size_t len;

	if (str == NULL) {
		D_ERROR("invalid NULL %s\n", prop_name);
		return false;
	}
	/* Detect if it's longer than max_len */
	len = strnlen(str, max_len + 1);
	if (len == 0 || len > max_len) {
		D_ERROR("invalid %s len=%lu, max=%lu\n",
			prop_name, len, max_len);
		return false;
	}
	return true;
}

static bool
daos_prop_owner_valid(d_string_t owner)
{
	/* Max length passed in doesn't include the null terminator */
	return daos_prop_str_valid(owner, "owner",
				   DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static bool
daos_prop_owner_group_valid(d_string_t owner)
{
	/* Max length passed in doesn't include the null terminator */
	return daos_prop_str_valid(owner, "owner-group",
				   DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static bool
daos_prop_label_valid(d_string_t label)
{
	return daos_prop_str_valid(label, "label", DAOS_PROP_LABEL_MAX_LEN);
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
	uint32_t	type;
	uint64_t	val;
	struct daos_acl	*acl_ptr;
	int		i;

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
			D_ERROR("invalid properties, NON-NULL dpp_entries with "
				"zero dpp_nr.\n");
		return prop->dpp_entries == NULL;
	}
	if (prop->dpp_entries == NULL) {
		D_ERROR("invalid properties, NULL dpp_entries with non-zero "
			"dpp_nr.\n");
		return false;
	}
	for (i = 0; i < prop->dpp_nr; i++) {
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
			if (!daos_prop_label_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_PO_ACL:
		case DAOS_PROP_CO_ACL:
			acl_ptr = prop->dpp_entries[i].dpe_val_ptr;
			if (daos_acl_validate(acl_ptr) != 0)
				return false;
			break;
		case DAOS_PROP_PO_SPACE_RB:
			val = prop->dpp_entries[i].dpe_val;
			if (val > 100) {
				D_ERROR("invalid space_rb "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_SELF_HEAL:
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
		/* container-only properties */
		case DAOS_PROP_PO_SVC_LIST:
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_LAYOUT_UNKOWN &&
			    val != DAOS_PROP_CO_LAYOUT_POSIX &&
			    val != DAOS_PROP_CO_LAYOUT_HDF5) {
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
				D_ERROR("invalid chunk size "
					DF_U64". Should be < 2GiB\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_CSUM_SV_OFF &&
			    val != DAOS_PROP_CO_CSUM_SV_ON) {
				D_ERROR("invalid csum server verify property "
					DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_DEDUP:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_DEDUP_OFF &&
			    val != DAOS_PROP_CO_DEDUP_MEMCMP &&
			    val != DAOS_PROP_CO_DEDUP_HASH) {
				D_ERROR("invalid deduplication parameter "
					DF_U64".\n", val);
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
			if (val != DAOS_PROP_CO_REDUN_RACK &&
			    val != DAOS_PROP_CO_REDUN_NODE) {
				D_ERROR("invalid redundancy level "DF_U64".\n",
					val);
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
				D_ERROR("invalid compression parameter "
					DF_U64".\n", val);
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
				D_ERROR("invalid encryption parameter "
					DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
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
	switch (entry->dpe_type) {
	case DAOS_PROP_PO_LABEL:
	case DAOS_PROP_CO_LABEL:
		D_STRNDUP(entry_dup->dpe_str, entry->dpe_str,
			  DAOS_PROP_LABEL_MAX_LEN);
		if (entry_dup->dpe_str == NULL) {
			D_ERROR("failed to dup label.\n");
			return -DER_NOMEM;
		}
		break;
	case DAOS_PROP_PO_ACL:
	case DAOS_PROP_CO_ACL:
		acl_ptr = entry->dpe_val_ptr;
		entry_dup->dpe_val_ptr = daos_acl_dup(acl_ptr);
		if (entry_dup->dpe_val_ptr == NULL) {
			D_ERROR("failed to dup ACL\n");
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
			D_ERROR("failed to dup ownership info.\n");
			return -DER_NOMEM;
		}
		break;
	case DAOS_PROP_PO_SVC_LIST:
		svc_list = entry->dpe_val_ptr;

		rc = d_rank_list_dup(&dst_list, svc_list);
		if (rc) {
			D_ERROR("failed dup rank list\n");
			return rc;
		}
		entry_dup->dpe_val_ptr = dst_list;
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
daos_prop_dup(daos_prop_t *prop, bool pool)
{
	daos_prop_t		*prop_dup;
	struct daos_prop_entry	*entry, *entry_dup;
	int			 i;
	int			 rc;

	if (!daos_prop_valid(prop, pool, true))
		return NULL;

	prop_dup = daos_prop_alloc(prop->dpp_nr);
	if (prop_dup == NULL)
		return NULL;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		entry_dup = &prop_dup->dpp_entries[i];
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
	struct daos_acl		*acl;
	d_rank_list_t		*dst_list;
	uint32_t		 type;
	int			 i;
	int			 rc = 0;

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

	for (i = 0; i < prop_req->dpp_nr && i < prop_reply->dpp_nr; i++) {
		entry_req = &prop_req->dpp_entries[i];
		type = entry_req->dpe_type;
		if (type == 0) {
			/* req doesn't have any entry type populated yet */
			type = prop_reply->dpp_entries[i].dpe_type;
			entry_req->dpe_type = type;
		}
		entry_reply = daos_prop_entry_get(prop_reply, type);
		if (entry_reply == NULL) {
			D_ERROR("cannot find prop entry for type %d.\n", type);
			D_GOTO(out, rc = -DER_PROTO);
		}
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

		if (entries_alloc)
			D_FREE(prop_req->dpp_entries);
	}
	return rc;
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
		D_ERROR("ACL len mistmatch, %lu != %lu\n",
			acl1_size, acl2_size);
		return -DER_MISMATCH;
	}

	if (memcmp(acl1, acl2, acl1_size) != 0) {
		D_ERROR("ACL content mismatch\n");
		return -DER_MISMATCH;
	}

	return 0;
}
