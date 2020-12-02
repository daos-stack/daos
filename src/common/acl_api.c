/*
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <daos/common.h>
#include <daos_security.h>
#include <gurt/common.h>
#include <gurt/debug.h>
#include <gurt/hash.h>
#include <stdio.h>

/*
 * Comparison function for qsort. Compares by principal type.
 * Enum daos_acl_principal_type is in the expected order of type priority.
 */
static int
compare_aces(const void *p1, const void *p2)
{
	/* the inputs are in fact ptrs to ptrs */
	struct daos_ace *ace1 = *((struct daos_ace **)p1);
	struct daos_ace *ace2 = *((struct daos_ace **)p2);

	return (int)ace1->dae_principal_type - (int)ace2->dae_principal_type;
}

static void
free_ace_array(struct daos_ace **aces, uint16_t num_aces)
{
	uint16_t i;

	for (i = 0; i < num_aces; i++) {
		daos_ace_free(aces[i]);
	}

	D_FREE(aces);
}

static struct daos_ace **
copy_ace_array(struct daos_ace *aces[], uint16_t num_aces)
{
	struct daos_ace	**copy;
	uint16_t	i;

	D_ALLOC_ARRAY(copy, num_aces);
	if (copy == NULL) {
		return NULL;
	}

	for (i = 0; i < num_aces; i++) {
		ssize_t size = daos_ace_get_size(aces[i]);

		D_ASSERTF(size > 0, "ACE should have already been validated");
		D_ALLOC(copy[i], (size_t)size);
		if (copy[i] == NULL) {
			free_ace_array(copy, num_aces);
			return NULL;
		}

		memcpy(copy[i], aces[i], (size_t)size);
	}

	return copy;
}

static struct daos_ace **
get_copy_sorted_by_principal_type(struct daos_ace *aces[], uint16_t num_aces)
{
	struct daos_ace	**copy;

	copy = copy_ace_array(aces, num_aces);
	if (copy == NULL) {
		return NULL;
	}

	qsort(copy, num_aces, sizeof(struct daos_ace *), compare_aces);

	return copy;
}

/*
 * Flattens the array of ACE pointers into a single data blob in buffer.
 * Assumes caller has allocated the buffer large enough to hold the flattened
 * list.
 */
static void
flatten_aces(uint8_t *buffer, uint32_t buf_len, struct daos_ace *aces[],
	     uint16_t num_aces)
{
	int	i;
	uint8_t	*pen; /* next addr to write in the buffer */

	pen = buffer;
	for (i = 0; i < num_aces; i++) {
		ssize_t ace_size = daos_ace_get_size(aces[i]);
		/*
		 * Should have checked the ACE size validity during allocation
		 * of the input buffer.
		 */
		D_ASSERTF(ace_size > 0, "ACE size became invalid");

		/* Internal error if we walk outside the buffer */
		D_ASSERTF((pen + ace_size) <= (buffer + buf_len),
			  "ACEs too long for buffer size %u", buf_len);

		memcpy(pen, aces[i], ace_size);
		pen += ace_size;
	}
}

/*
 * Calculates the expected length of the flattened ACE data blob.
 *
 * Returns -DER_INVAL if one of the ACEs is NULL.
 */
static int
get_flattened_ace_size(struct daos_ace *aces[], uint16_t num_aces)
{
	int	i;
	int	total_size = 0;

	for (i = 0; i < num_aces; i++) {
		ssize_t len = daos_ace_get_size(aces[i]);

		if (len < 0) {
			return len;
		}

		total_size += len;
	}

	return total_size;
}

/*
 * ace_len is the length of the ACE list tacked onto the daos_acl struct
 */
static size_t
get_total_acl_size(uint32_t ace_len)
{
	return sizeof(struct daos_acl) + ace_len;
}

