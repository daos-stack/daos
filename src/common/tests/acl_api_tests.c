/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the ACL property API
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/tests_lib.h>
#include <time.h>
#include <daos_types.h>
#include <daos_security.h>
#include <daos_errno.h>
#include <daos/test_utils.h>
#include <gurt/common.h>

/*
 * Utility helper methods
 */
static size_t
aligned_strlen(const char *str)
{
	size_t len = strlen(str) + 1;

	return D_ALIGNUP(len, 8);
}

/*
 * Tests
 */
static void
test_ace_alloc_principal_user(void **state)
{
	const char			expected_name[] = "user1@";
	enum daos_acl_principal_type	expected_type = DAOS_ACL_USER;
	struct daos_ace			*ace;

	ace = daos_ace_create(expected_type, expected_name);

	assert_non_null(ace);
	assert_int_equal(ace->dae_principal_type, expected_type);
	assert_int_equal(ace->dae_principal_len, aligned_strlen(expected_name));
	assert_string_equal(ace->dae_principal, expected_name);
	assert_false(ace->dae_access_flags & DAOS_ACL_FLAG_GROUP);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_user_no_name(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_USER, "");

	assert_null(ace);
}

static void
test_ace_alloc_principal_user_null_name(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_USER, NULL);

	assert_null(ace);
}

static void
test_ace_alloc_principal_group(void **state)
{
	const char			expected_name[] = "group1234@";
	enum daos_acl_principal_type	expected_type = DAOS_ACL_GROUP;
	struct daos_ace			*ace;

	ace = daos_ace_create(expected_type, expected_name);

	assert_non_null(ace);
	assert_int_equal(ace->dae_principal_type, expected_type);
	assert_int_equal(ace->dae_principal_len, aligned_strlen(expected_name));
	assert_string_equal(ace->dae_principal, expected_name);
	assert_true(ace->dae_access_flags & DAOS_ACL_FLAG_GROUP);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_group_no_name(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_GROUP, "");

	assert_null(ace);
}

static void
expect_valid_owner_ace(struct daos_ace *ace)
{
	assert_non_null(ace);
	assert_int_equal(ace->dae_principal_type, DAOS_ACL_OWNER);
	assert_int_equal(ace->dae_principal_len, 0);
	assert_false(ace->dae_access_flags & DAOS_ACL_FLAG_GROUP);
}

