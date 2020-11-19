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

/**
 * Unit tests for the ACL principal functions
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_security.h>
#include <daos_errno.h>
#include <gurt/common.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#define TEST_EXPECTED_BUF_SIZE	1024

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
 * Uid to principal conversion tests
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

/*
 * Gid to principal conversion tests
 */

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

/*
 * Principal validity tests
 */

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

/*
 * Principal to uid conversion tests
 */

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

/*
 * Principal to gid conversion tests
 */

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

/*
 * Tests for parsing a principal string
 */

static void
test_acl_principal_from_str_null_params(void **state)
{
	enum daos_acl_principal_type	type;
	char				*name;

	assert_int_equal(daos_acl_principal_from_str(NULL, &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("OWNER@", NULL, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("OWNER@", &type, NULL),
			 -DER_INVAL);
}

static void
expect_principal_str_is_special_type(const char *str,
				     enum daos_acl_principal_type exp_type)
{
	enum daos_acl_principal_type	type;
	char				*name;

	assert_int_equal(daos_acl_principal_from_str(str, &type, &name),
			 0);

	assert_int_equal(type, exp_type);
	assert_null(name);
}

static void
test_acl_principal_from_str_special(void **state)
{
	expect_principal_str_is_special_type("OWNER@", DAOS_ACL_OWNER);
	expect_principal_str_is_special_type("GROUP@", DAOS_ACL_OWNER_GROUP);
	expect_principal_str_is_special_type("EVERYONE@", DAOS_ACL_EVERYONE);
}

static void
expect_principal_str_is_named_type(const char *str,
				   enum daos_acl_principal_type exp_type,
				   const char *exp_name)
{
	enum daos_acl_principal_type	type;
	char				*name;

	assert_int_equal(daos_acl_principal_from_str(str, &type, &name),
			 0);

	assert_int_equal(type, exp_type);
	assert_non_null(name);
	assert_string_equal(name, exp_name);

	D_FREE(name);
}

static void
test_acl_principal_from_str_named(void **state)
{
	expect_principal_str_is_named_type("u:niceuser@", DAOS_ACL_USER,
					   "niceuser@");
	expect_principal_str_is_named_type("u:me@nicedomain", DAOS_ACL_USER,
					   "me@nicedomain");
	expect_principal_str_is_named_type("g:readers@", DAOS_ACL_GROUP,
					   "readers@");
	expect_principal_str_is_named_type("g:devs@bigcompany.com",
					   DAOS_ACL_GROUP,
					   "devs@bigcompany.com");
}

static void
test_acl_principal_from_str_bad_format(void **state)
{
	enum daos_acl_principal_type	type;
	char				*name;

	assert_int_equal(daos_acl_principal_from_str("", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("USER@", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("U:name@", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("G:name@", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("user:name@", &type,
			 &name), -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("group:name@", &type,
			 &name), -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("x:name@", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("name@", &type, &name),
			 -DER_INVAL);
}

static void
test_acl_principal_from_str_invalid_name(void **state)
{
	enum daos_acl_principal_type	type;
	char				*name;

	assert_int_equal(daos_acl_principal_from_str("u:", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("g:", &type, &name),
			 -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("u:name@name@", &type,
			 &name), -DER_INVAL);
	assert_int_equal(daos_acl_principal_from_str("u:name", &type,
			 &name), -DER_INVAL);
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
		cmocka_unit_test(test_acl_principal_from_str_null_params),
		cmocka_unit_test(test_acl_principal_from_str_special),
		cmocka_unit_test(test_acl_principal_from_str_named),
		cmocka_unit_test(test_acl_principal_from_str_bad_format),
		cmocka_unit_test(test_acl_principal_from_str_invalid_name),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
