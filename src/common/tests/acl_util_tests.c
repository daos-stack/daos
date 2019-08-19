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
 * Unit tests for the ACL utilities
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_types.h>
#include <daos_security.h>
#include <gurt/common.h>
#include <gurt/errno.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#define TEST_EXPECTED_BUF_SIZE	1024
#define TEST_DEFAULT_ACE_STR	"A::user@:rw"

/*
 * Mocks
 */

/* getpwuid_r mock */
static int		getpwuid_r_return;
static uid_t		getpwuid_r_uid;
static size_t		getpwuid_r_buflen;
static struct passwd	*getpwuid_r_result_return;
static int		getpwuid_r_num_erange_failures;
static int		getpwuid_r_call_count;
int
getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
	   struct passwd **result)
{
	getpwuid_r_call_count++;

	getpwuid_r_uid = uid;
	getpwuid_r_buflen = buflen;

	/*
	 * Simulate the case where the buflen is too small. Caller will want to
	 * retry.
	 */
	if (getpwuid_r_num_erange_failures > 0) {
		*result = NULL;
		getpwuid_r_num_erange_failures--;
		return ERANGE;
	}

	*result = getpwuid_r_result_return;

	return getpwuid_r_return;
}

static void
getpwuid_setup(struct passwd *passwd, int err)
{
	getpwuid_r_uid = 0;
	getpwuid_r_buflen = 0;
	getpwuid_r_result_return = passwd;
	getpwuid_r_return = err;
	getpwuid_r_num_erange_failures = 0;
	getpwuid_r_call_count = 0;
}

/* getpwnam_r mock */
static int		getpwnam_r_return;
static char		getpwnam_r_name[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
static size_t		getpwnam_r_buflen;
static struct passwd	*getpwnam_r_result_return;
static int		getpwnam_r_num_erange_failures;
static int		getpwnam_r_call_count;
int
getpwnam_r(const char *name, struct passwd *pwd,
	   char *buf, size_t buflen, struct passwd **result)
{
	getpwnam_r_call_count++;

	strncpy(getpwnam_r_name, name, DAOS_ACL_MAX_PRINCIPAL_LEN);
	getpwnam_r_buflen = buflen;

	/*
	 * Simulate the case where the buflen is too small. Caller will want to
	 * retry.
	 */
	if (getpwnam_r_num_erange_failures > 0) {
		*result = NULL;
		getpwnam_r_num_erange_failures--;
		return ERANGE;
	}

	*result = getpwnam_r_result_return;

	return getpwnam_r_return;
}

static void
getpwnam_setup(struct passwd *passwd, int err)
{
	getpwnam_r_buflen = 0;
	getpwnam_r_result_return = passwd;
	getpwnam_r_return = err;
	getpwnam_r_num_erange_failures = 0;
	getpwnam_r_call_count = 0;

	memset(getpwnam_r_name, 0, sizeof(getpwnam_r_name));
}

/* getgrgid_r mock */
static int		getgrgid_r_return;
static gid_t		getgrgid_r_gid;
static size_t		getgrgid_r_buflen;
static struct group	*getgrgid_r_result_return;
static int		getgrgid_r_num_erange_failures;
static int		getgrgid_r_call_count;
int
getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
	   struct group **result)
{
	getgrgid_r_call_count++;

	getgrgid_r_gid = gid;
	getgrgid_r_buflen = buflen;

	/*
	 * Simulate the case where the buflen is too small. Caller will want to
	 * retry.
	 */
	if (getgrgid_r_num_erange_failures > 0) {
		*result = NULL;
		getgrgid_r_num_erange_failures--;
		return ERANGE;
	}

	*result = getgrgid_r_result_return;

	return getgrgid_r_return;
}

static void
getgrgid_setup(struct group *grp, int err)
{
	getgrgid_r_gid = 0;
	getgrgid_r_buflen = 0;
	getgrgid_r_result_return = grp;
	getgrgid_r_return = err;
	getgrgid_r_num_erange_failures = 0;
	getgrgid_r_call_count = 0;
}

