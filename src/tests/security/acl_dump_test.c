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

/*
 * Utility to manually verify human-readable ACL dump
 * This doesn't do any output checking, it's just exercises the variety
 * of values that can come out of the daos_acl_dump() method.
 */

#include <stdio.h>
#include <stdbool.h>
#include <daos_security.h>
#include <daos_errno.h>
#include <gurt/common.h>

void
print_error(const char *message)
{
	fprintf(stderr, "ERROR: %s\n", message);
}

void
print_null_acl(void)
{
	printf("* NULL ACL\n");
	daos_acl_dump(NULL);
}

void
print_null_ace(void)
{
	printf("* NULL ACE\n");
	daos_ace_dump(NULL, 0);
}

int
print_empty_acl(void)
{
	struct daos_acl	*acl;

	printf("* Empty ACL\n");
	acl = daos_acl_create(NULL, 0);
	if (acl == NULL) {
		print_error("Failed to allocate ACL");
		return -DER_NOMEM;
	}

	daos_acl_dump(acl);

	daos_acl_free(acl);
	return 0;
}

int
print_valid_acl(void)
{
	struct daos_acl	*acl = NULL;
	struct daos_ace	*ace[NUM_DAOS_ACL_TYPES];
	int		rc = 0;
	int		i;

	memset(ace, 0, sizeof(ace));

	printf("* ACL with all types of entry\n");

	for (i = 0; i < NUM_DAOS_ACL_TYPES; i++) {
		const char *name = NULL;

		if (i == DAOS_ACL_USER) {
			name = "testuser@";
		} else if (i == DAOS_ACL_GROUP) {
			name = "testgroup@";
		}
		ace[i] = daos_ace_create(i, name);
		if (ace[i] == NULL) {
			print_error("Failed to allocate ACE");
			rc = -DER_NOMEM;
			goto cleanup;
		}
	}

	ace[DAOS_ACL_OWNER]->dae_access_flags |= DAOS_ACL_FLAG_POOL_INHERIT;
	ace[DAOS_ACL_OWNER]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[DAOS_ACL_OWNER]->dae_allow_perms = DAOS_ACL_PERM_READ |
			DAOS_ACL_PERM_WRITE;

	ace[DAOS_ACL_USER]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[DAOS_ACL_USER]->dae_allow_perms = DAOS_ACL_PERM_READ;

	ace[DAOS_ACL_OWNER_GROUP]->dae_access_flags |=
			DAOS_ACL_FLAG_POOL_INHERIT |
			DAOS_ACL_FLAG_ACCESS_SUCCESS;
	ace[DAOS_ACL_OWNER_GROUP]->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_AUDIT;
	ace[DAOS_ACL_OWNER_GROUP]->dae_allow_perms = DAOS_ACL_PERM_READ |
			DAOS_ACL_PERM_WRITE;
	ace[DAOS_ACL_OWNER_GROUP]->dae_audit_perms = DAOS_ACL_PERM_WRITE;

	ace[DAOS_ACL_GROUP]->dae_access_flags |=
			DAOS_ACL_FLAG_ACCESS_FAIL;
	ace[DAOS_ACL_GROUP]->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_ALARM;
	ace[DAOS_ACL_GROUP]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[DAOS_ACL_GROUP]->dae_alarm_perms = DAOS_ACL_PERM_WRITE;

	ace[DAOS_ACL_EVERYONE]->dae_access_flags |= DAOS_ACL_FLAG_POOL_INHERIT |
			DAOS_ACL_FLAG_ACCESS_FAIL;
	ace[DAOS_ACL_EVERYONE]->dae_access_types = DAOS_ACL_ACCESS_ALARM;
	ace[DAOS_ACL_EVERYONE]->dae_alarm_perms =
			DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;


	acl = daos_acl_create(ace, NUM_DAOS_ACL_TYPES);
	if (acl == NULL) {
		print_error("Failed to allocate ACL");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	daos_acl_dump(acl);

cleanup:
	daos_acl_free(acl);
	for (i = 0; i < NUM_DAOS_ACL_TYPES; i++) {
		daos_ace_free(ace[i]);
	}
	return rc;
}

int
print_invalid_acl(void)
{
	struct daos_acl	*acl;
	struct daos_ace	*ace;
	int		rc = 0;

	printf("* ACL with unknown values in ACE\n");

	ace = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	if (ace == NULL) {
		print_error("Failed to allocate ACE");
		return -DER_NOMEM;
	}

	/* Mangle it so all fields have some invalid types/bits */
	ace->dae_principal_type = NUM_DAOS_ACL_TYPES;
	ace->dae_access_flags |= DAOS_ACL_FLAG_POOL_INHERIT | (1 << 7);
	ace->dae_access_types = (1 << 7);
	ace->dae_allow_perms = DAOS_ACL_PERM_READ |
			DAOS_ACL_PERM_WRITE | ((uint64_t)1 << 60);
	ace->dae_audit_perms = DAOS_ACL_PERM_WRITE | ((uint64_t)1 << 35);
	ace->dae_alarm_perms = ((uint64_t)1 << 52) | ((uint64_t)1 << 44);


	acl = daos_acl_create(&ace, 1);
	if (acl == NULL) {
		print_error("Failed to allocate ACL");
		rc = -DER_NOMEM;
		goto cleanup;
	}

	daos_acl_dump(acl);

cleanup:
	daos_acl_free(acl);
	daos_ace_free(ace);
	return rc;
}

int
print_single_ace(void)
{
	struct daos_ace	*ace;

	printf("* Single valid ACE with no extra indentation\n");

	ace = daos_ace_create(DAOS_ACL_USER, "lovelyuser@lovelydomain.tld");
	if (ace == NULL) {
		print_error("Failed to allocate ACE");
		return -DER_NOMEM;
	}

	ace->dae_access_flags |= DAOS_ACL_FLAG_POOL_INHERIT;
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_AUDIT;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ |
			DAOS_ACL_PERM_WRITE;

	daos_ace_dump(ace, 0);

	daos_ace_free(ace);
	return 0;
}

int
main(int argc, char **argv)
{
	int rc;

	print_null_acl();
	print_null_ace();

	rc = print_empty_acl();
	if (rc != 0) {
		goto done;
	}

	rc = print_valid_acl();
	if (rc != 0) {
		goto done;
	}

	rc = print_invalid_acl();
	if (rc != 0) {
		goto done;
	}

	rc = print_single_ace();
	if (rc != 0) {
		goto done;
	}

done:
	printf("Done.\n");
	return rc;
}
