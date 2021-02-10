/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the ACL validity checking
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_types.h>
#include <daos_security.h>
#include <daos_errno.h>
#include <daos/test_utils.h>
#include <gurt/common.h>

static void
test_ace_is_valid_null(void **state)
{
	assert_false(daos_ace_is_valid(NULL));
}

static void
expect_ace_valid(enum daos_acl_principal_type type, const char *principal)
{
	struct daos_ace *ace;

	ace = daos_ace_create(type, principal);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;

	assert_true(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_valid_types(void **state)
{
	expect_ace_valid(DAOS_ACL_OWNER, NULL);
	expect_ace_valid(DAOS_ACL_USER, "myuser@");
	expect_ace_valid(DAOS_ACL_OWNER_GROUP, NULL);
	expect_ace_valid(DAOS_ACL_GROUP, "group@domain.tld");
	expect_ace_valid(DAOS_ACL_EVERYONE, NULL);
}

static void
test_ace_is_valid_invalid_owner(void **state)
{
	struct daos_ace *ace;

	/* Having a name for the owner is not valid */
	ace = daos_ace_create(DAOS_ACL_USER, "name@notwanted.tld");
	ace->dae_principal_type = DAOS_ACL_OWNER;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_invalid_user(void **state)
{
	struct daos_ace *ace;

	/* Having a name for the user is required */
	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_principal_type = DAOS_ACL_USER;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_invalid_owner_group(void **state)
{
	struct daos_ace *ace;

	/* Having a name for the owner group is not valid */
	ace = daos_ace_create(DAOS_ACL_GROUP, "group@");
	ace->dae_principal_type = DAOS_ACL_OWNER_GROUP;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_invalid_group(void **state)
{
	struct daos_ace *ace;

	/* Having a name for the group is required */
	ace = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace->dae_principal_type = DAOS_ACL_GROUP;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_invalid_everyone(void **state)
{
	struct daos_ace *ace;

	/* Having a name for the owner is not valid */
	ace = daos_ace_create(DAOS_ACL_USER, "somejunk");
	ace->dae_principal_type = DAOS_ACL_EVERYONE;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
expect_ace_invalid_without_group_flag(enum daos_acl_principal_type type,
		const char *principal)
{
	struct daos_ace *ace;

	ace = daos_ace_create(type, principal);
	ace->dae_access_flags &= ~DAOS_ACL_FLAG_GROUP;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_group_needs_flag(void **state)
{
	expect_ace_invalid_without_group_flag(DAOS_ACL_GROUP, "mygroup");
	expect_ace_invalid_without_group_flag(DAOS_ACL_OWNER_GROUP, NULL);
}


static void
expect_ace_invalid_with_group_flag(enum daos_acl_principal_type type,
		const char *principal)
{
	struct daos_ace *ace;

	ace = daos_ace_create(type, principal);
	ace->dae_access_flags |= DAOS_ACL_FLAG_GROUP;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_non_group_needs_no_flag(void **state)
{
	expect_ace_invalid_with_group_flag(DAOS_ACL_OWNER, NULL);
	expect_ace_invalid_with_group_flag(DAOS_ACL_USER, "user@domain.tld");
	expect_ace_invalid_with_group_flag(DAOS_ACL_EVERYONE, NULL);
}

static void
test_ace_is_valid_principal_len_not_aligned(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_USER, "myuser@");
	ace->dae_principal_len = 9; /* bad - would expect aligned to 8 bytes */

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_principal_not_terminated(void **state)
{
	struct daos_ace	*ace;
	uint16_t	i;

	ace = daos_ace_create(DAOS_ACL_USER, "greatuser@greatdomain.tld");
	for (i = 0; i < ace->dae_principal_len; i++) {
		/* fill up whole array */
		ace->dae_principal[i] = 'a';
	}

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_undefined_flags(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_GROUP, "mygroup@");
	ace->dae_access_flags |= (1 << 15); /* nonexistent flag */

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_valid_flags(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_GROUP, "mygroup@");
	ace->dae_access_types = DAOS_ACL_ACCESS_AUDIT;
	ace->dae_access_flags |= DAOS_ACL_FLAG_ACCESS_FAIL |
			DAOS_ACL_FLAG_ACCESS_SUCCESS |
			DAOS_ACL_FLAG_POOL_INHERIT;

	assert_true(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static uint64_t *
get_permissions_field(struct daos_ace *ace, enum daos_acl_access_type type)
{
	switch (type) {
	case DAOS_ACL_ACCESS_ALLOW:
		return &ace->dae_allow_perms;

	case DAOS_ACL_ACCESS_AUDIT:
		return &ace->dae_audit_perms;

	case DAOS_ACL_ACCESS_ALARM:
		return &ace->dae_alarm_perms;

	default:
		break;
	}

	return NULL;
}

static void
expect_ace_invalid_with_bad_perms(enum daos_acl_access_type type)
{
	struct daos_ace	*ace;
	uint64_t	*perms;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types = type;

	perms = get_permissions_field(ace, type);
	assert_non_null(perms);
	*perms = (uint64_t)1 << 63;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_undefined_perms(void **state)
{
	expect_ace_invalid_with_bad_perms(DAOS_ACL_ACCESS_ALLOW);
	expect_ace_invalid_with_bad_perms(DAOS_ACL_ACCESS_AUDIT);
	expect_ace_invalid_with_bad_perms(DAOS_ACL_ACCESS_ALARM);
}

static void
expect_ace_valid_with_good_perms(enum daos_acl_access_type type)
{
	struct daos_ace	*ace;
	uint64_t	*perms;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types = type;
	if (type == DAOS_ACL_ACCESS_AUDIT || type == DAOS_ACL_ACCESS_ALARM) {
		ace->dae_access_flags |= DAOS_ACL_FLAG_ACCESS_SUCCESS;
	}

	perms = get_permissions_field(ace, type);
	assert_non_null(perms);
	*perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE |
		 DAOS_ACL_PERM_CREATE_CONT | DAOS_ACL_PERM_DEL_CONT |
		 DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_SET_PROP |
		 DAOS_ACL_PERM_GET_ACL | DAOS_ACL_PERM_SET_ACL |
		 DAOS_ACL_PERM_SET_OWNER;

	assert_true(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_valid_perms(void **state)
{
	expect_ace_valid_with_good_perms(DAOS_ACL_ACCESS_ALLOW);
	expect_ace_valid_with_good_perms(DAOS_ACL_ACCESS_AUDIT);
	expect_ace_valid_with_good_perms(DAOS_ACL_ACCESS_ALARM);
}

static void
test_ace_is_valid_undefined_access_type(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types |= (1 << 7); /* nonexistent type */

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_no_access_type(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types = 0;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_valid_access_types(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_flags = DAOS_ACL_FLAG_ACCESS_FAIL;
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_AUDIT |
			DAOS_ACL_ACCESS_ALARM;

	assert_true(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
expect_ace_invalid_when_perms_set_for_unset_type(
		enum daos_acl_access_type type)
{
	struct daos_ace *ace;
	uint64_t	*perms;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_flags = DAOS_ACL_FLAG_ACCESS_FAIL;
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_AUDIT |
			DAOS_ACL_ACCESS_ALARM;
	ace->dae_access_types &= ~type;

	perms = get_permissions_field(ace, type);
	assert_non_null(perms);
	*perms = DAOS_ACL_PERM_READ;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_perms_for_unset_type(void **state)
{
	expect_ace_invalid_when_perms_set_for_unset_type(DAOS_ACL_ACCESS_ALLOW);
	expect_ace_invalid_when_perms_set_for_unset_type(DAOS_ACL_ACCESS_AUDIT);
	expect_ace_invalid_when_perms_set_for_unset_type(DAOS_ACL_ACCESS_ALARM);
}

static void
expect_ace_invalid_with_flag_with_only_allow(enum daos_acl_flags flag)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_flags = flag;
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_audit_flags_with_only_allow(void **state)
{
	expect_ace_invalid_with_flag_with_only_allow(DAOS_ACL_FLAG_ACCESS_FAIL);
	expect_ace_invalid_with_flag_with_only_allow(
			DAOS_ACL_FLAG_ACCESS_SUCCESS);
}

static void
test_ace_is_valid_audit_without_flags(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_flags &= ~(DAOS_ACL_FLAG_ACCESS_FAIL |
					DAOS_ACL_FLAG_ACCESS_SUCCESS);
	ace->dae_access_types = DAOS_ACL_ACCESS_AUDIT;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_ace_is_valid_bad_principal(void **state)
{
	struct	daos_ace *ace;
	char	bad_username[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN + 1];
	size_t	i;

	memset(bad_username, 0, sizeof(bad_username));

	/* create a long principal string > principal max by 1 */
	for (i = 0; i < DAOS_ACL_MAX_PRINCIPAL_LEN; i++) {
		bad_username[i] = 'u';
	}
	bad_username[i] = '@'; /*properly formatted, just too long */

	ace = daos_ace_create(DAOS_ACL_USER, bad_username);

	/* set up with valid perms */
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;

	assert_false(daos_ace_is_valid(ace));

	daos_ace_free(ace);
}

static void
test_acl_is_valid_null(void **state)
{
	assert_int_equal(daos_acl_validate(NULL), -DER_INVAL);
}

static void
test_acl_is_valid_empty(void **state)
{
	struct daos_acl *acl;

	acl = daos_acl_create(NULL, 0);

	assert_int_equal(daos_acl_validate(acl), 0);

	daos_acl_free(acl);
}

static void
expect_acl_invalid_with_version(uint16_t version)
{
	struct daos_acl *acl;

	acl = daos_acl_create(NULL, 0);
	acl->dal_ver = version;

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
}

static void
test_acl_is_valid_bad_version(void **state)
{
	expect_acl_invalid_with_version(0);
	expect_acl_invalid_with_version(DAOS_ACL_VERSION + 1);
}

static void
test_acl_is_valid_len_too_small(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	acl = daos_acl_create(&ace, 1);
	acl->dal_len = sizeof(struct daos_ace) - 8; /* still aligned */

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	daos_ace_free(ace);
}

static void
test_acl_is_valid_len_unaligned(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	acl = daos_acl_create(&ace, 1);
	acl->dal_len = sizeof(struct daos_ace) + 1;

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	daos_ace_free(ace);
}

static void
test_acl_is_valid_one_invalid_ace(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace->dae_access_types = 1 << 7; /* invalid access type */
	acl = daos_acl_create(&ace, 1);

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	daos_ace_free(ace);
}

static void
test_acl_is_valid_valid_aces(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 3;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_int_equal(daos_acl_validate(acl), 0);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_is_valid_later_ace_invalid(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 3;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	ace[num_aces - 1]->dae_access_types = 1 << 7; /* invalid access type */
	acl = daos_acl_create(ace, num_aces);

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_is_valid_duplicate_ace_type(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 3;
	struct daos_ace *ace[num_aces];

	ace[0] = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace[1] = daos_ace_create(DAOS_ACL_USER, "user1@");
	ace[2] = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	acl = daos_acl_create(ace, num_aces);

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_is_valid_duplicate_user(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 3;
	struct daos_ace *ace[num_aces];

	ace[0] = daos_ace_create(DAOS_ACL_USER, "user1@");
	ace[1] = daos_ace_create(DAOS_ACL_USER, "anotheruser@");
	ace[2] = daos_ace_create(DAOS_ACL_USER, "user1@");
	/* Give the duplicate instance different perms */
	ace[2]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[2]->dae_allow_perms = DAOS_ACL_PERM_READ;
	acl = daos_acl_create(ace, num_aces);

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_is_valid_duplicate_group(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 3;
	struct daos_ace *ace[num_aces];

	ace[0] = daos_ace_create(DAOS_ACL_GROUP, "grp1@");
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, "anothergroup@");
	ace[2] = daos_ace_create(DAOS_ACL_GROUP, "grp1@");
	acl = daos_acl_create(ace, num_aces);

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static struct daos_acl *
acl_create_in_exact_order(struct daos_ace *ace[], size_t num_aces)
{
	struct daos_acl	*acl;
	uint8_t		*pen;
	size_t		i;

	acl = daos_acl_create(ace, num_aces);

	/* Create probably reordered our input - rewrite in exact order */
	pen = acl->dal_ace;
	for (i = 0; i < num_aces; i++) {
		ssize_t ace_len = daos_ace_get_size(ace[i]);

		assert_true(ace_len > 0);

		memcpy(pen, ace[i], ace_len);
		pen += ace_len;
	}

	return acl;
}

static bool
needs_name(enum daos_acl_principal_type type)
{
	return (type == DAOS_ACL_USER || type == DAOS_ACL_GROUP);
}

static void
expect_acl_invalid_bad_ordering(enum daos_acl_principal_type type1,
		enum daos_acl_principal_type type2)
{
	struct daos_acl *acl;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];
	const char	*name1 = NULL;
	const char	*name2 = NULL;

	if (needs_name(type1)) {
		name1 = "name1@";
	}

	if (needs_name(type2)) {
		name2 = "name2@";
	}

	ace[0] = daos_ace_create(type1, name1);
	ace[1] = daos_ace_create(type2, name2);
	acl = acl_create_in_exact_order(ace, num_aces);

	assert_int_equal(daos_acl_validate(acl), -DER_INVAL);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_is_valid_bad_ordering(void **state)
{
	expect_acl_invalid_bad_ordering(DAOS_ACL_USER, DAOS_ACL_OWNER);
	expect_acl_invalid_bad_ordering(DAOS_ACL_OWNER_GROUP, DAOS_ACL_USER);
	expect_acl_invalid_bad_ordering(DAOS_ACL_GROUP, DAOS_ACL_OWNER_GROUP);
	expect_acl_invalid_bad_ordering(DAOS_ACL_EVERYONE, DAOS_ACL_GROUP);
	expect_acl_invalid_bad_ordering(DAOS_ACL_EVERYONE, DAOS_ACL_OWNER);
}

static void
expect_acl_random_buffer_not_valid(void)
{
	size_t		bufsize, i;
	uint32_t	len;
	struct daos_acl	*random_acl;
	uint8_t		*buf;
	int		result;

	/* Limit the length to limit how much time we spend */
	len = (uint32_t)(rand() % UINT16_MAX);
	bufsize = sizeof(struct daos_acl) + len;
	D_ALLOC(buf, bufsize);
	assert_non_null(buf);

	for (i = 0; i < bufsize; i++) {
		buf[i] = rand() % UINT8_MAX;
	}

	/* Need to match the advertised len to the actual len */
	random_acl = (struct daos_acl *)buf;
	random_acl->dal_len = len;

	result = daos_acl_validate(random_acl);
	/*
	 * In theory it's possible (but unlikely) to run into a case where the
	 * random garbage represents something valid. Interesting to see what
	 * the content actually was.
	 */
	if (result == 0) {
		printf("Surprise! The random buffer was a valid ACL:\n");
		daos_acl_dump(random_acl);
	} else {
		assert_int_equal(result, -DER_INVAL);
	}

	D_FREE(buf);
}

static void
test_acl_random_buffer(void **state)
{
	int i;

	/* Fuzz test - random content */
	srand((unsigned int)time(NULL));

	for (i = 0; i < 500; i++) {
		expect_acl_random_buffer_not_valid();
	}
}

static void
test_acl_is_valid_for_pool_null(void **state)
{
	assert_int_equal(daos_acl_pool_validate(NULL), -DER_INVAL);
}

static struct daos_acl *
create_acl_with_type_perms(enum daos_acl_access_type type, uint64_t perms)
{
	struct daos_ace	*ace;
	uint64_t	*ace_perms;
	struct daos_acl	*acl;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	assert_non_null(ace);
	ace->dae_access_types = type;

	/* Need flags for audit/alarm types to come back as valid */
	if (type != DAOS_ACL_ACCESS_ALLOW)
		ace->dae_access_flags = DAOS_ACL_FLAG_ACCESS_SUCCESS;

	ace_perms = get_permissions_field(ace, type);
	assert_non_null(ace_perms);
	*ace_perms = perms;

	acl = daos_acl_create(&ace, 1);

	daos_ace_free(ace);
	return acl;
}

static void
expect_pool_acl_with_type_perms(enum daos_acl_access_type type,
				uint64_t perms, int exp_result)
{
	struct daos_acl	*acl;

	acl = create_acl_with_type_perms(type, perms);
	assert_non_null(acl);

	assert_int_equal(daos_acl_pool_validate(acl), exp_result);

	daos_acl_free(acl);
}

static void
expect_pool_acl_invalid_with_perms(uint64_t perms)
{
	expect_pool_acl_with_type_perms(DAOS_ACL_ACCESS_ALLOW, perms,
					-DER_INVAL);
	expect_pool_acl_with_type_perms(DAOS_ACL_ACCESS_AUDIT, perms,
					-DER_INVAL);
	expect_pool_acl_with_type_perms(DAOS_ACL_ACCESS_ALARM, perms,
					-DER_INVAL);
}

static void
test_acl_is_valid_for_pool_invalid_perms(void **state)
{
	expect_pool_acl_invalid_with_perms((uint64_t)-1);
	expect_pool_acl_invalid_with_perms(DAOS_ACL_PERM_GET_ACL);
	expect_pool_acl_invalid_with_perms(DAOS_ACL_PERM_SET_ACL);
	expect_pool_acl_invalid_with_perms(DAOS_ACL_PERM_SET_PROP);
	expect_pool_acl_invalid_with_perms(DAOS_ACL_PERM_SET_OWNER);
}

static void
expect_pool_acl_valid_with_perms(uint64_t perms)
{
	expect_pool_acl_with_type_perms(DAOS_ACL_ACCESS_ALLOW, perms, 0);
	expect_pool_acl_with_type_perms(DAOS_ACL_ACCESS_AUDIT, perms, 0);
	expect_pool_acl_with_type_perms(DAOS_ACL_ACCESS_ALARM, perms, 0);
}

static void
test_acl_is_valid_for_pool_good_perms(void **state)
{
	expect_pool_acl_valid_with_perms(DAOS_ACL_PERM_READ);
	expect_pool_acl_valid_with_perms(DAOS_ACL_PERM_GET_PROP);
	expect_pool_acl_valid_with_perms(DAOS_ACL_PERM_WRITE);
	expect_pool_acl_valid_with_perms(DAOS_ACL_PERM_CREATE_CONT);
	expect_pool_acl_valid_with_perms(DAOS_ACL_PERM_DEL_CONT);
}

static void
test_acl_is_valid_for_cont_null(void **state)
{
	assert_int_equal(daos_acl_cont_validate(NULL), -DER_INVAL);
}

static void
expect_cont_acl_with_type_perms(enum daos_acl_access_type type,
				uint64_t perms, int exp_result)
{
	struct daos_acl	*acl;

	acl = create_acl_with_type_perms(type, perms);
	assert_non_null(acl);

	assert_int_equal(daos_acl_cont_validate(acl), exp_result);

	daos_acl_free(acl);
}

static void
expect_cont_acl_invalid_with_perms(uint64_t perms)
{
	expect_cont_acl_with_type_perms(DAOS_ACL_ACCESS_ALLOW, perms,
					-DER_INVAL);
	expect_cont_acl_with_type_perms(DAOS_ACL_ACCESS_AUDIT, perms,
					-DER_INVAL);
	expect_cont_acl_with_type_perms(DAOS_ACL_ACCESS_ALARM, perms,
					-DER_INVAL);
}

static void
test_acl_is_valid_for_cont_invalid_perms(void **state)
{
	expect_cont_acl_invalid_with_perms((uint64_t)-1);
	expect_cont_acl_invalid_with_perms(DAOS_ACL_PERM_CREATE_CONT);
}

static void
expect_cont_acl_valid_with_perms(uint64_t perms)
{
	expect_cont_acl_with_type_perms(DAOS_ACL_ACCESS_ALLOW, perms, 0);
	expect_cont_acl_with_type_perms(DAOS_ACL_ACCESS_AUDIT, perms, 0);
	expect_cont_acl_with_type_perms(DAOS_ACL_ACCESS_ALARM, perms, 0);
}

static void
test_acl_is_valid_for_cont_good_perms(void **state)
{
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_READ);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_WRITE);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_DEL_CONT);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_GET_PROP);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_SET_PROP);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_GET_ACL);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_SET_ACL);
	expect_cont_acl_valid_with_perms(DAOS_ACL_PERM_SET_OWNER);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ace_is_valid_null),
		cmocka_unit_test(test_ace_is_valid_valid_types),
		cmocka_unit_test(test_ace_is_valid_invalid_owner),
		cmocka_unit_test(test_ace_is_valid_invalid_user),
		cmocka_unit_test(test_ace_is_valid_invalid_owner_group),
		cmocka_unit_test(test_ace_is_valid_invalid_group),
		cmocka_unit_test(test_ace_is_valid_invalid_everyone),
		cmocka_unit_test(test_ace_is_valid_group_needs_flag),
		cmocka_unit_test(test_ace_is_valid_non_group_needs_no_flag),
		cmocka_unit_test(test_ace_is_valid_principal_len_not_aligned),
		cmocka_unit_test(test_ace_is_valid_principal_not_terminated),
		cmocka_unit_test(test_ace_is_valid_undefined_flags),
		cmocka_unit_test(test_ace_is_valid_valid_flags),
		cmocka_unit_test(test_ace_is_valid_undefined_perms),
		cmocka_unit_test(test_ace_is_valid_valid_perms),
		cmocka_unit_test(test_ace_is_valid_undefined_access_type),
		cmocka_unit_test(test_ace_is_valid_no_access_type),
		cmocka_unit_test(test_ace_is_valid_valid_access_types),
		cmocka_unit_test(test_ace_is_valid_perms_for_unset_type),
		cmocka_unit_test(test_ace_is_valid_audit_flags_with_only_allow),
		cmocka_unit_test(test_ace_is_valid_audit_without_flags),
		cmocka_unit_test(test_ace_is_valid_bad_principal),
		cmocka_unit_test(test_acl_is_valid_null),
		cmocka_unit_test(test_acl_is_valid_empty),
		cmocka_unit_test(test_acl_is_valid_bad_version),
		cmocka_unit_test(test_acl_is_valid_len_too_small),
		cmocka_unit_test(test_acl_is_valid_len_unaligned),
		cmocka_unit_test(test_acl_is_valid_one_invalid_ace),
		cmocka_unit_test(test_acl_is_valid_valid_aces),
		cmocka_unit_test(test_acl_is_valid_later_ace_invalid),
		cmocka_unit_test(test_acl_is_valid_duplicate_ace_type),
		cmocka_unit_test(test_acl_is_valid_duplicate_user),
		cmocka_unit_test(test_acl_is_valid_duplicate_group),
		cmocka_unit_test(test_acl_is_valid_bad_ordering),
		cmocka_unit_test(test_acl_random_buffer),
		cmocka_unit_test(test_acl_is_valid_for_pool_null),
		cmocka_unit_test(test_acl_is_valid_for_pool_invalid_perms),
		cmocka_unit_test(test_acl_is_valid_for_pool_good_perms),
		cmocka_unit_test(test_acl_is_valid_for_cont_null),
		cmocka_unit_test(test_acl_is_valid_for_cont_invalid_perms),
		cmocka_unit_test(test_acl_is_valid_for_cont_good_perms),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