struct daos_acl *
daos_acl_create(struct daos_ace *aces[], uint16_t num_aces)
{
	struct daos_acl	*acl;
	int		ace_len;
	struct daos_ace	**sorted_aces;

	ace_len = get_flattened_ace_size(aces, num_aces);
	if (ace_len < 0) {
		/* Bad ACE list */
		return NULL;
	}

	sorted_aces = get_copy_sorted_by_principal_type(aces, num_aces);
	if (sorted_aces == NULL) {
		return NULL;
	}

	D_ALLOC(acl, get_total_acl_size(ace_len));
	if (acl == NULL) {
		free_ace_array(sorted_aces, num_aces);
		return NULL;
	}

	acl->dal_ver = DAOS_ACL_VERSION;
	acl->dal_len = ace_len;

	flatten_aces(acl->dal_ace, acl->dal_len, sorted_aces, num_aces);

	free_ace_array(sorted_aces, num_aces);

	return acl;
}

struct daos_acl *
daos_acl_dup(struct daos_acl *acl)
{
	struct daos_acl	*acl_copy;
	size_t		acl_len;

	if (acl == NULL) {
		return NULL;
	}

	acl_len = daos_acl_get_size(acl);
	D_ALLOC(acl_copy, acl_len);
	if (acl_copy == NULL) {
		return NULL;
	}

	memcpy(acl_copy, acl, acl_len);

	return acl_copy;
}

void
daos_acl_free(struct daos_acl *acl)
{
	/* The ACL is one contiguous data blob - nothing special to do */
	D_FREE(acl);
}

ssize_t
daos_acl_get_size(struct daos_acl *acl)
{
	if (acl == NULL) {
		return -DER_INVAL;
	}

	return get_total_acl_size(acl->dal_len);
}

static bool
principal_name_matches_ace(struct daos_ace *ace, const char *principal)
{
	if (principal == NULL && ace->dae_principal_len == 0) {
		/* Nothing to compare */
		return true;
	}

	return (principal != NULL &&
		strncmp(principal, ace->dae_principal, ace->dae_principal_len)
			== 0);
}

static bool
ace_matches_principal(struct daos_ace *ace,
		      enum daos_acl_principal_type type, const char *principal)
{
	size_t principal_len;

	if (principal == NULL || strlen(principal) == 0) {
		principal_len = 0;
	} else {
		principal_len = strlen(principal) + 1;
	}

	return	(ace->dae_principal_type == type) &&
		(ace->dae_principal_len == D_ALIGNUP(principal_len, 8)) &&
		principal_name_matches_ace(ace, principal);
}

static bool
principals_match(struct daos_ace *ace1, struct daos_ace *ace2)
{
	const char *principal_name = NULL;

	if (ace2->dae_principal_len > 0) {
		principal_name = ace2->dae_principal;
	}

	return ace_matches_principal(ace1, ace2->dae_principal_type,
			principal_name);
}

/*
 * Write the ACE to the memory address pointed to by the pen.
 * Returns the new pen position.
 */
static uint8_t *
write_ace(struct daos_ace *ace, uint8_t *pen)
{
	ssize_t	len;

	len = daos_ace_get_size(ace);
	D_ASSERTF(len > 0, "ACE should have already been validated");

	memcpy(pen, (uint8_t *)ace, (size_t)len);

	return pen + len;
}

static void
copy_acl_with_new_ace_inserted(struct daos_acl *acl, struct daos_acl *new_acl,
			       struct daos_ace *new_ace)
{
	struct daos_ace	*current;
	uint8_t		*pen;
	bool		new_written = false;

	current = daos_acl_get_next_ace(acl, NULL);
	pen = new_acl->dal_ace;
	while (current != NULL) {
		if (!new_written && current->dae_principal_type >
				new_ace->dae_principal_type) {
			new_written = true;
			pen = write_ace(new_ace, pen);
		} else {
			pen = write_ace(current, pen);
			current = daos_acl_get_next_ace(acl, current);
		}
	}

	/* didn't insert it - add it onto the end */
	if (!new_written) {
		write_ace(new_ace, pen);
	}
}