static void
test_ace_alloc_principal_owner(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, NULL);

	expect_valid_owner_ace(ace);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_owner_ignores_name(void **state)
{
	const char	name[] = "owner@";
	struct daos_ace	*ace;

	ace = daos_ace_create(DAOS_ACL_OWNER, name);

	expect_valid_owner_ace(ace);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_owner_group(void **state)
{
	enum daos_acl_principal_type	expected_type = DAOS_ACL_OWNER_GROUP;
	struct daos_ace			*ace;

	ace = daos_ace_create(expected_type, NULL);

	assert_non_null(ace);
	assert_int_equal(ace->dae_principal_type, expected_type);
	assert_int_equal(ace->dae_principal_len, 0);
	assert_true(ace->dae_access_flags & DAOS_ACL_FLAG_GROUP);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_everyone(void **state)
{
	enum daos_acl_principal_type	expected_type = DAOS_ACL_EVERYONE;
	struct daos_ace			*ace;

	ace = daos_ace_create(expected_type, NULL);

	assert_non_null(ace);
	assert_int_equal(ace->dae_principal_type, expected_type);
	assert_int_equal(ace->dae_principal_len, 0);
	assert_false(ace->dae_access_flags & DAOS_ACL_FLAG_GROUP);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_invalid(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_EVERYONE + 0xFF, "");

	assert_null(ace);
}

static void
test_ace_get_size_null(void **state)
{
	assert_int_equal(daos_ace_get_size(NULL), -DER_INVAL);
}

static void
test_ace_get_size_without_name(void **state)
{
	struct daos_ace	*ace;

	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	assert_int_equal(daos_ace_get_size(ace), sizeof(struct daos_ace));

	daos_ace_free(ace);
}

static void
test_ace_get_size_with_name(void **state)
{
	const char	name[] = "group1@";
	struct daos_ace	*ace;

	ace = daos_ace_create(DAOS_ACL_GROUP, name);

	/* name string rounded up to 64 bits */
	assert_int_equal(daos_ace_get_size(ace), sizeof(struct daos_ace) +
			aligned_strlen(name));

	daos_ace_free(ace);
}

static void
test_acl_alloc_empty(void **state)
{
	struct daos_acl *acl = daos_acl_create(NULL, 0);

	assert_non_null(acl);
	assert_int_equal(acl->dal_ver, 1);
	assert_int_equal(acl->dal_len, 0);

	daos_acl_free(acl);
}

static void
test_acl_alloc_one_user(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *ace[1];
	const char	name[] = "user1@";

	ace[0] = daos_ace_create(DAOS_ACL_USER, name);

	acl = daos_acl_create(ace, 1);

	assert_non_null(acl);
	assert_int_equal(acl->dal_ver, 1);
	assert_int_equal(acl->dal_len, daos_ace_get_size(ace[0]));
	assert_memory_equal(acl->dal_ace, ace[0], daos_ace_get_size(ace[0]));

	daos_ace_free(ace[0]);
	daos_acl_free(acl);
}

static ssize_t
get_total_ace_list_size(struct daos_ace *ace[], size_t num_aces)
{
	ssize_t ace_len = 0;
	int i;

	for (i = 0; i < num_aces; i++) {
		ace_len += daos_ace_get_size(ace[i]);
	}

	return ace_len;
}

static void
test_acl_alloc_two_users(void **state)
{
	struct daos_acl *acl;
	ssize_t		ace_len;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	ace_len = get_total_ace_list_size(ace, num_aces);

	acl = daos_acl_create(ace, num_aces);

	assert_non_null(acl);
	assert_int_equal(acl->dal_ver, 1);
	assert_int_equal(acl->dal_len, ace_len);
	/* expect the ACEs to be laid out in flat contiguous memory */
	assert_memory_equal(acl->dal_ace, ace[0], daos_ace_get_size(ace[0]));
	assert_memory_equal(acl->dal_ace + daos_ace_get_size(ace[0]),
			ace[1], daos_ace_get_size(ace[1]));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

/*
 * Assumes ACE array is allocated large enough for all types
 */
static void
fill_ace_list_with_all_types_shuffled(struct daos_ace *ace[],
		const char *user_name, const char *group_name)
{
	/* Shuffled order */
	ace[0] = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace[1] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace[2] = daos_ace_create(DAOS_ACL_USER, user_name);
	ace[3] = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace[4] = daos_ace_create(DAOS_ACL_GROUP, group_name);
}

static void
test_acl_alloc_type_order(void **state)
{
	struct daos_acl			*acl;
	int				i;
	ssize_t				ace_len = 0;
	size_t				num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace			*ace[num_aces];
	const char			group_name[] = "mygroup@";
	const char			user_name[] = "me@";
	struct daos_ace			*current_ace;
	enum daos_acl_principal_type	expected_order[] = {
			DAOS_ACL_OWNER,
			DAOS_ACL_USER,
			DAOS_ACL_OWNER_GROUP,
			DAOS_ACL_GROUP,
			DAOS_ACL_EVERYONE
	};

	fill_ace_list_with_all_types_shuffled(ace, user_name, group_name);
	ace_len = get_total_ace_list_size(ace, num_aces);

	acl = daos_acl_create(ace, num_aces);

	assert_non_null(acl);
	assert_int_equal(acl->dal_ver, 1);
	assert_int_equal(acl->dal_len, ace_len);

	/* expected order: Owner, User, Owner Group, Group, Everyone */
	current_ace = (struct daos_ace *)acl->dal_ace;
	for (i = 0; i < num_aces; i++) {
		uint32_t principal_len = current_ace->dae_principal_len;

		assert_int_equal(current_ace->dae_principal_type,
				expected_order[i]);

		current_ace = (struct daos_ace *)((uint8_t *)current_ace +
				sizeof(struct daos_ace) + principal_len);
	}

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_alloc_null_ace(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	ace[0] = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace[1] = NULL;

	acl = daos_acl_create(ace, num_aces);

	/* NULL entry is invalid input, don't do anything with it */
	assert_null(acl);

	/* cleanup */
	daos_ace_free(ace[0]);
}

static void
test_acl_copy_null_acl(void **state)
{
	assert_null(daos_acl_dup(NULL));
}

static void
test_acl_copy_empty_acl(void **state)
{
	struct daos_acl *acl = daos_acl_create(NULL, 0);
	struct daos_acl *copy;

	copy = daos_acl_dup(acl);

	assert_non_null(copy);
	assert_memory_equal(acl, copy, sizeof(struct daos_acl));

	daos_acl_free(acl);
	daos_acl_free(copy);
}

static void
test_acl_copy_with_aces(void **state)
{
	struct daos_acl	*acl;
	struct daos_acl	*copy;
	size_t		num_aces = 3;
	struct daos_ace	*ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	copy = daos_acl_dup(acl);

	assert_non_null(copy);
	assert_int_equal(copy->dal_len, acl->dal_len);
	assert_memory_equal(copy, acl, acl->dal_len);

	daos_acl_free(acl);
	daos_acl_free(copy);
	free_all_aces(ace, num_aces);
}

static void
test_acl_get_size_null(void **state)
{
	assert_int_equal(daos_acl_get_size(NULL), -DER_INVAL);
}

static void
test_acl_get_size_empty(void **state)
{
	struct daos_acl	*acl;

	acl = daos_acl_create(NULL, 0);

	assert_int_equal(daos_acl_get_size(acl), sizeof(struct daos_acl));

	daos_acl_free(acl);
}

static void
test_acl_get_size_with_aces(void **state)
{
	struct daos_acl	*acl;
	size_t		num_aces = 3;
	struct daos_ace	*ace[num_aces];
	size_t		expected_ace_len;

	fill_ace_list_with_users(ace, num_aces);
	expected_ace_len = get_total_ace_list_size(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_int_equal(daos_acl_get_size(acl),
			sizeof(struct daos_acl) + expected_ace_len);

	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_get_first_ace_empty_list(void **state)
{
	struct daos_acl *acl = daos_acl_create(NULL, 0);

	assert_null(daos_acl_get_next_ace(acl, NULL));

	daos_acl_free(acl);
}

static void
test_acl_get_first_ace_multiple(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);

	acl = daos_acl_create(ace, num_aces);

	result = daos_acl_get_next_ace(acl, NULL);

	assert_non_null(result);
	assert_ptr_equal(result, acl->dal_ace);
	assert_memory_equal(result, ace[0], daos_ace_get_size(ace[0]));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_next_ace_null_acl(void **state)
{
	struct daos_ace *ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	assert_null(daos_acl_get_next_ace(NULL, ace));

	daos_ace_free(ace);
}

static void
test_acl_get_next_ace_success(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);

	acl = daos_acl_create(ace, num_aces);

	result = daos_acl_get_next_ace(acl, (struct daos_ace *)acl->dal_ace);

	assert_non_null(result);
	assert_ptr_equal(result, acl->dal_ace + daos_ace_get_size(ace[0]));
	assert_memory_equal(result, ace[1], daos_ace_get_size(ace[1]));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_next_ace_last_item(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	struct daos_ace *last;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);

	acl = daos_acl_create(ace, num_aces);
	last = (struct daos_ace *)(acl->dal_ace + daos_ace_get_size(ace[0]));

	result = daos_acl_get_next_ace(acl, last);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_next_ace_empty(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;

	acl = daos_acl_create(NULL, 0);

	result = daos_acl_get_next_ace(acl, (struct daos_ace *)acl->dal_ace);

	assert_null(result);

	/* cleanup */
	daos_acl_free(acl);
}

static void
test_acl_get_next_ace_bad_ace(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	/* pass a value for current ACE outside of the ACE list */
	result = daos_acl_get_next_ace(acl,
			(struct daos_ace *)acl);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_null_acl(void **state)
{
	struct daos_ace *ace = NULL;

	assert_rc_equal(daos_acl_get_ace_for_principal(NULL, DAOS_ACL_USER,
			"user1@", &ace), -DER_INVAL);

	assert_null(ace);
}

static void
test_acl_get_ace_null_ace_ptr(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			"user1@", NULL), -DER_INVAL);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_invalid_type(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result = NULL;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	/* bad type */
	assert_rc_equal(daos_acl_get_ace_for_principal(acl,
			NUM_DAOS_ACL_TYPES,
			ace[0]->dae_principal, &result), -DER_INVAL);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_first_item(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result = NULL;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			ace[0]->dae_principal, &result), 0);

	assert_non_null(result);
	assert_ptr_equal(result, acl->dal_ace);
	assert_memory_equal(result, ace[0], daos_ace_get_size(result));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_later_item(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result = NULL;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			ace[1]->dae_principal, &result), 0);

	assert_non_null(result);
	assert_ptr_equal(result, acl->dal_ace + daos_ace_get_size(ace[0]));
	assert_memory_equal(result, ace[1], daos_ace_get_size(result));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_match_wrong_type(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result = NULL;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);

	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_GROUP,
			ace[0]->dae_principal, &result), -DER_NONEXIST);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_name_not_found(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result = NULL;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			"notinthelist", &result), -DER_NONEXIST);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_name_needed(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace *ace[num_aces];
	struct daos_ace *result = NULL;

	fill_ace_list_with_all_types_shuffled(ace, "user1@", "group1@");
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			NULL, &result), -DER_INVAL);
	assert_null(result);

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, DAOS_ACL_GROUP,
			NULL, &result), -DER_INVAL);
	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
