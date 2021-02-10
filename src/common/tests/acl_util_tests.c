/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos_errno.h>
#include <gurt/common.h>

#define TEST_DEFAULT_ACE_STR	"A::user@:rw"

/*
 * String conversion tests
 */

static void
expect_string_for_principal(enum daos_acl_principal_type type, const char *name,
			    const char *exp_str)
{
	struct daos_ace *ace;

	ace = daos_ace_create(type, name);
	assert_string_equal(daos_ace_get_principal_str(ace),
			    exp_str);
	daos_ace_free(ace);
}

static void
test_ace_get_principal_str(void **state)
{
	expect_string_for_principal(DAOS_ACL_OWNER, NULL,
				    DAOS_ACL_PRINCIPAL_OWNER);
	expect_string_for_principal(DAOS_ACL_OWNER_GROUP, NULL,
				    DAOS_ACL_PRINCIPAL_OWNER_GRP);
	expect_string_for_principal(DAOS_ACL_EVERYONE, NULL,
				    DAOS_ACL_PRINCIPAL_EVERYONE);
	expect_string_for_principal(DAOS_ACL_USER, "acl_user@",
				    "acl_user@");
	expect_string_for_principal(DAOS_ACL_GROUP, "acl_grp@",
				    "acl_grp@");
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
expect_perms_for_str(char *perms_str, uint64_t exp_perms)
{
	const char	*identity = "someuser@";
	char		ace_str[DAOS_ACL_MAX_ACE_STR_LEN];

	snprintf(ace_str, sizeof(ace_str), "A::%s:%s", identity, perms_str);

	check_ace_from_valid_str(ace_str,
				 DAOS_ACL_ACCESS_ALLOW,
				 DAOS_ACL_USER,
				 0,
				 exp_perms,
				 0, 0, identity);
}

static void
test_ace_from_str_perms(void **state)
{
	expect_perms_for_str("", 0);
	expect_perms_for_str("r", DAOS_ACL_PERM_READ);
	expect_perms_for_str("w", DAOS_ACL_PERM_WRITE);
	expect_perms_for_str("c", DAOS_ACL_PERM_CREATE_CONT);
	expect_perms_for_str("d", DAOS_ACL_PERM_DEL_CONT);
	expect_perms_for_str("t", DAOS_ACL_PERM_GET_PROP);
	expect_perms_for_str("T", DAOS_ACL_PERM_SET_PROP);
	expect_perms_for_str("a", DAOS_ACL_PERM_GET_ACL);
	expect_perms_for_str("A", DAOS_ACL_PERM_SET_ACL);
	expect_perms_for_str("o", DAOS_ACL_PERM_SET_OWNER);
	expect_perms_for_str("rwcdtTaAo", DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE |
					  DAOS_ACL_PERM_CREATE_CONT |
					  DAOS_ACL_PERM_DEL_CONT |
					  DAOS_ACL_PERM_GET_PROP |
					  DAOS_ACL_PERM_SET_PROP |
					  DAOS_ACL_PERM_GET_ACL |
					  DAOS_ACL_PERM_SET_ACL |
					  DAOS_ACL_PERM_SET_OWNER);
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
test_ace_from_str_principal_too_long(void **state)
{
	size_t		i;
	char		bad_username[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN + 1];
	char		input[DAOS_ACL_MAX_ACE_STR_LEN + 1];
	struct daos_ace	*ace = NULL;

	memset(bad_username, 0, sizeof(bad_username));

	/* create a long principal string > principal max by 1 */
	for (i = 0; i < DAOS_ACL_MAX_PRINCIPAL_LEN; i++) {
		bad_username[i] = 'u';
	}
	bad_username[i] = '@'; /* gotta be a properly formatted principal */

	snprintf(input, sizeof(input), "A::%s:rw", bad_username);

	/* Should interpret as invalid */
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
	struct daos_ace	*ace;
	char		buf[DAOS_ACL_MAX_ACE_STR_LEN];

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types = 0;

	assert_int_equal(daos_ace_to_str(ace, buf, sizeof(buf)), -DER_INVAL);

	daos_ace_free(ace);
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
			       DAOS_ACL_PERM_READ |
			       DAOS_ACL_PERM_WRITE |
			       DAOS_ACL_PERM_CREATE_CONT |
			       DAOS_ACL_PERM_DEL_CONT |
			       DAOS_ACL_PERM_GET_PROP |
			       DAOS_ACL_PERM_SET_PROP |
			       DAOS_ACL_PERM_GET_ACL |
			       DAOS_ACL_PERM_SET_ACL |
			       DAOS_ACL_PERM_SET_OWNER,
			       "L:F:EVERYONE@:rwcdtTaAo");
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
	check_ace_turns_back_to_same_str("U:S:OWNER@:rwcdtTaAo");
	check_ace_turns_back_to_same_str("A:G:GROUP@:rw");
	check_ace_turns_back_to_same_str("AUL:GS:somegroup@somedomain:rw");
	check_ace_turns_back_to_same_str("AL:F:user1@:r");
	check_ace_turns_back_to_same_str("A::user2@:");
	check_ace_turns_back_to_same_str("UL:F:EVERYONE@:rw");
}

static void
test_acl_from_strs_bad_input(void **state)
{
	struct daos_acl		*acl = NULL;
	static const char	*valid_aces[] = {"A::OWNER@:rw"};
	static const char	*garbage[] = {"ABCD:E:FGH:IJ"};
	/* duplicate entries aren't valid */
	static const char	*invalid_aces[2] = {"A::OWNER@:rw",
						    "A::OWNER@:rw"};

	assert_int_equal(daos_acl_from_strs(NULL, 1, &acl), -DER_INVAL);
	assert_int_equal(daos_acl_from_strs(valid_aces, 0, &acl),
			 -DER_INVAL);
	assert_int_equal(daos_acl_from_strs(valid_aces, 1, NULL),
			 -DER_INVAL);
	assert_int_equal(daos_acl_from_strs(garbage, 1, &acl), -DER_INVAL);
	assert_int_equal(daos_acl_from_strs(invalid_aces, 2, &acl), -DER_INVAL);
}

static void
test_acl_from_strs_success(void **state)
{
	struct daos_acl			*acl = NULL;
	static const char		*aces[] = {"A::OWNER@:rw",
						   "L:F:EVERYONE@:rw"};
	enum daos_acl_principal_type	expected[] = { DAOS_ACL_OWNER,
						       DAOS_ACL_EVERYONE };
	size_t				aces_nr = 2;
	size_t				actual_count = 0;
	struct daos_ace			*current;

	assert_int_equal(daos_acl_from_strs(aces, aces_nr, &acl), 0);

	assert_non_null(acl);

	current = daos_acl_get_next_ace(acl, NULL);
	while (current != NULL && actual_count < aces_nr) {
		assert_int_equal(current->dae_principal_type,
				 expected[actual_count]);

		actual_count++;
		current = daos_acl_get_next_ace(acl, current);
	}

	assert_int_equal(actual_count, aces_nr);
	assert_null(current);

	daos_acl_free(acl);
}

static void
test_acl_to_strs_bad_input(void **state)
{
	struct daos_acl	*acl;
	char		**result = NULL;
	size_t		len = 0;

	acl = daos_acl_create(NULL, 0); /* empty is valid */

	assert_int_equal(daos_acl_to_strs(NULL, &result, &len), -DER_INVAL);
	assert_int_equal(daos_acl_to_strs(acl, NULL, &len), -DER_INVAL);
	assert_int_equal(daos_acl_to_strs(acl, &result, NULL), -DER_INVAL);

	/* mess up the length so the ACL is invalid */
	acl->dal_len = 1;
	assert_int_equal(daos_acl_to_strs(acl, &result, &len), -DER_INVAL);

	daos_acl_free(acl);
}

static void
test_acl_to_strs_empty(void **state)
{
	struct daos_acl	*acl;
	char		**result = NULL;
	size_t		len = 0;

	acl = daos_acl_create(NULL, 0); /* empty is valid */

	assert_int_equal(daos_acl_to_strs(acl, &result, &len), 0);

	assert_null(result); /* no point in allocating if there's nothing */
	assert_int_equal(len, 0);

	daos_acl_free(acl);
}

static void
test_acl_to_strs_success(void **state)
{
	struct daos_acl	*acl;
	struct daos_ace	*ace = NULL;
	char		**result = NULL;
	size_t		len = 0;
	char		*expected_result[] = {"A::OWNER@:rw",
					      "A::user1@:rw",
					      "A:G:readers@:r"};
	size_t		expected_len = sizeof(expected_result) / sizeof(char *);
	size_t		i;

	/* Set up with direct conversion from expected results */
	acl = daos_acl_create(NULL, 0);
	for (i = 0; i < expected_len; i++) {
		assert_int_equal(daos_ace_from_str(expected_result[i], &ace),
				 0);
		daos_acl_add_ace(&acl, ace);

		daos_ace_free(ace);
		ace = NULL;
	}

	assert_int_equal(daos_acl_to_strs(acl, &result, &len), 0);

	assert_int_equal(len, expected_len);
	assert_non_null(result);

	for (i = 0; i < expected_len; i++) {
		assert_non_null(result[i]);
		assert_string_equal(result[i], expected_result[i]);
	}

	/* Free up dynamically allocated strings */
	for (i = 0; i < len; i++) {
		D_FREE(result[i]);
	}
	D_FREE(result);
	daos_acl_free(acl);
}

static void
test_ace_str_to_verbose_invalid(void **state)
{
	char result[DAOS_ACL_MAX_ACE_STR_LEN];

	printf("NULL ACE string\n");
	assert_int_equal(daos_ace_str_get_verbose(NULL, result, sizeof(result)),
			 -DER_INVAL);

	printf("NULL result buffer\n");
	assert_int_equal(daos_ace_str_get_verbose(TEST_DEFAULT_ACE_STR,
						 NULL, sizeof(result)),
			 -DER_INVAL);

	printf("Buffer size == 0\n");
	assert_int_equal(daos_ace_str_get_verbose(TEST_DEFAULT_ACE_STR,
						 result, 0),
			 -DER_INVAL);

	printf("Empty ACE string\n");
	assert_int_equal(daos_ace_str_get_verbose("", result, sizeof(result)),
			 -DER_INVAL);

	printf("Not an ACE string\n");
	assert_int_equal(daos_ace_str_get_verbose("AAa", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Bad access type\n");
	assert_int_equal(daos_ace_str_get_verbose("oA::OWNER@:rw", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("No access type\n");
	assert_int_equal(daos_ace_str_get_verbose("::OWNER@:rw", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Bad flags\n");
	assert_int_equal(daos_ace_str_get_verbose("A:xyzG:GROUP@:rw", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Badly-formatted principal\n");
	assert_int_equal(daos_ace_str_get_verbose("A::nope:rw", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("No principal\n");
	assert_int_equal(daos_ace_str_get_verbose("A:::rw", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Bad permissions\n");
	assert_int_equal(daos_ace_str_get_verbose("A:G:GROUP@:rwxyz", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Truncated at access type\n");
	assert_int_equal(daos_ace_str_get_verbose("A", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Truncated at flags\n");
	assert_int_equal(daos_ace_str_get_verbose("A:G", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Truncated at principal\n");
	assert_int_equal(daos_ace_str_get_verbose("A:G:GROUP@", result,
						  sizeof(result)),
			 -DER_INVAL);

	printf("Too many colons\n");
	assert_int_equal(daos_ace_str_get_verbose("A:G:GROUP@:rw:", result,
						  sizeof(result)),
			 -DER_INVAL);
}

static void
expect_ace_str_to_verbose(const char *ace_str, const char *expected)
{
	char result[DAOS_ACL_MAX_ACE_STR_LEN];

	printf("Testing: '%s'\n", ace_str);
	assert_int_equal(daos_ace_str_get_verbose(ace_str, result,
						  sizeof(result)), 0);

	assert_string_equal(result, expected);
}

static void
test_ace_str_to_verbose_valid(void **state)
{
	/* Different principals */
	expect_ace_str_to_verbose("A::myuser@:r", "Allow::myuser@:Read");
	expect_ace_str_to_verbose("A:G:mygrp@:r", "Allow:Group:mygrp@:Read");
	expect_ace_str_to_verbose("A::OWNER@:r", "Allow::Owner:Read");
	expect_ace_str_to_verbose("A:G:GROUP@:r",
				  "Allow:Group:Owner-Group:Read");
	expect_ace_str_to_verbose("A::EVERYONE@:r", "Allow::Everyone:Read");

	/* Different access types/flags */
	expect_ace_str_to_verbose("U:S:myuser@:r",
				  "Audit:Access-Success:myuser@:Read");
	expect_ace_str_to_verbose("U:F:myuser@:r",
				  "Audit:Access-Failure:myuser@:Read");
	expect_ace_str_to_verbose("L:S:myuser@:r",
				  "Alarm:Access-Success:myuser@:Read");

	/* Combining access types */
	expect_ace_str_to_verbose("AL:S:myuser@:r",
				  "Allow/Alarm:Access-Success:myuser@:Read");

	/* Combining flags */
	expect_ace_str_to_verbose("L:GF:mygrp@:r",
				  "Alarm:Group/Access-Failure:mygrp@:Read");
	expect_ace_str_to_verbose("U:GS:mygrp@:r",
				  "Audit:Group/Access-Success:mygrp@:Read");

	/* Different perms */
	expect_ace_str_to_verbose("A::myuser@:w",
				  "Allow::myuser@:Write");
	expect_ace_str_to_verbose("A::myuser@:c",
				  "Allow::myuser@:Create-Container");
	expect_ace_str_to_verbose("A::myuser@:d",
				  "Allow::myuser@:Delete-Container");
	expect_ace_str_to_verbose("A::myuser@:t",
				  "Allow::myuser@:Get-Prop");
	expect_ace_str_to_verbose("A::myuser@:T",
				  "Allow::myuser@:Set-Prop");
	expect_ace_str_to_verbose("A::myuser@:a",
				  "Allow::myuser@:Get-ACL");
	expect_ace_str_to_verbose("A::myuser@:A",
				  "Allow::myuser@:Set-ACL");
	expect_ace_str_to_verbose("A::myuser@:o",
				  "Allow::myuser@:Set-Owner");

	/* Combine perms */
	expect_ace_str_to_verbose("A::myuser@:rwcdtTaAo",
				  "Allow::myuser@:Read/Write/Create-Container/"
				  "Delete-Container/Get-Prop/Set-Prop/Get-ACL/"
				  "Set-ACL/Set-Owner");

	/* No perms */
	expect_ace_str_to_verbose("A::myuser@:",
				  "Allow::myuser@:No-Access");
}

static void
test_ace_str_to_verbose_truncated(void **state)
{
	char result[DAOS_ACL_MAX_ACE_STR_LEN];

	assert_int_equal(daos_ace_str_get_verbose(TEST_DEFAULT_ACE_STR,
						  result, 4),
			 -DER_TRUNC);
	assert_string_equal(result, "All");

	assert_int_equal(daos_ace_str_get_verbose(TEST_DEFAULT_ACE_STR,
						  result, 7),
			 -DER_TRUNC);
	assert_string_equal(result, "Allow:");

	assert_int_equal(daos_ace_str_get_verbose(TEST_DEFAULT_ACE_STR,
						  result, 10),
			 -DER_TRUNC);
	assert_string_equal(result, "Allow::us");

	assert_int_equal(daos_ace_str_get_verbose(TEST_DEFAULT_ACE_STR,
						  result, 14),
			 -DER_TRUNC);
	assert_string_equal(result, "Allow::user@:");
}

static void
test_acl_to_stream_bad_stream(void **state)
{
	struct daos_acl *valid_acl = daos_acl_create(NULL, 0);

	assert_int_equal(daos_acl_to_stream(NULL, valid_acl, false),
			 -DER_INVAL);

	daos_acl_free(valid_acl);
}

static void
assert_stream_written(FILE *stream, const char *exp_str)
{
	char	result[DAOS_ACL_MAX_ACE_STR_LEN];
	char	*exp_str_mutable;
	char	*pch;

	/* Arbitrary max size - these are just tests, after all */
	D_STRNDUP(exp_str_mutable, exp_str, DAOS_ACL_MAX_ACE_STR_LEN * 5);
	assert_non_null(exp_str_mutable);

	rewind(stream);

	/* Check line by line */
	pch = strtok(exp_str_mutable, "\n");
	while (pch != NULL) {
		char *end;

		memset(result, 0, sizeof(result));
		assert_non_null(fgets(result, sizeof(result), stream));

		/* trim result to match the line from exp_str */
		end = strpbrk(result, "\n");
		assert_non_null(end);
		*end = '\0';

		assert_string_equal(result, pch);

		pch = strtok(NULL, "\n");
	}

	/* Should be no more output in stream */
	assert_null(fgets(result, sizeof(result), stream));

	D_FREE(exp_str_mutable);
}

static void
add_ace_allow(struct daos_acl **acl, enum daos_acl_principal_type type,
	      const char *principal, uint64_t perms)
{
	struct daos_ace *ace = daos_ace_create(type, principal);

	assert_non_null(ace);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = perms;
	assert_int_equal(daos_acl_add_ace(acl, ace), 0);

	daos_ace_free(ace);
}

static void
test_acl_to_stream_success(void **state)
{
	FILE		*tmpstream = tmpfile();
	struct daos_acl	*acl = daos_acl_create(NULL, 0);
	const char	*exp_empty_str = "# Entries:\n"
					 "#   None\n";

	assert_non_null(acl); /* sanity check */

	printf("= NULL ACL\n");
	assert_int_equal(daos_acl_to_stream(tmpstream, NULL, false), 0);
	assert_stream_written(tmpstream, exp_empty_str);

	rewind(tmpstream);

	printf("= Empty ACL\n");
	assert_int_equal(daos_acl_to_stream(tmpstream, acl, false), 0);
	assert_stream_written(tmpstream, exp_empty_str);

	rewind(tmpstream);

	printf("= Empty ACL (verbose)\n");
	assert_int_equal(daos_acl_to_stream(tmpstream, acl, true), 0);
	assert_stream_written(tmpstream, exp_empty_str);

	rewind(tmpstream);

	printf("= ACL with entries\n");
	add_ace_allow(&acl, DAOS_ACL_OWNER, NULL, DAOS_ACL_PERM_CONT_ALL);
	add_ace_allow(&acl, DAOS_ACL_GROUP, "readers@", DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_to_stream(tmpstream, acl, false), 0);
	assert_stream_written(tmpstream,
			      "# Entries:\n"
			      "A::OWNER@:rwdtTaAo\n"
			      "A:G:readers@:r\n");

	rewind(tmpstream);

	printf("= ACL with entries (verbose)\n");
	assert_int_equal(daos_acl_to_stream(tmpstream, acl, true), 0);
	assert_stream_written(tmpstream,
			      "# Entries:\n"
			      "# Allow::Owner:Read/Write/Delete-Container/"
			      "Get-Prop/Set-Prop/Get-ACL/Set-ACL/Set-Owner\n"
			      "A::OWNER@:rwdtTaAo\n"
			      "# Allow:Group:readers@:Read\n"
			      "A:G:readers@:r\n");
	fclose(tmpstream);

	daos_acl_free(acl);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ace_get_principal_str),
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
		cmocka_unit_test(test_ace_from_str_perms),
		cmocka_unit_test(test_ace_from_str_invalid_perms),
		cmocka_unit_test(test_ace_from_str_empty_str),
		cmocka_unit_test(test_ace_from_str_not_all_fields),
		cmocka_unit_test(test_ace_from_str_too_many_fields),
		cmocka_unit_test(test_ace_from_str_too_long),
		cmocka_unit_test(test_ace_from_str_principal_too_long),
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
		cmocka_unit_test(test_ace_from_str_and_back_again),
		cmocka_unit_test(test_acl_from_strs_bad_input),
		cmocka_unit_test(test_acl_from_strs_success),
		cmocka_unit_test(test_acl_to_strs_bad_input),
		cmocka_unit_test(test_acl_to_strs_empty),
		cmocka_unit_test(test_acl_to_strs_success),
		cmocka_unit_test(test_ace_str_to_verbose_invalid),
		cmocka_unit_test(test_ace_str_to_verbose_valid),
		cmocka_unit_test(test_ace_str_to_verbose_truncated),
		cmocka_unit_test(test_acl_to_stream_bad_stream),
		cmocka_unit_test(test_acl_to_stream_success),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef TEST_EXPECTED_BUF_SIZE