static void
overwrite_ace_for_principal(struct daos_acl *acl, struct daos_ace *new_ace)
{
	struct daos_ace	*current;

	current = daos_acl_get_next_ace(acl, NULL);
	while (current != NULL) {
		if (principals_match(current, new_ace)) {
			write_ace(new_ace, (uint8_t *)current);
			break;
		}

		current = daos_acl_get_next_ace(acl, current);
	}
}

static bool
acl_already_has_principal(struct daos_acl *acl,
		enum daos_acl_principal_type type,
		const char *principal_name)
{
	struct daos_ace *result = NULL;

	return (daos_acl_get_ace_for_principal(acl, type, principal_name,
			&result) == 0);
}

int
daos_acl_add_ace(struct daos_acl **acl, struct daos_ace *new_ace)
{
	uint32_t	new_len;
	ssize_t		new_ace_len;
	struct daos_acl	*new_acl;

	if (acl == NULL || *acl == NULL) {
		return -DER_INVAL;
	}

	new_ace_len = daos_ace_get_size(new_ace);
	if (new_ace_len < 0) {
		/* ACE was invalid */
		return -DER_INVAL;
	}

	if (acl_already_has_principal(*acl, new_ace->dae_principal_type,
			new_ace->dae_principal)) {
		overwrite_ace_for_principal(*acl, new_ace);
		return 0;
	}

	new_len = (*acl)->dal_len + new_ace_len;

	D_ALLOC(new_acl, get_total_acl_size(new_len));
	if (new_acl == NULL) {
		return -DER_NOMEM;
	}

	new_acl->dal_ver = (*acl)->dal_ver;
	new_acl->dal_len = new_len;

	copy_acl_with_new_ace_inserted(*acl, new_acl, new_ace);

	daos_acl_free(*acl);
	*acl = new_acl;

	return 0;
}

static bool
type_is_valid(enum daos_acl_principal_type type)
{
	bool result;

	switch (type) {
	case DAOS_ACL_USER:
	case DAOS_ACL_GROUP:
	case DAOS_ACL_OWNER:
	case DAOS_ACL_OWNER_GROUP:
	case DAOS_ACL_EVERYONE:
		result = true;
		break;

	case NUM_DAOS_ACL_TYPES:
	default:
		result = false;
		break;
	}

	return result;
}

static bool
type_needs_name(enum daos_acl_principal_type type)
{
	/*
	 * The only ACE types that require a name are User and Group. All others
	 * are "special" ACEs that apply to an abstract category.
	 */
	if (type == DAOS_ACL_USER || type == DAOS_ACL_GROUP) {
		return true;
	}

	return false;
}

static bool
principal_meets_type_requirements(enum daos_acl_principal_type type,
				  const char *principal_name)
{
	return	(!type_needs_name(type) ||
		(principal_name != NULL && strlen(principal_name) != 0));
}

int
daos_acl_remove_ace(struct daos_acl **acl,
		    enum daos_acl_principal_type type,
		    const char *principal_name)
{
	struct daos_ace	*current;
	struct daos_ace	*ace_to_remove;
	struct daos_acl	*new_acl;
	uint32_t	new_len;
	uint8_t		*pen;
	int		rc;

	if (acl == NULL || *acl == NULL || !type_is_valid(type) ||
		!principal_meets_type_requirements(type, principal_name)) {
		return -DER_INVAL;
	}

	rc = daos_acl_get_ace_for_principal(*acl, type,
			principal_name, &ace_to_remove);
	if (rc != 0) {
		/* requested principal not in the list */
		return rc;
	}

	new_len = (*acl)->dal_len - daos_ace_get_size(ace_to_remove);

	D_ALLOC(new_acl, get_total_acl_size(new_len));
	if (new_acl == NULL) {
		return -DER_NOMEM;
	}

	new_acl->dal_len = new_len;
	new_acl->dal_ver = (*acl)->dal_ver;

	pen = new_acl->dal_ace;
	current = daos_acl_get_next_ace(*acl, NULL);
	while (current != NULL) {
		if (!ace_matches_principal(current, type, principal_name)) {
			pen = write_ace(current, pen);
		}

		current = daos_acl_get_next_ace(*acl, current);
	}

	daos_acl_free(*acl);
	*acl = new_acl;

	return 0;
}