expect_acl_get_ace_returns_type(struct daos_acl *acl,
		enum daos_acl_principal_type type)
{
	struct daos_ace	*result = NULL;

	assert_rc_equal(daos_acl_get_ace_for_principal(acl, type, NULL,
			&result), 0);

	assert_non_null(result);
	assert_int_equal(result->dae_principal_type, type);
}

static void
test_acl_get_ace_name_not_needed(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_all_types_shuffled(ace, "user1@", "group1@");
	acl = daos_acl_create(ace, num_aces);

	expect_acl_get_ace_returns_type(acl, DAOS_ACL_OWNER);
	expect_acl_get_ace_returns_type(acl, DAOS_ACL_OWNER_GROUP);
	expect_acl_get_ace_returns_type(acl, DAOS_ACL_EVERYONE);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_add_ace_with_null_acl_ptr(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	assert_rc_equal(daos_acl_add_ace(NULL, ace),
			-DER_INVAL);

	daos_ace_free(ace);
}

static void
test_acl_add_ace_with_null_acl(void **state)
{
	struct daos_ace *ace;
	struct daos_acl *acl = NULL;

	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	assert_rc_equal(daos_acl_add_ace(&acl, ace),
			-DER_INVAL);

	daos_ace_free(ace);
}

static void
test_acl_add_ace_with_null_ace(void **state)
{
	struct daos_acl *acl;

	acl = daos_acl_create(NULL, 0);

	assert_rc_equal(daos_acl_add_ace(&acl, NULL),
			-DER_INVAL);

	daos_acl_free(acl);
}

static void
expect_empty_acl_adds_ace_as_only_item(struct daos_ace *ace)
{
	struct daos_acl *acl;
	struct daos_acl *original_acl;
	size_t		ace_len;

	ace_len = daos_ace_get_size(ace);
	acl = daos_acl_create(NULL, 0);
	original_acl = daos_acl_dup(acl);

	assert_rc_equal(daos_acl_add_ace(&acl, ace), 0);

	assert_int_equal(acl->dal_ver, original_acl->dal_ver);
	assert_int_equal(acl->dal_len, ace_len);
	assert_memory_equal(acl->dal_ace, ace, ace_len);

	daos_acl_free(acl);
	daos_acl_free(original_acl);
}

static void
test_acl_add_ace_without_name(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ;

	expect_empty_acl_adds_ace_as_only_item(ace);

	daos_ace_free(ace);
}

static void
test_acl_add_ace_with_name(void **state)
{
	struct daos_ace	*ace;
	const char	name[] = "myuser@";

	ace = daos_ace_create(DAOS_ACL_USER, name);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ;

	expect_empty_acl_adds_ace_as_only_item(ace);

	daos_ace_free(ace);
}

/*
 * Assumes ACE array is allocated large enough for all types
 */
static void
fill_ace_list_with_all_types(struct daos_ace *ace[],
		const char *user_name, const char *group_name)
{
	int i;

	for (i = 0; i < NUM_DAOS_ACL_TYPES; i++) {
		if (i == DAOS_ACL_USER) {
			ace[i] = daos_ace_create(DAOS_ACL_USER, user_name);
		} else if (i == DAOS_ACL_GROUP) {
			ace[i] = daos_ace_create(DAOS_ACL_GROUP, group_name);
		} else {
			ace[i] = daos_ace_create(i, NULL);
		}
	}
}

static ssize_t
get_offset_for_type(enum daos_acl_principal_type type,
		struct daos_ace *ace[], int num_aces)
{
	int	i;
	ssize_t	offset = 0;

	/* Expect it to be inserted at the end of its own type list */
	for (i = 0; i < num_aces; i++) {
		if (ace[i]->dae_principal_type > type) {
			break;
		}

		offset += daos_ace_get_size(ace[i]);
	}

	return offset;
}

static void
expect_ace_inserted_at_correct_location(struct daos_ace *ace[], int num_aces,
		struct daos_ace *new_ace)
{
	struct daos_acl	*acl;
	struct daos_acl	*orig_acl;
	ssize_t		expected_len = 0;

	expected_len = get_total_ace_list_size(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);
	orig_acl = daos_acl_dup(acl);

	/* Add some permission bits for testing */
	new_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	new_ace->dae_allow_perms = DAOS_ACL_PERM_READ;
	expected_len += daos_ace_get_size(new_ace);

	assert_rc_equal(daos_acl_add_ace(&acl, new_ace), 0);

	assert_non_null(acl);
	assert_int_equal(acl->dal_ver, orig_acl->dal_ver);
	assert_int_equal(acl->dal_len, expected_len);

	assert_memory_equal(new_ace, acl->dal_ace +
		get_offset_for_type(new_ace->dae_principal_type, ace, num_aces),
		daos_ace_get_size(new_ace));

	/* cleanup */
	daos_acl_free(acl);
	daos_acl_free(orig_acl);
}

static void
test_acl_add_ace_user_to_existing_list(void **state)
{
	int		num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	new_ace_name[] = "newuser@";

	fill_ace_list_with_all_types(ace, "user1@", "group1@");

	new_ace = daos_ace_create(DAOS_ACL_USER, new_ace_name);

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_group_to_existing_list(void **state)
{
	int		num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	new_ace_name[] = "newgroup@";

	fill_ace_list_with_all_types(ace, "user1@", "group1@");

	new_ace = daos_ace_create(DAOS_ACL_GROUP, new_ace_name);

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_owner_to_existing_list(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	user_name[] = "user1@";
	const char	group_name[] = "group1@";

	ace[0] = daos_ace_create(DAOS_ACL_USER, user_name);
	ace[1] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace[2] = daos_ace_create(DAOS_ACL_GROUP, group_name);
	ace[3] = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	new_ace = daos_ace_create(DAOS_ACL_OWNER, NULL);

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_owner_group_to_existing_list(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	user_name[] = "user1@";
	const char	group_name[] = "group1@";

	ace[0] = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace[1] = daos_ace_create(DAOS_ACL_USER, user_name);
	ace[2] = daos_ace_create(DAOS_ACL_GROUP, group_name);
	ace[3] = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	new_ace = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_everyone_to_existing_list(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	user_name[] = "user1@";
	const char	group_name[] = "group1@";

	ace[0] = daos_ace_create(DAOS_ACL_OWNER, NULL);
	ace[1] = daos_ace_create(DAOS_ACL_USER, user_name);
	ace[2] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace[3] = daos_ace_create(DAOS_ACL_GROUP, group_name);

	new_ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
expect_add_duplicate_ace_unchanged(enum daos_acl_principal_type type)
{
	int              num_aces = NUM_DAOS_ACL_TYPES;
	ssize_t          size;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	struct daos_acl	*acl;
	struct daos_acl	*orig_acl;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	acl = daos_acl_create(ace, num_aces);
	orig_acl = daos_acl_dup(acl);

	/* Create an exact duplicate */
	size = daos_ace_get_size(ace[type]);
	assert_true(size > 0);
	D_ALLOC(new_ace, size);
	assert_non_null(new_ace);
	memcpy(new_ace, ace[type], size);

	assert_rc_equal(daos_acl_add_ace(&acl, new_ace), 0);

	/* Expect a copy of original */
	assert_non_null(acl);
	assert_int_equal(acl->dal_len, orig_acl->dal_len);
	assert_memory_equal(acl, orig_acl,
			sizeof(struct daos_acl) + orig_acl->dal_len);

	/* cleanup */
	daos_acl_free(acl);
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_duplicate(void **state)
{
	expect_add_duplicate_ace_unchanged(DAOS_ACL_USER);
	expect_add_duplicate_ace_unchanged(DAOS_ACL_GROUP);
}

static void
test_acl_add_ace_duplicate_no_name(void **state)
{
	expect_add_duplicate_ace_unchanged(DAOS_ACL_OWNER);
	expect_add_duplicate_ace_unchanged(DAOS_ACL_OWNER_GROUP);
	expect_add_duplicate_ace_unchanged(DAOS_ACL_EVERYONE);
}

static void
test_acl_add_ace_replace(void **state)
{
	int		num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	struct daos_acl	*acl;
	struct daos_acl	*orig_acl;
	uint8_t		*result_ace_addr;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	acl = daos_acl_create(ace, num_aces);
	orig_acl = daos_acl_dup(acl);

	/* Create an updated ACE */
	new_ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	new_ace->dae_access_flags = DAOS_ACL_FLAG_ACCESS_FAIL |
			DAOS_ACL_FLAG_POOL_INHERIT;
	new_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_ALARM;
	new_ace->dae_allow_perms = DAOS_ACL_PERM_READ;
	new_ace->dae_alarm_perms = DAOS_ACL_PERM_WRITE;

	assert_rc_equal(daos_acl_add_ace(&acl, new_ace), 0);

	/* Expect the entry was replaced, not added */
	assert_non_null(acl);
	assert_int_equal(acl->dal_len, orig_acl->dal_len);

	/* type EVERYONE is last, and there is only one ACE for it */
	result_ace_addr = acl->dal_ace + acl->dal_len -
			daos_ace_get_size(new_ace);
	assert_memory_equal(new_ace, result_ace_addr,
		daos_ace_get_size(new_ace));

	/* cleanup */
	daos_acl_free(acl);
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_remove_ace_null_acl_ptr(void **state)
{
	struct daos_acl	*result_acl = NULL;

	assert_rc_equal(daos_acl_remove_ace(NULL, DAOS_ACL_EVERYONE, NULL),
			-DER_INVAL);

	assert_null(result_acl);
}

static void
test_acl_remove_ace_null_acl(void **state)
{
	struct daos_acl	*acl = NULL;

	assert_rc_equal(daos_acl_remove_ace(&acl, DAOS_ACL_EVERYONE, NULL),
			-DER_INVAL);

	/* cleanup */
	daos_acl_free(acl);
}

static void
test_acl_remove_ace_invalid_type(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_remove_ace(&acl,
			NUM_DAOS_ACL_TYPES, ace[0]->dae_principal),
			-DER_INVAL);

	/* cleanup */
	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
expect_acl_remove_ace_missing_name_fails(enum daos_acl_principal_type type)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_remove_ace(&acl, type, NULL), -DER_INVAL);

	/* cleanup */
	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_missing_name(void **state)
{
	expect_acl_remove_ace_missing_name_fails(DAOS_ACL_USER);
	expect_acl_remove_ace_missing_name_fails(DAOS_ACL_GROUP);
}

static void
test_acl_remove_ace_name_len_zero(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_remove_ace(&acl,
					    DAOS_ACL_USER, ""), -DER_INVAL);

	assert_rc_equal(daos_acl_remove_ace(&acl,
					    DAOS_ACL_GROUP, ""), -DER_INVAL);

	/* cleanup */
	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_one_user(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_remove_ace(&acl,
			ace[0]->dae_principal_type, ace[0]->dae_principal), 0);

	/* Result should be empty ACL */
	assert_non_null(acl);
	assert_int_equal(acl->dal_len, 0);

	/* cleanup */
	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_multi_user(void **state)
{
	int		num_aces = 4;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;
	struct daos_acl	*orig_acl;
	int		removed_idx = 2;
	int		i;

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);
	orig_acl = daos_acl_dup(acl);

	assert_rc_equal(daos_acl_remove_ace(&acl,
			ace[removed_idx]->dae_principal_type,
			ace[removed_idx]->dae_principal),
			0);

	/* Result should have only removed that user */
	assert_non_null(acl);
	assert_int_equal(acl->dal_len, orig_acl->dal_len -
			daos_ace_get_size(ace[removed_idx]));

	for (i = 0; i < num_aces; i++) {
		struct daos_ace	*current = NULL;
		int		rc;

		rc = daos_acl_get_ace_for_principal(acl,
				ace[i]->dae_principal_type,
				ace[i]->dae_principal, &current);
		if (i == removed_idx) {
			assert_rc_equal(rc, -DER_NONEXIST);
			assert_null(current);
		} else {
			assert_rc_equal(rc, 0);
			assert_non_null(current);
		}
	}

	/* cleanup */
	daos_acl_free(acl);
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
}

static void
expect_acl_remove_ace_removes_principal(enum daos_acl_principal_type type,
		const char *principal)
{
	int		num_aces = NUM_DAOS_ACL_TYPES;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;
	struct daos_acl	*orig_acl;
	struct daos_ace	*ace_to_find = NULL;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	acl = daos_acl_create(ace, num_aces);
	orig_acl = daos_acl_dup(acl);

	assert_rc_equal(daos_acl_remove_ace(&acl, type, principal), 0);

	/* Result should have the specific ACE removed */
	assert_non_null(acl);
	assert_int_equal(acl->dal_len,
			 orig_acl->dal_len - daos_ace_get_size(ace[type]));
	assert_rc_equal(daos_acl_get_ace_for_principal(acl, type, principal,
						       &ace_to_find),
			-DER_NONEXIST);

	/* cleanup */
	daos_acl_free(acl);
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_first(void **state)
{
	expect_acl_remove_ace_removes_principal(DAOS_ACL_OWNER, NULL);
}

static void
test_acl_remove_ace_last(void **state)
{
	expect_acl_remove_ace_removes_principal(DAOS_ACL_EVERYONE, NULL);
}

static void
test_acl_remove_ace_with_name(void **state)
{
	const char user_name[] = "user1@";
	const char group_name[] = "group1@";

	expect_acl_remove_ace_removes_principal(DAOS_ACL_USER, user_name);
	expect_acl_remove_ace_removes_principal(DAOS_ACL_GROUP, group_name);
}

static void
test_acl_remove_ace_not_found(void **state)
{
	int		num_aces = 4;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;
	const char	name[] = "notarealuser@";

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_create(ace, num_aces);

	assert_rc_equal(daos_acl_remove_ace(&acl, DAOS_ACL_USER, name),
			-DER_NONEXIST);

	/* cleanup */
	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ace_alloc_principal_user),
		cmocka_unit_test(test_ace_alloc_principal_user_no_name),
		cmocka_unit_test(test_ace_alloc_principal_user_null_name),
		cmocka_unit_test(test_ace_alloc_principal_group),
		cmocka_unit_test(test_ace_alloc_principal_group_no_name),
		cmocka_unit_test(test_ace_alloc_principal_owner),
		cmocka_unit_test(test_ace_alloc_principal_owner_ignores_name),
		cmocka_unit_test(test_ace_alloc_principal_owner_group),
		cmocka_unit_test(test_ace_alloc_principal_everyone),
		cmocka_unit_test(test_ace_alloc_principal_invalid),
		cmocka_unit_test(test_ace_get_size_null),
		cmocka_unit_test(test_ace_get_size_without_name),
		cmocka_unit_test(test_ace_get_size_with_name),
		cmocka_unit_test(test_acl_alloc_empty),
		cmocka_unit_test(test_acl_alloc_one_user),
		cmocka_unit_test(test_acl_alloc_two_users),
		cmocka_unit_test(test_acl_alloc_type_order),
		cmocka_unit_test(test_acl_alloc_null_ace),
		cmocka_unit_test(test_acl_copy_null_acl),
		cmocka_unit_test(test_acl_copy_empty_acl),
		cmocka_unit_test(test_acl_copy_with_aces),
		cmocka_unit_test(test_acl_get_size_null),
		cmocka_unit_test(test_acl_get_size_empty),
		cmocka_unit_test(test_acl_get_size_with_aces),
		cmocka_unit_test(test_acl_get_first_ace_empty_list),
		cmocka_unit_test(test_acl_get_first_ace_multiple),
		cmocka_unit_test(test_acl_get_next_ace_null_acl),
		cmocka_unit_test(test_acl_get_next_ace_success),
		cmocka_unit_test(test_acl_get_next_ace_last_item),
		cmocka_unit_test(test_acl_get_next_ace_empty),
		cmocka_unit_test(test_acl_get_next_ace_bad_ace),
		cmocka_unit_test(test_acl_get_ace_null_acl),
		cmocka_unit_test(test_acl_get_ace_null_ace_ptr),
		cmocka_unit_test(test_acl_get_ace_invalid_type),
		cmocka_unit_test(test_acl_get_ace_first_item),
		cmocka_unit_test(test_acl_get_ace_later_item),
		cmocka_unit_test(test_acl_get_ace_match_wrong_type),
		cmocka_unit_test(test_acl_get_ace_name_not_found),
		cmocka_unit_test(test_acl_get_ace_name_needed),
		cmocka_unit_test(test_acl_get_ace_name_not_needed),
		cmocka_unit_test(test_acl_add_ace_with_null_acl_ptr),
		cmocka_unit_test(test_acl_add_ace_with_null_acl),
		cmocka_unit_test(test_acl_add_ace_with_null_ace),
		cmocka_unit_test(test_acl_add_ace_without_name),
		cmocka_unit_test(test_acl_add_ace_with_name),
		cmocka_unit_test(test_acl_add_ace_user_to_existing_list),
		cmocka_unit_test(test_acl_add_ace_group_to_existing_list),
		cmocka_unit_test(test_acl_add_ace_owner_to_existing_list),
		cmocka_unit_test(test_acl_add_ace_owner_group_to_existing_list),
		cmocka_unit_test(test_acl_add_ace_everyone_to_existing_list),
		cmocka_unit_test(test_acl_add_ace_duplicate),
		cmocka_unit_test(test_acl_add_ace_duplicate_no_name),
		cmocka_unit_test(test_acl_add_ace_replace),
		cmocka_unit_test(test_acl_remove_ace_null_acl_ptr),
		cmocka_unit_test(test_acl_remove_ace_null_acl),
		cmocka_unit_test(test_acl_remove_ace_invalid_type),
		cmocka_unit_test(test_acl_remove_ace_missing_name),
		cmocka_unit_test(test_acl_remove_ace_name_len_zero),
		cmocka_unit_test(test_acl_remove_ace_one_user),
		cmocka_unit_test(test_acl_remove_ace_multi_user),
		cmocka_unit_test(test_acl_remove_ace_first),
		cmocka_unit_test(test_acl_remove_ace_last),
		cmocka_unit_test(test_acl_remove_ace_with_name),
		cmocka_unit_test(test_acl_remove_ace_not_found),
	};

	return cmocka_run_group_tests_name("common_acl_api", tests, NULL, NULL);
}
