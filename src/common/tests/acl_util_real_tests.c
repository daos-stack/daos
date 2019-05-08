/*
 * (C) Copyright 2019 Intel Corporation.
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

/**
 * Integration tests for the ACL utilities
 */
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_security.h>
#include <gurt/common.h>
#include <stdio.h>
#include <unistd.h>
#include <grp.h>

static void
test_acl_euid_principal_conversion(void **state)
{
	uid_t	uid = geteuid();
	uid_t	result = 0;
	char	*name = NULL;

	printf("Converting UID %u to principal name...\n", uid);
	assert_int_equal(daos_acl_uid_to_principal(uid, &name), 0);
	assert_non_null(name);
	printf("Principal user name: '%s'\n", name);

	printf("Converting back to UID...\n");
	assert_int_equal(daos_acl_principal_to_uid(name, &result), 0);
	printf("Got UID %u\n", result);
	assert_int_equal(result, uid);

	D_FREE(name);
}

static void
verify_gid_principal_conversion(gid_t gid)
{
	gid_t	result = 0;
	char	*name = NULL;

	printf("Converting GID %u to principal name...\n", gid);
	assert_int_equal(daos_acl_gid_to_principal(gid, &name), 0);
	assert_non_null(name);
	printf("Principal group name: '%s'\n", name);

	printf("Converting back to GID...\n");
	assert_int_equal(daos_acl_principal_to_gid(name, &result), 0);
	printf("Got GID %u\n", result);
	assert_int_equal(result, gid);

	D_FREE(name);
}

static void
test_acl_egid_principal_conversion(void **state)
{
	verify_gid_principal_conversion(getegid());
}

static void
test_acl_all_gid_principal_conversion(void **state)
{
	struct group	*grp;
	size_t		max_iterations = 30; /* arbitrary limit */
	size_t		i = 0;

	/*
	 * Sanity check. Try out some real GIDs from groups on the system
	 * that aren't just the effective GID.
	 */
	setgrent();
	do {
		grp = getgrent();
		if (grp != NULL) {
			verify_gid_principal_conversion(grp->gr_gid);
		}
		i++;
	} while (grp != NULL && i < max_iterations);

	endgrent();
}

static void
test_acl_id_not_found(void **state)
{
	char	*name = NULL;

	assert_int_equal(daos_acl_uid_to_principal(-1, &name), -DER_NONEXIST);
	assert_null(name);

	assert_int_equal(daos_acl_gid_to_principal(-1, &name), -DER_NONEXIST);
	assert_null(name);
}

static void
test_acl_name_not_found(void **state)
{
	const char	*name = "completelyfictionaluser@";
	uid_t		uid;
	gid_t		gid;

	assert_int_equal(daos_acl_principal_to_uid(name, &uid), -DER_NONEXIST);
	assert_int_equal(daos_acl_principal_to_gid(name, &gid), -DER_NONEXIST);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_acl_euid_principal_conversion),
		cmocka_unit_test(test_acl_egid_principal_conversion),
		cmocka_unit_test(test_acl_all_gid_principal_conversion),
		cmocka_unit_test(test_acl_id_not_found),
		cmocka_unit_test(test_acl_name_not_found),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