static bool
is_in_ace_list(uint8_t *addr, struct daos_acl *acl)
{
	uint8_t *start_addr = acl->dal_ace;
	uint8_t *end_addr = acl->dal_ace + acl->dal_len;

	return addr >= start_addr && addr < end_addr;
}

struct daos_ace *
daos_acl_get_next_ace(struct daos_acl *acl, struct daos_ace *current_ace)
{
	size_t offset;

	if (acl == NULL) {
		return NULL;
	}

	/* requested the first ACE */
	if (current_ace == NULL && acl->dal_len > 0) {
		return (struct daos_ace *)acl->dal_ace;
	}

	/* already at/beyond the end */
	if (!is_in_ace_list((uint8_t *)current_ace, acl)) {
		return NULL;
	}

	/* there is no next item */
	D_ASSERT(current_ace != NULL);
	offset = sizeof(struct daos_ace) + current_ace->dae_principal_len;
	if (!is_in_ace_list((uint8_t *)current_ace + offset, acl)) {
		return NULL;
	}

	return (struct daos_ace *)((uint8_t *)current_ace + offset);
}

int
daos_acl_get_ace_for_principal(struct daos_acl *acl,
			       enum daos_acl_principal_type type,
			       const char *principal, struct daos_ace **ace)
{
	struct daos_ace *result;

	if (acl == NULL || ace == NULL || !type_is_valid(type) ||
	    (type_needs_name(type) && principal == NULL)) {
		return -DER_INVAL;
	}

	result = daos_acl_get_next_ace(acl, NULL);
	while (result != NULL) {
		if (result->dae_principal_type == type &&
			principal_name_matches_ace(result, principal)) {
			break;
		}

		result = daos_acl_get_next_ace(acl, result);
	}

	if (result == NULL) {
		return -DER_NONEXIST;
	}

	*ace = result;

	return 0;
}

void
daos_acl_dump(struct daos_acl *acl)
{
	struct daos_ace *current;

	printf("Access Control List:\n");
	if (acl == NULL) {
		printf("\tNULL\n");
		return;
	}

	printf("\tVersion: %hu\n", acl->dal_ver);
	printf("\tLength: %u\n", acl->dal_len);

	current = daos_acl_get_next_ace(acl, NULL);
	while (current != NULL) {
		daos_ace_dump(current, 1);

		current = daos_acl_get_next_ace(acl, current);
	}

}

/*
 * Internal structure for hash table entries
 */
struct ace_hash_entry {
	d_list_t	entry;
	int		refcount;
	struct daos_ace	*ace;
};

struct ace_hash_entry *
ace_hash_entry(d_list_t *rlink)
{
	return (struct ace_hash_entry *)container_of(rlink,
			struct ace_hash_entry, entry);
}

uint32_t
hash_ace_key_hash(struct d_hash_table *htable, const void *key,
		  unsigned int ksize)
{
	struct daos_ace			*ace = (struct daos_ace *)key;
	const char			*str_key;
	size_t				str_key_len;
	unsigned int			idx;

	str_key = daos_ace_get_principal_str(ace);
	str_key_len = strnlen(str_key, DAOS_ACL_MAX_PRINCIPAL_BUF_LEN);

	idx = d_hash_string_u32(str_key, str_key_len);
	return idx & ((1U << htable->ht_bits) - 1);
}

/*
 * Key comparison for hash table - Checks whether the principals match.
 * Body of the ACE doesn't need to match.
 */
bool
hash_ace_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct daos_ace		*ace;
	struct ace_hash_entry	*entry;

	entry = ace_hash_entry(rlink);
	ace = (struct daos_ace *)key;

	return principals_match(ace, entry->ace);
}