/* getgrnam_r mock */
static int		getgrnam_r_return;
static char		getgrnam_r_name[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
static size_t		getgrnam_r_buflen;
static struct group	*getgrnam_r_result_return;
static int		getgrnam_r_num_erange_failures;
static int		getgrnam_r_call_count;
int
getgrnam_r(const char *name, struct group *grp,
	   char *buf, size_t buflen, struct group **result)
{
	getgrnam_r_call_count++;

	strncpy(getgrnam_r_name, name, DAOS_ACL_MAX_PRINCIPAL_LEN);
	getgrnam_r_buflen = buflen;

	/*
	 * Simulate the case where the buflen is too small. Caller will want to
	 * retry.
	 */
	if (getgrnam_r_num_erange_failures > 0) {
		*result = NULL;
		getgrnam_r_num_erange_failures--;
		return ERANGE;
	}

	*result = getgrnam_r_result_return;

	return getgrnam_r_return;
}

static void
getgrnam_setup(struct group *grp, int err)
{
	getgrnam_r_buflen = 0;
	getgrnam_r_result_return = grp;
	getgrnam_r_return = err;
	getgrnam_r_num_erange_failures = 0;
	getgrnam_r_call_count = 0;

	memset(getgrnam_r_name, 0, sizeof(getgrnam_r_name));
}

/*
 * Helper functions
 */
struct passwd *
get_test_passwd(const char *principal_name, uid_t uid)
{
	struct passwd *passwd;

	D_ALLOC_PTR(passwd);

	/* system user name should be principal name minus the '@' */
	D_STRNDUP(passwd->pw_name, principal_name, strlen(principal_name) - 1);
	passwd->pw_uid = uid;

	return passwd;
}

void
free_test_passwd(struct passwd *passwd)
{
	D_FREE(passwd->pw_name);
	D_FREE(passwd);
}

struct group *
get_test_group(const char *principal_name, gid_t gid)
{
	struct group *grp;

	D_ALLOC_PTR(grp);

	/* system user name should be principal name minus the '@' */
	D_STRNDUP(grp->gr_name, principal_name, strlen(principal_name) - 1);
	grp->gr_gid = gid;

	return grp;
}

void
free_test_group(struct group *grp)
{
	D_FREE(grp->gr_name);
	D_FREE(grp);
}

/*
 * Tests
 */

static void
test_acl_uid_to_principal_null(void **state)
{
	assert_int_equal(daos_acl_uid_to_principal(2, NULL), -DER_INVAL);
}

static void
test_acl_uid_to_principal_bad_uid(void **state)
{
	char	*name = NULL;

	getpwuid_setup(NULL, 0);

	assert_int_equal(daos_acl_uid_to_principal(2, &name),
			 -DER_NONEXIST);

	assert_null(name);
	assert_int_equal(getpwuid_r_call_count, 1);
}

static void
test_acl_uid_to_principal_getpwuid_err(void **state)
{
	char		*name = NULL;

	getpwuid_setup(NULL, ENOMEM);

	assert_int_equal(daos_acl_uid_to_principal(2, &name),
			 -DER_NOMEM);

	assert_null(name);
	assert_int_equal(getpwuid_r_call_count, 1);
}

static void
test_acl_uid_to_principal_success(void **state)
{
	char		*name = NULL;
	const char	*expected_name = "myuser@";
	uid_t		uid = 5;
	struct passwd	*passwd;

	passwd = get_test_passwd(expected_name, uid);
	getpwuid_setup(passwd, 0);

	assert_int_equal(daos_acl_uid_to_principal(uid, &name),
			 0);

	assert_int_equal(getpwuid_r_call_count, 1);

	/* Verify params passed into getpwuid_r */
	assert_int_equal(getpwuid_r_uid, uid);
	assert_int_equal(getpwuid_r_buflen, TEST_EXPECTED_BUF_SIZE);

	assert_non_null(name);
	assert_string_equal(name, expected_name);

	free_test_passwd(passwd);
	D_FREE(name);
}

static void
test_acl_uid_to_principal_getpwuid_buf_too_small(void **state)
{
	char		*name = NULL;
	const char	*expected_name = "myuser@";
	uid_t		uid = 5;
	struct passwd	*passwd;

	/* Set it up to return success after the first round of buf len error */
	passwd = get_test_passwd(expected_name, uid);
	getpwuid_setup(passwd, 0);

	getpwuid_r_num_erange_failures = 1;

	assert_int_equal(daos_acl_uid_to_principal(uid, &name), 0);

	assert_non_null(name);
	assert_string_equal(name, expected_name);

	/* Should have called into the mock again after ERANGE failure */
	assert_int_equal(getpwuid_r_call_count, 2);
	/* Should have larger buffer size */
	assert_int_equal(getpwuid_r_buflen, TEST_EXPECTED_BUF_SIZE * 2);

	free_test_passwd(passwd);
	D_FREE(name);
}

static void
test_acl_gid_to_principal_null(void **state)
{
	assert_int_equal(daos_acl_gid_to_principal(1, NULL), -DER_INVAL);
}

static void
test_acl_gid_to_principal_bad_gid(void **state)
{
	char		*name = NULL;
	gid_t		gid = 1;

	getgrgid_setup(NULL, 0);

	assert_int_equal(daos_acl_gid_to_principal(gid, &name),
			 -DER_NONEXIST);

	assert_null(name);
}

static void
test_acl_gid_to_principal_getgrgid_err(void **state)
{
	char		*name = NULL;
	gid_t		gid = 1;

	getgrgid_setup(NULL, ENOMEM);

	assert_int_equal(daos_acl_gid_to_principal(gid, &name),
			 -DER_NOMEM);

	assert_null(name);
}

static void
test_acl_gid_to_principal_success(void **state)
{
	char		*name = NULL;
	const char	*expected_name = "wonderfulgroup@";
	gid_t		gid = 5;
	struct group	*grp;

	grp = get_test_group(expected_name, gid);
	getgrgid_setup(grp, 0);

	assert_int_equal(daos_acl_gid_to_principal(gid, &name),
			 0);

	/* Verify params passed into getgrgid_r */
	assert_int_equal(getgrgid_r_gid, gid);
	assert_int_equal(getgrgid_r_buflen, TEST_EXPECTED_BUF_SIZE);

	assert_non_null(name);
	assert_string_equal(name, expected_name);

	free_test_group(grp);
	D_FREE(name);
}

static void
test_acl_gid_to_principal_getgrgid_buf_too_small(void **state)
{
	char		*name = NULL;
	const char	*expected_name = "myuser@";
	gid_t		gid = 2;
	struct group	*grp;

	/* Set it up to succeed after the first round of buf len error */
	grp = get_test_group(expected_name, gid);
	getgrgid_setup(grp, 0);

	getgrgid_r_num_erange_failures = 1;

	assert_int_equal(daos_acl_gid_to_principal(gid, &name), 0);

	assert_non_null(name);
	assert_string_equal(name, expected_name);

	/* Should have called into the mock again after ERANGE failure */
	assert_int_equal(getgrgid_r_call_count, 2);
	/* Should have larger buffer size */
	assert_int_equal(getgrgid_r_buflen, TEST_EXPECTED_BUF_SIZE * 2);

	free_test_group(grp);
	D_FREE(name);
}

static void
test_acl_principal_is_valid_null(void **state)
{
	assert_false(daos_acl_principal_is_valid(NULL));
}

static void
test_acl_principal_is_valid_empty(void **state)
{
	assert_false(daos_acl_principal_is_valid(""));
}

static void
test_acl_principal_is_valid_too_long(void **state)
{
	size_t	len = DAOS_ACL_MAX_PRINCIPAL_LEN * 2;
	char	name[len];
	size_t	i;

	for (i = 0; i < len; i++) {
		name[i] = 'a';
	}
	name[DAOS_ACL_MAX_PRINCIPAL_LEN] = '@';
	name[len - 1] = '\0';

	assert_false(daos_acl_principal_is_valid(name));
}

static void
test_acl_principal_is_valid_good_names(void **state)
{
	assert_true(daos_acl_principal_is_valid("username@"));
	assert_true(daos_acl_principal_is_valid("user123@"));
	assert_true(daos_acl_principal_is_valid("group@domain"));
	assert_true(daos_acl_principal_is_valid("name2@domain.com"));
	assert_true(daos_acl_principal_is_valid("user_name@sub.domain2.tld"));
}

static void
test_acl_principal_is_valid_bad_names(void **state)
{
	assert_false(daos_acl_principal_is_valid("username"));
	assert_false(daos_acl_principal_is_valid("@domain"));
	assert_false(daos_acl_principal_is_valid("name@domain@"));
	assert_false(daos_acl_principal_is_valid("@domain@"));
	assert_false(daos_acl_principal_is_valid("12345"));
	assert_false(daos_acl_principal_is_valid("@"));
}

static void
test_acl_principal_to_uid_null_name(void **state)
{
	uid_t uid;

	assert_int_equal(daos_acl_principal_to_uid(NULL, &uid), -DER_INVAL);
}

static void
test_acl_principal_to_uid_null_uid(void **state)
{
	assert_int_equal(daos_acl_principal_to_uid("name@", NULL), -DER_INVAL);
}

static void
test_acl_principal_to_uid_invalid_name(void **state)
{
	uid_t uid = 0;

	assert_int_equal(daos_acl_principal_to_uid("", &uid), -DER_INVAL);
	assert_int_equal(daos_acl_principal_to_uid("@", &uid), -DER_INVAL);
	assert_int_equal(daos_acl_principal_to_uid("12345", &uid), -DER_INVAL);
}

static void
test_acl_principal_to_uid_success(void **state)
{
	uid_t		expected_uid = 15;
	uid_t		uid = 0;
	const char	*name = "specialuser@";
	struct passwd	*passwd;

	passwd = get_test_passwd(name, expected_uid);
	getpwnam_setup(passwd, 0);

	assert_int_equal(daos_acl_principal_to_uid(name, &uid), 0);

	assert_int_equal(uid, expected_uid);

	/* Verify getpwnam inputs */
	assert_int_equal(getpwnam_r_call_count, 1);
	assert_string_equal(getpwnam_r_name, "specialuser");
	assert_int_equal(getpwnam_r_buflen, TEST_EXPECTED_BUF_SIZE);

	free_test_passwd(passwd);
}

static void
test_acl_principal_to_uid_success_domain(void **state)
{
	uid_t		expected_uid = 12;
	uid_t		uid = 0;
	const char	*name = "user@domain";
	struct passwd	*passwd;

	passwd = get_test_passwd("user@", expected_uid);
	getpwnam_setup(passwd, 0);

	assert_int_equal(daos_acl_principal_to_uid(name, &uid), 0);

	assert_int_equal(uid, expected_uid);

	/* Verify name was parsed correctly */
	assert_int_equal(getpwnam_r_call_count, 1);
	assert_string_equal(getpwnam_r_name, "user");

	free_test_passwd(passwd);
}

static void
test_acl_principal_to_uid_not_found(void **state)
{
	uid_t	uid = 0;

	getpwnam_setup(NULL, 0);

	assert_int_equal(daos_acl_principal_to_uid("user@", &uid),
			 -DER_NONEXIST);

	assert_int_equal(getpwnam_r_call_count, 1);
}

static void
test_acl_principal_to_uid_getpwnam_err(void **state)
{
	uid_t	uid = 0;

	getpwnam_setup(NULL, ENOMEM);

	assert_int_equal(daos_acl_principal_to_uid("user@", &uid),
			 -DER_NOMEM);

	assert_int_equal(getpwnam_r_call_count, 1);
}

static void
test_acl_principal_to_uid_getpwnam_buf_too_small(void **state)
{
	uid_t		expected_uid = 15;
	uid_t		uid = 0;
	const char	*name = "specialuser@";
	struct passwd	*passwd;

	/* Set up to succeed after first pass of buffer too small */
	passwd = get_test_passwd(name, expected_uid);
	getpwnam_setup(passwd, 0);

	/* Cause one ERANGE error */
	getpwnam_r_num_erange_failures = 1;

	/* should recover from the failure and retry */
	assert_int_equal(daos_acl_principal_to_uid(name, &uid), 0);

	assert_int_equal(uid, expected_uid);

	/* Verify getpwnam was retried with larger buf */
	assert_int_equal(getpwnam_r_call_count, 2);
	assert_int_equal(getpwnam_r_buflen, TEST_EXPECTED_BUF_SIZE * 2);

	free_test_passwd(passwd);
}

static void
test_acl_principal_to_gid_null_name(void **state)
{
	gid_t gid;

	assert_int_equal(daos_acl_principal_to_gid(NULL, &gid), -DER_INVAL);
}

static void
test_acl_principal_to_gid_null_gid(void **state)
{
	assert_int_equal(daos_acl_principal_to_gid("grp@", NULL), -DER_INVAL);
}

static void
test_acl_principal_to_gid_invalid_name(void **state)
{
	gid_t gid = 0;

	assert_int_equal(daos_acl_principal_to_gid("", &gid), -DER_INVAL);
	assert_int_equal(daos_acl_principal_to_gid("@@", &gid), -DER_INVAL);
	assert_int_equal(daos_acl_principal_to_gid("grp", &gid), -DER_INVAL);
}

static void
test_acl_principal_to_gid_success(void **state)
{
	gid_t		expected_gid = 15;
	gid_t		gid = 0;
	const char	*name = "delightfulgroup@";
	struct group	*grp;

	grp = get_test_group(name, expected_gid);
	getgrnam_setup(grp, 0);

	assert_int_equal(daos_acl_principal_to_gid(name, &gid), 0);

	assert_int_equal(gid, expected_gid);

	/* Verify getgrnam inputs */
	assert_int_equal(getgrnam_r_call_count, 1);
	assert_string_equal(getgrnam_r_name, "delightfulgroup");
	assert_int_equal(getgrnam_r_buflen, TEST_EXPECTED_BUF_SIZE);

	free_test_group(grp);
}

static void
test_acl_principal_to_gid_success_domain(void **state)
{
	gid_t		expected_gid = 25;
	gid_t		gid = 0;
	const char	*name = "grp@domain";
	struct group	*grp;

	grp = get_test_group("grp@", expected_gid);
	getgrnam_setup(grp, 0);

	assert_int_equal(daos_acl_principal_to_gid(name, &gid), 0);

	assert_int_equal(gid, expected_gid);

	/* Verify name was parsed out correctly */
	assert_int_equal(getgrnam_r_call_count, 1);
	assert_string_equal(getgrnam_r_name, "grp");

	free_test_group(grp);
}

static void
test_acl_principal_to_gid_not_found(void **state)
{
	gid_t	gid = 0;

	getgrnam_setup(NULL, 0);

	assert_int_equal(daos_acl_principal_to_gid("group@", &gid),
			 -DER_NONEXIST);

	assert_int_equal(getgrnam_r_call_count, 1);
}

static void
test_acl_principal_to_gid_getgrnam_err(void **state)
{
	gid_t	gid = 0;

	getgrnam_setup(NULL, ENOMEM);

	assert_int_equal(daos_acl_principal_to_gid("group@", &gid),
			 -DER_NOMEM);

	assert_int_equal(getgrnam_r_call_count, 1);
}

static void
test_acl_principal_to_gid_getgrnam_buf_too_small(void **state)
{
	gid_t		expected_gid = 15;
	gid_t		gid = 0;
	const char	*name = "group@";
	struct group	*grp;

	/* Set up to succeed after first pass of buffer too small */
	grp = get_test_group(name, expected_gid);
	getgrnam_setup(grp, 0);

	/* Cause one ERANGE error */
	getgrnam_r_num_erange_failures = 1;

	/* should recover from the failure and retry */
	assert_int_equal(daos_acl_principal_to_gid(name, &gid), 0);

	assert_int_equal(gid, expected_gid);

	/* Verify getgrnam was retried with larger buf */
	assert_int_equal(getgrnam_r_call_count, 2);
	assert_int_equal(getgrnam_r_buflen, TEST_EXPECTED_BUF_SIZE * 2);

	free_test_group(grp);
}

static void
test_ace_from_str_null_str(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str(NULL, &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_null_ptr(void **state)
{
	assert_int_equal(daos_ace_from_str(TEST_DEFAULT_ACE_STR, NULL),
			 -DER_INVAL);
}

static void
check_ace_from_valid_str(const char *str, uint8_t access,
			 enum daos_acl_principal_type type,
			 uint16_t flags, uint64_t allow_perms,
			 uint64_t audit_perms, uint64_t alarm_perms,
			 const char *identity)
{
	struct daos_ace	*ace = NULL;
	size_t		exp_principal_len = 0;

	if (identity != NULL) {
		exp_principal_len = strnlen(identity,
					    DAOS_ACL_MAX_PRINCIPAL_LEN) + 1;
		exp_principal_len = D_ALIGNUP(exp_principal_len, 8);
	}

	assert_int_equal(daos_ace_from_str(str, &ace), 0);

	assert_non_null(ace);
	assert_int_equal(ace->dae_access_types, access);
	assert_int_equal(ace->dae_principal_type, type);
	assert_int_equal(ace->dae_access_flags, flags);
	assert_int_equal(ace->dae_allow_perms, allow_perms);
	assert_int_equal(ace->dae_audit_perms, audit_perms);
	assert_int_equal(ace->dae_alarm_perms, alarm_perms);
	assert_int_equal(ace->dae_principal_len, exp_principal_len);

	if (identity != NULL)
		assert_string_equal(ace->dae_principal, identity);

	daos_ace_free(ace);
}

static void
test_ace_from_str_owner(void **state)
{
	check_ace_from_valid_str("A::OWNER@:rw",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_OWNER,
				 0,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, 0, NULL);
}

static void
test_ace_from_str_owner_group(void **state)
{
	check_ace_from_valid_str("A:G:GROUP@:rw",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_OWNER_GROUP,
				 DAOS_ACL_FLAG_GROUP,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, 0, NULL);
}

static void
test_ace_from_str_group_needs_flag(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("A::GROUP@:rw", &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_owner_is_not_group(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("A:G:OWNER@:rw", &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_everyone(void **state)
{
	check_ace_from_valid_str("A::EVERYONE@:rw",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_EVERYONE,
				 0,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, 0, NULL);
}

static void
test_ace_from_str_everyone_is_not_group(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("A:G:EVERYONE@:rw", &ace),
			 -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_user(void **state)
{
	check_ace_from_valid_str("A::someuser@:rw",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_USER,
				 0,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, 0, "someuser@");
}

static void
test_ace_from_str_group(void **state)
{
	check_ace_from_valid_str("A:G:somegrp@:rw",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_GROUP,
				 DAOS_ACL_FLAG_GROUP,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, 0, "somegrp@");
}

static void
test_ace_from_str_audit_access(void **state)
{
	check_ace_from_valid_str("U:S:someuser@:rw",
				 DAOS_ACL_ACCESS_AUDIT,
				 DAOS_ACL_USER,
				 DAOS_ACL_FLAG_ACCESS_SUCCESS,
				 0,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, "someuser@");
}

static void
test_ace_from_str_alarm_access(void **state)
{
	check_ace_from_valid_str("L:S:someuser@:rw",
				 DAOS_ACL_ACCESS_ALARM,
				 DAOS_ACL_USER,
				 DAOS_ACL_FLAG_ACCESS_SUCCESS,
				 0, 0,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 "someuser@");
}

static void
test_ace_from_str_multiple_access(void **state)
{
	uint64_t expected_perm = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;

	check_ace_from_valid_str("ALU:S:someuser@:rw",
				 DAOS_ACL_ACCESS_ALLOW | DAOS_ACL_ACCESS_AUDIT |
				 DAOS_ACL_ACCESS_ALARM,
				 DAOS_ACL_USER,
				 DAOS_ACL_FLAG_ACCESS_SUCCESS,
				 expected_perm,
				 expected_perm,
				 expected_perm,
				 "someuser@");
}

static void
test_ace_from_str_invalid_access(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("Ux:S:someuser@:rw", &ace),
			 -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_multiple_flags(void **state)
{
	check_ace_from_valid_str("U:SFGP:somegrp@:rw",
				 DAOS_ACL_ACCESS_AUDIT,
				 DAOS_ACL_GROUP,
				 DAOS_ACL_FLAG_ACCESS_SUCCESS |
				 DAOS_ACL_FLAG_ACCESS_FAIL |
				 DAOS_ACL_FLAG_GROUP |
				 DAOS_ACL_FLAG_POOL_INHERIT,
				 0,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 0, "somegrp@");
}

static void
test_ace_from_str_invalid_flags(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("U:SFbG:somegrp@:rw", &ace),
			 -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_perm_read_only(void **state)
{
	check_ace_from_valid_str("A::someuser@:r",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_USER,
				 0,
				 DAOS_ACL_PERM_READ,
				 0, 0, "someuser@");
}

static void
test_ace_from_str_perm_none(void **state)
{
	check_ace_from_valid_str("A::someuser@:",
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_USER,
				 0, 0, 0, 0, "someuser@");
}

static void
test_ace_from_str_invalid_perms(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("A::someuser@:rz", &ace),
			 -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_empty_str(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("", &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_not_all_fields(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("A::someuser@", &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_too_many_fields(void **state)
{
	struct daos_ace *ace = NULL;

	assert_int_equal(daos_ace_from_str("A::someuser@:rw:r", &ace),
			 -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_from_str_too_long(void **state)
{
	size_t		i;
	size_t		len = DAOS_ACL_MAX_ACE_STR_LEN * 2;
	char		input[len];
	struct daos_ace	*ace = NULL;

	i = snprintf(input, len, "AUL:SG:somelongergroupname@:");

	/* Pad out with the same permissions over and over... */
	for (; i < len; i++) {
		input[i] = 'r';
	}
	input[len - 1] = '\0';

	/* Ensure the overly-long string doesn't crash us */
	assert_int_equal(daos_ace_from_str(input, &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_ace_to_str_null_ace(void **state)
{
	char buf[DAOS_ACL_MAX_ACE_STR_LEN];

	assert_int_equal(daos_ace_to_str(NULL, buf, DAOS_ACL_MAX_ACE_STR_LEN),
			 -DER_INVAL);
}

static void
test_ace_to_str_null_buf(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);

	assert_int_equal(daos_ace_to_str(ace, NULL, DAOS_ACL_MAX_ACE_STR_LEN),
			 -DER_INVAL);

	daos_ace_free(ace);
}

static void
test_ace_to_str_zero_len_buf(void **state)
{
	struct daos_ace	*ace;
	char		buf[0];

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);

	assert_int_equal(daos_ace_to_str(ace, buf, 0), -DER_INVAL);

	daos_ace_free(ace);
}

static void
test_ace_to_str_invalid_ace(void **state)
{
	struct daos_ace	*ace;
	char		buf[DAOS_ACL_MAX_ACE_STR_LEN];

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_principal_len = 100; /* Owner shouldn't have principal name */

	assert_int_equal(daos_ace_to_str(ace, buf, sizeof(buf)), -DER_INVAL);

	daos_ace_free(ace);
}

static void
check_valid_ace_to_str(enum daos_acl_principal_type type, const char *principal,
		       uint8_t access_types, uint16_t flags,
		       uint64_t allow_perms, uint64_t audit_perms,
		       uint64_t alarm_perms, const char *expected_str)
{
	struct daos_ace	*ace;
	char		buf[DAOS_ACL_MAX_ACE_STR_LEN];

	ace = daos_ace_create(type, principal);
	ace->dae_access_types = access_types;
	ace->dae_access_flags |= flags;
	ace->dae_allow_perms = allow_perms;
	ace->dae_audit_perms = audit_perms;
	ace->dae_alarm_perms = alarm_perms;

	assert_int_equal(daos_ace_to_str(ace, buf, sizeof(buf)), 0);

	assert_string_equal(buf, expected_str);

	daos_ace_free(ace);
}

static void
test_ace_to_str_owner(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_OWNER, NULL,
			       DAOS_ACL_ACCESS_ALLOW, 0,
			       DAOS_ACL_PERM_READ, 0, 0,
			       "A::OWNER@:r");
}

static void
test_ace_to_str_owner_group(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_OWNER_GROUP, NULL,
			       DAOS_ACL_ACCESS_ALLOW, 0,
			       DAOS_ACL_PERM_READ, 0, 0,
			       "A:G:GROUP@:r");
}

static void
test_ace_to_str_everyone(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_EVERYONE, NULL,
			       DAOS_ACL_ACCESS_ALLOW, 0,
			       DAOS_ACL_PERM_READ, 0, 0,
			       "A::EVERYONE@:r");
}

static void
test_ace_to_str_user(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_USER, "niceuser@domain",
			       DAOS_ACL_ACCESS_ALLOW, 0,
			       DAOS_ACL_PERM_READ, 0, 0,
			       "A::niceuser@domain:r");
}

static void
test_ace_to_str_group(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_GROUP, "nicegrp@",
			       DAOS_ACL_ACCESS_ALLOW, 0,
			       DAOS_ACL_PERM_READ, 0, 0,
			       "A:G:nicegrp@:r");
}

static void
test_ace_to_str_all_access_types(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_OWNER, NULL,
			       DAOS_ACL_ACCESS_ALLOW |
			       DAOS_ACL_ACCESS_AUDIT |
			       DAOS_ACL_ACCESS_ALARM,
			       DAOS_ACL_FLAG_ACCESS_SUCCESS,
			       DAOS_ACL_PERM_READ,
			       DAOS_ACL_PERM_READ,
			       DAOS_ACL_PERM_READ,
			       "AUL:S:OWNER@:r");
}

static void
test_ace_to_str_no_access_types(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_OWNER_GROUP, NULL,
			       0, 0, 0, 0, 0,
			       ":G:GROUP@:");
}

static void
test_ace_to_str_all_flags(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_OWNER_GROUP, NULL,
			       DAOS_ACL_ACCESS_AUDIT,
			       DAOS_ACL_FLAG_ACCESS_SUCCESS |
			       DAOS_ACL_FLAG_ACCESS_FAIL |
			       DAOS_ACL_FLAG_POOL_INHERIT,
			       0,
			       DAOS_ACL_PERM_READ,
			       0,
			       "U:GSFP:GROUP@:r");
}

static void
test_ace_to_str_all_perms(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_EVERYONE, NULL,
			       DAOS_ACL_ACCESS_ALARM,
			       DAOS_ACL_FLAG_ACCESS_FAIL,
			       0,
			       0,
			       DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
			       "L:F:EVERYONE@:rw");
}

static void
test_ace_to_str_no_perms(void **state)
{
	check_valid_ace_to_str(DAOS_ACL_EVERYONE, NULL,
			       DAOS_ACL_ACCESS_ALLOW,
			       0,
			       0,
			       0,
			       0,
			       "A::EVERYONE@:");
}

static void
check_ace_to_str_truncated_to_size(struct daos_ace *ace, char *buf,
		size_t buf_size, const char *expected_str)
{
	assert_int_equal(daos_ace_to_str(ace, buf, buf_size), -DER_TRUNC);

	assert_string_equal(buf, expected_str);

}

static void
test_ace_to_str_truncated(void **state)
{
	struct daos_ace	*ace;
	char		buf[64];

	/*
	 * Full string would be "A::someuser@:rw"
	 */
	ace = daos_ace_create(DAOS_ACL_USER, "someuser@");
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;

	check_ace_to_str_truncated_to_size(ace, buf, 1, "");
	check_ace_to_str_truncated_to_size(ace, buf, 2, "A");
	check_ace_to_str_truncated_to_size(ace, buf, 3, "A:");
	check_ace_to_str_truncated_to_size(ace, buf, 10, "A::someus");
	check_ace_to_str_truncated_to_size(ace, buf, 13, "A::someuser@");
	check_ace_to_str_truncated_to_size(ace, buf, 15, "A::someuser@:r");

	daos_ace_free(ace);
}

static void
check_ace_to_str_different_perms(uint64_t allow_perms, uint64_t audit_perms,
				 uint64_t alarm_perms)
{
	struct daos_ace	*ace;
	char		buf[DAOS_ACL_MAX_ACE_STR_LEN];

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
				DAOS_ACL_ACCESS_ALARM |
				DAOS_ACL_ACCESS_AUDIT;
	ace->dae_access_flags |= DAOS_ACL_FLAG_ACCESS_FAIL;
	ace->dae_allow_perms = allow_perms;
	ace->dae_audit_perms = audit_perms;
	ace->dae_alarm_perms = alarm_perms;

	assert_int_equal(daos_ace_to_str(ace, buf, sizeof(buf)), -DER_INVAL);

	daos_ace_free(ace);
}

/*
 * Impossible to format a string that has different perms for different access
 * types
 */
static void
test_ace_to_str_different_perms(void **state)
{
	check_ace_to_str_different_perms(DAOS_ACL_PERM_READ,
					 DAOS_ACL_PERM_READ |
					 DAOS_ACL_PERM_WRITE,
					 DAOS_ACL_PERM_READ);
	check_ace_to_str_different_perms(DAOS_ACL_PERM_READ,
					 DAOS_ACL_PERM_READ,
					 0);
	check_ace_to_str_different_perms(0,
					 DAOS_ACL_PERM_READ,
					 DAOS_ACL_PERM_READ);
}

static void
check_ace_turns_back_to_same_str(const char *ace_str)
{
	char		result[DAOS_ACL_MAX_ACE_STR_LEN];
	struct daos_ace	*ace = NULL;

	assert_int_equal(daos_ace_from_str(ace_str, &ace), 0);
	assert_non_null(ace);

	assert_int_equal(daos_ace_to_str(ace, result, sizeof(result)), 0);

	assert_string_equal(ace_str, result);

	daos_ace_free(ace);
}

static void
test_ace_from_str_and_back_again(void **state)
{
	check_ace_turns_back_to_same_str("U:S:OWNER@:w");
	check_ace_turns_back_to_same_str("A:G:GROUP@:rw");
	check_ace_turns_back_to_same_str("AUL:GS:somegroup@somedomain:rw");
	check_ace_turns_back_to_same_str("AL:F:user1@:r");
	check_ace_turns_back_to_same_str("A::user2@:");
	check_ace_turns_back_to_same_str("UL:F:EVERYONE@:rw");
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_acl_uid_to_principal_null),
		cmocka_unit_test(test_acl_uid_to_principal_bad_uid),
		cmocka_unit_test(test_acl_uid_to_principal_getpwuid_err),
		cmocka_unit_test(
			test_acl_uid_to_principal_getpwuid_buf_too_small),
		cmocka_unit_test(test_acl_uid_to_principal_success),
		cmocka_unit_test(test_acl_gid_to_principal_null),
		cmocka_unit_test(test_acl_gid_to_principal_bad_gid),
		cmocka_unit_test(test_acl_gid_to_principal_getgrgid_err),
		cmocka_unit_test(test_acl_gid_to_principal_success),
		cmocka_unit_test(
			test_acl_gid_to_principal_getgrgid_buf_too_small),
		cmocka_unit_test(test_acl_principal_is_valid_null),
		cmocka_unit_test(test_acl_principal_is_valid_empty),
		cmocka_unit_test(test_acl_principal_is_valid_too_long),
		cmocka_unit_test(test_acl_principal_is_valid_good_names),
		cmocka_unit_test(test_acl_principal_is_valid_bad_names),
		cmocka_unit_test(test_acl_principal_to_uid_null_name),
		cmocka_unit_test(test_acl_principal_to_uid_null_uid),
		cmocka_unit_test(test_acl_principal_to_uid_invalid_name),
		cmocka_unit_test(test_acl_principal_to_uid_success),
		cmocka_unit_test(test_acl_principal_to_uid_success_domain),
		cmocka_unit_test(test_acl_principal_to_uid_not_found),
		cmocka_unit_test(test_acl_principal_to_uid_getpwnam_err),
		cmocka_unit_test(
			test_acl_principal_to_uid_getpwnam_buf_too_small),
		cmocka_unit_test(test_acl_principal_to_gid_null_name),
		cmocka_unit_test(test_acl_principal_to_gid_null_gid),
		cmocka_unit_test(test_acl_principal_to_gid_invalid_name),
		cmocka_unit_test(test_acl_principal_to_gid_success),
		cmocka_unit_test(test_acl_principal_to_gid_success_domain),
		cmocka_unit_test(test_acl_principal_to_gid_not_found),
		cmocka_unit_test(test_acl_principal_to_gid_getgrnam_err),
		cmocka_unit_test(
			test_acl_principal_to_gid_getgrnam_buf_too_small),
		cmocka_unit_test(test_ace_from_str_null_str),
		cmocka_unit_test(test_ace_from_str_null_ptr),
		cmocka_unit_test(test_ace_from_str_owner),
		cmocka_unit_test(test_ace_from_str_owner_group),
		cmocka_unit_test(test_ace_from_str_group_needs_flag),
		cmocka_unit_test(test_ace_from_str_owner_is_not_group),
		cmocka_unit_test(test_ace_from_str_everyone),
		cmocka_unit_test(test_ace_from_str_everyone_is_not_group),
		cmocka_unit_test(test_ace_from_str_user),
		cmocka_unit_test(test_ace_from_str_group),
		cmocka_unit_test(test_ace_from_str_audit_access),
		cmocka_unit_test(test_ace_from_str_alarm_access),
		cmocka_unit_test(test_ace_from_str_multiple_access),
		cmocka_unit_test(test_ace_from_str_invalid_access),
		cmocka_unit_test(test_ace_from_str_multiple_flags),
		cmocka_unit_test(test_ace_from_str_invalid_flags),
		cmocka_unit_test(test_ace_from_str_perm_read_only),
		cmocka_unit_test(test_ace_from_str_perm_none),
		cmocka_unit_test(test_ace_from_str_invalid_perms),
		cmocka_unit_test(test_ace_from_str_empty_str),
		cmocka_unit_test(test_ace_from_str_not_all_fields),
		cmocka_unit_test(test_ace_from_str_too_many_fields),
		cmocka_unit_test(test_ace_from_str_too_long),
		cmocka_unit_test(test_ace_to_str_null_ace),
		cmocka_unit_test(test_ace_to_str_null_buf),
		cmocka_unit_test(test_ace_to_str_zero_len_buf),
		cmocka_unit_test(test_ace_to_str_invalid_ace),
		cmocka_unit_test(test_ace_to_str_owner),
		cmocka_unit_test(test_ace_to_str_owner_group),
		cmocka_unit_test(test_ace_to_str_everyone),
		cmocka_unit_test(test_ace_to_str_user),
		cmocka_unit_test(test_ace_to_str_group),
		cmocka_unit_test(test_ace_to_str_all_access_types),
		cmocka_unit_test(test_ace_to_str_no_access_types),
		cmocka_unit_test(test_ace_to_str_all_flags),
		cmocka_unit_test(test_ace_to_str_all_perms),
		cmocka_unit_test(test_ace_to_str_no_perms),
		cmocka_unit_test(test_ace_to_str_truncated),
		cmocka_unit_test(test_ace_to_str_different_perms),
		cmocka_unit_test(test_ace_from_str_and_back_again)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef TEST_EXPECTED_BUF_SIZE
