/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "sec_test_util.h"

void
free_ace_list(struct daos_ace **aces, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		daos_ace_free(aces[i]);
}

struct daos_acl *
get_acl_with_perms(uint64_t owner_perms, uint64_t group_perms)
{
	struct daos_acl *acl;
	size_t		num_aces = 2;
	struct daos_ace *aces[num_aces];
	size_t		i;

	aces[0] = daos_ace_create(DAOS_ACL_OWNER, NULL);
	aces[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	aces[0]->dae_allow_perms = owner_perms;

	aces[1] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	aces[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	aces[1]->dae_allow_perms = group_perms;

	acl = daos_acl_create(aces, num_aces);

	for (i = 0; i < num_aces; i++) {
		daos_ace_free(aces[i]);
	}

	return acl;
}

struct daos_acl *
get_user_acl_with_perms(const char *user, uint64_t perms)
{
	struct daos_acl *acl;
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_USER, user);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = perms;

	acl = daos_acl_create(&ace, 1);

	daos_ace_free(ace);

	return acl;
}
