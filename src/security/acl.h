/*
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __SECURITY_ACL_H__
#define __SECURITY_ACL_H__

#include <stddef.h>
#include <stdint.h>
#include <daos/security.h>
#include <daos_security.h>

#define POOL_OWNER_MIN_PERMS	(0)
#define CONT_OWNER_MIN_PERMS	(DAOS_ACL_PERM_GET_ACL | DAOS_ACL_PERM_SET_ACL)

/**
 * Information about a specific user to be checked against ACLs.
 */
struct acl_user {
	char	*user;		/* username in ACL principal format */
	char	**groups;	/* group list in ACL principal format */
	size_t	nr_groups;
};

/**
 * Checks whether the ownership struct is populated with valid values.
 *
 * \param[in]	ownership	User/group ownership
 *
 * \return	true if valid, false otherwise
 */
bool
is_ownership_valid(struct d_ownership *ownership);

/**
 * Get the user's ACL permissions for a resource, based on username, primary group, and other
 * groups of which the user is a member.
 *
 * \param[in]	acl			Resource ACL to check the user's permissions with
 * \param[in]	ownership		Owner and group to which the resource belongs
 * \param[in]	user_info		User whose permissions we are getting
 * \param[in]	min_owner_perms		Special permissions granted to the owner, if any
 * \param[out]	permissions		The user's calculated permissions (DAOS_ACL_PERM_*)
 * \param[out]	is_owner		Whether the user is the owner
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 */
int
get_acl_permissions(struct daos_acl *acl, struct d_ownership *ownership, struct acl_user *user_info,
		    uint64_t min_owner_perms, uint64_t *permissions, bool *is_owner);

#endif