void
hash_ace_add_ref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ace_hash_entry *entry;

	entry = ace_hash_entry(rlink);
	entry->refcount++;
}

bool
hash_ace_dec_ref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ace_hash_entry *entry;

	entry = ace_hash_entry(rlink);
	entry->refcount--;

	return entry->refcount <= 0;
}

void
hash_ace_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ace_hash_entry *entry;

	entry = ace_hash_entry(rlink);
	D_FREE(entry);
}

/*
 * Checks if the given daos_ace is a duplicate of one already we've already
 * seen.
 */
static int
check_ace_is_duplicate(struct daos_ace *ace, struct d_hash_table *found_aces)
{
	struct ace_hash_entry	*entry;
	d_list_t		*link;
	int			rc;

	D_ALLOC_PTR(entry);
	if (entry == NULL) {
		D_ERROR("Failed to allocate hash table entry\n");
		return -DER_NOMEM;
	}

	D_INIT_LIST_HEAD(&entry->entry);
	entry->ace = ace;

	link = d_hash_rec_find(found_aces, ace, daos_ace_get_size(ace));
	if (link != NULL) { /* Duplicate */
		hash_ace_dec_ref(found_aces, link);
		D_FREE(entry);
		return -DER_INVAL;
	}

	rc = d_hash_rec_insert(found_aces, ace,
			daos_ace_get_size(ace),
			&entry->entry, true);
	if (rc != 0) {
		D_ERROR("Failed to insert new hash entry, rc="DF_RC"\n",
			DP_RC(rc));
		D_FREE(entry);
	}

	return rc;
}

/*
 * Walks the list of ACEs and checks them for validity.
 */
static int
validate_aces(struct daos_acl *acl)
{
	struct daos_ace		*current;
	int			last_type;
	int			rc;
	struct d_hash_table	found;
	d_hash_table_ops_t	ops = {
			.hop_key_hash = hash_ace_key_hash,
			.hop_key_cmp = hash_ace_key_cmp,
			.hop_rec_addref = hash_ace_add_ref,
			.hop_rec_decref = hash_ace_dec_ref,
			.hop_rec_free = hash_ace_free
	};

	last_type = -1;
	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
			8, NULL, &ops, &found);
	if (rc != 0) {
		D_ERROR("Failed to create hash table, rc="DF_RC"\n", DP_RC(rc));
		return rc;
	}

	current = daos_acl_get_next_ace(acl, NULL);
	while (current != NULL) {
		if (!daos_ace_is_valid(current)) {
			rc = -DER_INVAL;
			goto out;
		}

		/* Type order is in order of the enum */
		if (current->dae_principal_type < last_type) {
			rc = -DER_INVAL;
			goto out;
		}

		rc = check_ace_is_duplicate(current, &found);
		if (rc != 0) {
			goto out;
		}

		last_type = current->dae_principal_type;
		current = daos_acl_get_next_ace(acl, current);
	}

out:
	d_hash_table_destroy_inplace(&found, true);
	return rc;
}

