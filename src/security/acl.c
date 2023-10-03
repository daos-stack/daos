/*
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/debug.h>
#include <daos_errno.h>
#include <daos_security.h>
#include "acl.h"

bool
is_ownership_valid(struct d_ownership *ownership)
{
	return (daos_acl_principal_is_valid(ownership->user) &&
		daos_acl_principal_is_valid(ownership->group));
}

static int
get_perms_for_principal(struct daos_acl *acl, enum daos_acl_principal_type type,
			const char *name, uint64_t *perms)
{
	struct daos_ace *ace;
	int		rc;

	D_DEBUG(DB_SEC, "Checking ACE for principal type %d\n", type);

	rc = daos_acl_get_ace_for_principal(acl, type, name, &ace);
	if (rc != 0)
		return rc;

	*perms = ace->dae_allow_perms;
	return 0;
}

static bool
acl_user_has_group(struct acl_user *user_info, const char *group)
{
	size_t i;

	for (i = 0; i < user_info->nr_groups; i++) {
		if (strncmp(user_info->groups[i], group, DAOS_ACL_MAX_PRINCIPAL_LEN) == 0)
			return true;
	}

	return false;
}

static int
add_perms_for_principal(struct daos_acl *acl, enum daos_acl_principal_type type,
			const char *name, uint64_t *perms)
{
	int		rc;
	struct daos_ace	*ace = NULL;

	rc = daos_acl_get_ace_for_principal(acl, type, name, &ace);
	if (rc == 0)
		*perms |= ace->dae_allow_perms;

	return rc;
}

static int
get_perms_for_groups(struct daos_acl *acl, struct d_ownership *ownership,
		     struct acl_user *user_info, uint64_t *perms)
{
	int		rc;
	int		i;
	uint64_t	grp_perms = 0;
	bool		found = false;

	/*
	 * Group permissions are a union of the permissions of all groups the
	 * user is a member of, including the owner group.
	 */

	if (acl_user_has_group(user_info, ownership->group)) {
		rc = add_perms_for_principal(acl, DAOS_ACL_OWNER_GROUP, NULL, &grp_perms);
		if (rc == 0)
			found = true;
	}

	for (i = 0; i < user_info->nr_groups; i++) {
		rc = add_perms_for_principal(acl, DAOS_ACL_GROUP, user_info->groups[i], &grp_perms);
		if (rc == 0)
			found = true;
	}

	if (found) {
		*perms = grp_perms;
		return 0;
	}

	return -DER_NONEXIST;
}

static bool
acl_user_is_owner(struct acl_user *user_info, struct d_ownership *ownership)
{
	return strncmp(user_info->user, ownership->user,
		       DAOS_ACL_MAX_PRINCIPAL_LEN) == 0;
}

static int
calculate_acl_perms(struct daos_acl *acl, struct d_ownership *ownership, struct acl_user *user_info,
		    uint64_t *perms)
{
	int rc;

	if (acl == NULL) {
		*perms = 0;
		return 0;
	}

	/* If this is the owner, and there's an owner entry... */
	if (acl_user_is_owner(user_info, ownership)) {
		rc = get_perms_for_principal(acl, DAOS_ACL_OWNER, NULL, perms);
		if (rc != -DER_NONEXIST)
			return rc;
	}

	/* didn't match the owner entry, try the user by name */
	rc = get_perms_for_principal(acl, DAOS_ACL_USER, user_info->user, perms);
	if (rc != -DER_NONEXIST)
		return rc;

	rc = get_perms_for_groups(acl, ownership, user_info, perms);
	if (rc != -DER_NONEXIST)
		return rc;

	/*
	 * No match found to any specific entry. If there is an Everyone entry,
	 * we can use the capas for that.
	 */
	rc = get_perms_for_principal(acl, DAOS_ACL_EVERYONE, NULL, perms);

	/* No match - default no capabilities */
	if (rc == -DER_NONEXIST) {
		*perms = 0;
		rc = 0;
	}

	return rc;
}

int
get_acl_permissions(struct daos_acl *acl, struct d_ownership *ownership, struct acl_user *user_info,
		    uint64_t min_owner_perms, uint64_t *perms, bool *is_owner)
{
	int rc;

	D_ASSERT(ownership != NULL);
	D_ASSERT(user_info != NULL);
	D_ASSERT(user_info->user != NULL);
	D_ASSERT(user_info->groups != NULL || user_info->nr_groups == 0);
	D_ASSERT(perms != NULL);

	rc = calculate_acl_perms(acl, ownership, user_info, perms);
	if (rc != 0)
		return rc;

	*is_owner = acl_user_is_owner(user_info, ownership);

	/* Owner may have certain implicit permissions */
	if (*is_owner)
		*perms |= min_owner_perms;

	return rc;
}