int
daos_acl_validate(struct daos_acl *acl)
{
	int rc;

	if (acl == NULL) {
		return -DER_INVAL;
	}

	if (acl->dal_ver != DAOS_ACL_VERSION) {
		return -DER_INVAL;
	}

	if (acl->dal_len > 0 && (acl->dal_len < sizeof(struct daos_ace) ||
				(acl->dal_len > DAOS_ACL_MAX_ACE_LEN))) {
		D_ERROR("invalid dal_len %d, should with in [%zu, %d].\n",
			acl->dal_len, sizeof(struct daos_ace),
			DAOS_ACL_MAX_ACE_LEN);
		return -DER_INVAL;
	}

	/* overall structure must be 64-bit aligned */
	if (acl->dal_len % 8 != 0) {
		D_ERROR("invalid dal_len %d, not 8 bytes aligned.\n",
			acl->dal_len);
		return -DER_INVAL;
	}

	rc = validate_aces(acl);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

static bool
perms_valid_for_ace(struct daos_ace *ace, uint64_t valid_perms)
{
	if ((ace->dae_allow_perms & ~valid_perms) ||
	    (ace->dae_audit_perms & ~valid_perms) ||
	    (ace->dae_alarm_perms & ~valid_perms))
		return false;

	return true;
}

static int
validate_acl_with_special_perms(struct daos_acl *acl, uint64_t valid_perms)
{
	int		rc;
	struct daos_ace	*ace;

	rc = daos_acl_validate(acl);
	if (rc != 0)
		return rc;

	ace = daos_acl_get_next_ace(acl, NULL);
	while (ace != NULL) {
		if (!perms_valid_for_ace(ace, valid_perms))
			return -DER_INVAL;

		ace = daos_acl_get_next_ace(acl, ace);
	}

	return 0;
}

int
daos_acl_pool_validate(struct daos_acl *acl)
{
	return validate_acl_with_special_perms(acl, DAOS_ACL_PERM_POOL_ALL);
}

int
daos_acl_cont_validate(struct daos_acl *acl)
{
	return validate_acl_with_special_perms(acl, DAOS_ACL_PERM_CONT_ALL);
}

static bool
type_is_group(enum daos_acl_principal_type type)
{
	if (type == DAOS_ACL_GROUP || type == DAOS_ACL_OWNER_GROUP) {
		return true;
	}

	return false;
}

struct daos_ace *
daos_ace_create(enum daos_acl_principal_type type, const char *principal_name)
{
	struct daos_ace	*ace;
	size_t		principal_array_len = 0;

	if (!type_is_valid(type)) {
		return NULL;
	}

	if (type_needs_name(type)) {
		if (principal_name == NULL || strlen(principal_name) == 0) {
			return NULL;
		}

		/* align to 64 bits */
		principal_array_len = D_ALIGNUP(strlen(principal_name) + 1, 8);
	}

	D_ALLOC(ace, sizeof(struct daos_ace) + principal_array_len);
	if (ace != NULL) {
		ace->dae_principal_type = type;
		ace->dae_principal_len = principal_array_len;
		strncpy(ace->dae_principal, principal_name,
				principal_array_len);

		if (type_is_group(type)) {
			ace->dae_access_flags |= DAOS_ACL_FLAG_GROUP;
		}
	}

	return ace;
}


void
daos_ace_free(struct daos_ace *ace)
{
	D_FREE(ace);
}

ssize_t
daos_ace_get_size(struct daos_ace *ace)
{
	if (ace == NULL) {
		return -DER_INVAL;
	}

	return sizeof(struct daos_ace) + ace->dae_principal_len;
}


static void
indent(uint32_t num_tabs)
{
	uint32_t i;

	for (i = 0; i < num_tabs; i++) {
		printf("\t");
	}
}

static const char *
get_principal_type_string(uint8_t type)
{
	switch (type) {
	case DAOS_ACL_OWNER:
		return "Owner";

	case DAOS_ACL_USER:
		return "User";

	case DAOS_ACL_OWNER_GROUP:
		return "Owner Group";

	case DAOS_ACL_GROUP:
		return "Group";

	case DAOS_ACL_EVERYONE:
		return "Everyone";

	default:
		break;
	}

	return "Unknown Principal Type";
}

static void
print_principal(uint32_t indent_tabs, struct daos_ace *ace)
{
	indent(indent_tabs);
	printf("Principal Type: %s (%hhu)\n",
		get_principal_type_string(ace->dae_principal_type),
		ace->dae_principal_type);

	indent(indent_tabs);
	printf("Principal Length: %hu\n", ace->dae_principal_len);

	if (ace->dae_principal_len > 0) {
		indent(indent_tabs);
		printf("Principal Name: \"%s\"\n", ace->dae_principal);
	}
}

static const char *
get_access_type_string(uint8_t type)
{
	switch (type) {
	case DAOS_ACL_ACCESS_ALLOW:
		return "Allow";

	case DAOS_ACL_ACCESS_AUDIT:
		return "Audit";

	case DAOS_ACL_ACCESS_ALARM:
		return "Alarm";

	default:
		break;
	}

	return "Unknown Access Type";
}

static void
print_access_type(uint32_t indent_tabs, uint8_t type)
{
	indent(indent_tabs);
	printf("%s (0x%hhx)\n", get_access_type_string(type), type);
}

static void
print_all_access_types(uint32_t indent_tabs, struct daos_ace *ace)
{
	int i;

	indent(indent_tabs);
	printf("Access Types:\n");

	if (ace->dae_access_types == 0) {
		indent(indent_tabs + 1);
		printf("None\n");
		return;
	}

	for (i = 0; i < 8; i++) {
		uint8_t type = (uint8_t)1 << i;

		if (ace->dae_access_types & type) {
			print_access_type(indent_tabs + 1, type);
		}
	}
}

static const char *
get_flag_string(uint16_t flag)
{
	switch (flag) {
	case DAOS_ACL_FLAG_POOL_INHERIT:
		return "Pool Inherit";

	case DAOS_ACL_FLAG_GROUP:
		return "Group";

	case DAOS_ACL_FLAG_ACCESS_SUCCESS:
		return "Access Success";

	case DAOS_ACL_FLAG_ACCESS_FAIL:
		return "Access Fail";

	default:
		break;
	}

	return "Unknown Flag";
}

static void
print_flag(uint32_t indent_tabs, uint16_t flag)
{
	indent(indent_tabs);
	printf("%s (0x%hx)\n", get_flag_string(flag), flag);
}

static void
print_all_flags(uint32_t indent_tabs, struct daos_ace *ace)
{
	int i;

	indent(indent_tabs);
	printf("Flags:\n");

	if (ace->dae_access_flags == 0) {
		indent(indent_tabs + 1);
		printf("None\n");
		return;
	}

	for (i = 0; i < 16; i++) {
		uint16_t flag = (uint16_t)1 << i;

		if (ace->dae_access_flags & flag) {
			print_flag(indent_tabs + 1, flag);
		}
	}
}

static const char *
get_perm_string(uint64_t perm)
{
	switch (perm) {
	case DAOS_ACL_PERM_READ:
		return "Read";

	case DAOS_ACL_PERM_WRITE:
		return "Write";

	case DAOS_ACL_PERM_CREATE_CONT:
		return "Create Container";

	case DAOS_ACL_PERM_DEL_CONT:
		return "Delete Container";

	case DAOS_ACL_PERM_GET_PROP:
		return "Get Prop";

	case DAOS_ACL_PERM_SET_PROP:
		return "Set Prop";

	case DAOS_ACL_PERM_GET_ACL:
		return "Get ACL";

	case DAOS_ACL_PERM_SET_ACL:
		return "Set ACL";

	case DAOS_ACL_PERM_SET_OWNER:
		return "Set Owner";

	default:
		break;
	}

	return "Unknown Permission";
}

static void
print_permissions(uint32_t indent_tabs, const char *name, uint64_t perms)
{
	int i;

	indent(indent_tabs);
	printf("%s Permissions:\n", name);

	if (perms == 0) {
		indent(indent_tabs + 1);
		printf("None\n");
		return;
	}

	for (i = 0; i < 64; i++) {
		uint64_t bit = (uint64_t)1 << i;

		if (perms & bit) {
			indent(indent_tabs + 1);
			printf("%s (%#lx)\n", get_perm_string(bit), bit);
		}
	}
}

static void
print_all_perm_types(uint32_t indent_tabs, struct daos_ace *ace)
{
	print_permissions(indent_tabs, "Allow", ace->dae_allow_perms);
	print_permissions(indent_tabs, "Audit", ace->dae_audit_perms);
	print_permissions(indent_tabs, "Alarm", ace->dae_alarm_perms);
}

void
daos_ace_dump(struct daos_ace *ace, uint32_t tabs)
{
	indent(tabs);
	printf("Access Control Entry:\n");

	if (ace == NULL) {
		indent(tabs + 1);
		printf("NULL\n");
		return;
	}

	print_principal(tabs + 1, ace);
	print_all_access_types(tabs + 1, ace);
	print_all_flags(tabs + 1, ace);
	print_all_perm_types(tabs + 1, ace);
}

static bool
principal_is_null_terminated(struct daos_ace *ace)
{
	uint16_t i;

	for (i = 0; i < ace->dae_principal_len; i++) {
		if (ace->dae_principal[i] == '\0') {
			return true;
		}
	}

	return false;
}

static uint64_t
get_permissions(struct daos_ace *ace, enum daos_acl_access_type type)
{
	switch (type) {
	case DAOS_ACL_ACCESS_ALLOW:
		return ace->dae_allow_perms;

	case DAOS_ACL_ACCESS_AUDIT:
		return ace->dae_audit_perms;

	case DAOS_ACL_ACCESS_ALARM:
		return ace->dae_alarm_perms;

	default:
		D_ASSERTF(false, "Invalid type %d", type);
		break;
	}

	return 0;
}

static bool
permissions_match_access_type(struct daos_ace *ace,
		enum daos_acl_access_type type)
{
	uint64_t perms;

	perms = get_permissions(ace, type);
	if (!(ace->dae_access_types & type) && (perms != 0)) {
		return false;
	}

	return true;
}

static bool
access_matches_flags(struct daos_ace *ace)
{
	uint16_t	alert_flags;
	uint8_t		alert_access_types;
	bool		is_alert_type;
	bool		has_flags;

	alert_flags = DAOS_ACL_FLAG_ACCESS_FAIL | DAOS_ACL_FLAG_ACCESS_SUCCESS;
	alert_access_types = DAOS_ACL_ACCESS_ALARM | DAOS_ACL_ACCESS_AUDIT;

	is_alert_type = (ace->dae_access_types & alert_access_types) != 0;
	has_flags = (ace->dae_access_flags & alert_flags) != 0;

	return (is_alert_type == has_flags);
}

bool
daos_ace_is_valid(struct daos_ace *ace)
{
	uint8_t		valid_types = DAOS_ACL_ACCESS_ALL;
	uint16_t	valid_flags = DAOS_ACL_FLAG_ALL;
	uint64_t	valid_perms = DAOS_ACL_PERM_ALL;
	bool		name_exists;
	bool		flag_exists;

	if (ace == NULL)
		return false;

	/* Check for invalid bits in bit fields */
	if (ace->dae_access_types & ~valid_types)
		return false;

	/* No access type defined */
	if (ace->dae_access_types == 0)
		return false;

	if (ace->dae_access_flags & ~valid_flags)
		return false;

	if (!perms_valid_for_ace(ace, valid_perms))
		return false;

	/* Name should only exist for types that require it */
	name_exists = ace->dae_principal_len != 0;
	if (type_needs_name(ace->dae_principal_type) != name_exists)
		return false;

	/* Only principal types that are groups should have the group flag */
	flag_exists = (ace->dae_access_flags & DAOS_ACL_FLAG_GROUP) != 0;
	if (type_is_group(ace->dae_principal_type) != flag_exists)
		return false;

	/* overall structure must be kept 64-bit aligned */
	if (ace->dae_principal_len % 8 != 0)
		return false;

	if (ace->dae_principal_len > 0 && !principal_is_null_terminated(ace))
		return false;

	if (ace->dae_principal_len > 0 &&
	    !daos_acl_principal_is_valid(ace->dae_principal))
		return false;

	if (!permissions_match_access_type(ace, DAOS_ACL_ACCESS_ALLOW) ||
	    !permissions_match_access_type(ace, DAOS_ACL_ACCESS_AUDIT) ||
	    !permissions_match_access_type(ace, DAOS_ACL_ACCESS_ALARM))
		return false;

	if (!access_matches_flags(ace))
		return false;

	return true;
}
