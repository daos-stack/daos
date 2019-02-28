/**
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * Unit tests for the ACL property API
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_types.h>
#include <daos_api.h>
#include <gurt/common.h>
#include <gurt/errno.h>

static size_t
aligned_strlen(const char *str)
{
	size_t len = strlen(str) + 1;

	return D_ALIGNUP(len, 8);
}

static void
test_ace_alloc_principal_user(void **state)
{
	const char			expected_name[] = "user1@";
	enum daos_acl_principal_type	expected_type = DAOS_ACL_USER;
	struct daos_ace			*ace;

	ace = daos_ace_alloc(expected_type, expected_name,
			sizeof(expected_name));

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

	ace = daos_ace_alloc(DAOS_ACL_USER, "", 0);

	assert_null(ace);
}

static void
test_ace_alloc_principal_user_bad_len(void **state)
{
	struct daos_ace *ace;

	/* nonzero len for NULL name is invalid */
	ace = daos_ace_alloc(DAOS_ACL_USER, NULL, 5);

	assert_null(ace);
}

static void
test_ace_alloc_principal_group(void **state)
{
	const char			expected_name[] = "group1234@";
	enum daos_acl_principal_type	expected_type = DAOS_ACL_GROUP;
	struct daos_ace			*ace;

	ace = daos_ace_alloc(expected_type, expected_name,
			sizeof(expected_name));

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

	ace = daos_ace_alloc(DAOS_ACL_GROUP, "", 0);

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

	ace = daos_ace_alloc(DAOS_ACL_OWNER, "", 0);

	expect_valid_owner_ace(ace);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_owner_ignores_name(void **state)
{
	const char	name[] = "owner@";
	struct daos_ace	*ace;

	ace = daos_ace_alloc(DAOS_ACL_OWNER, name, sizeof(name));

	expect_valid_owner_ace(ace);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_owner_ignores_len(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_alloc(DAOS_ACL_OWNER, NULL, 6);

	expect_valid_owner_ace(ace);

	daos_ace_free(ace);
}

static void
test_ace_alloc_principal_owner_group(void **state)
{
	enum daos_acl_principal_type	expected_type = DAOS_ACL_OWNER_GROUP;
	struct daos_ace			*ace;

	ace = daos_ace_alloc(expected_type, NULL, 0);

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

	ace = daos_ace_alloc(expected_type, NULL, 0);

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

	ace = daos_ace_alloc(DAOS_ACL_EVERYONE + 0xFF, "", 0);

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

	ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	assert_int_equal(daos_ace_get_size(ace), sizeof(struct daos_ace));

	daos_ace_free(ace);
}

static void
test_ace_get_size_with_name(void **state)
{
	const char	name[] = "group1@";
	struct daos_ace	*ace;

	ace = daos_ace_alloc(DAOS_ACL_GROUP, name, sizeof(name));

	/* name string rounded up to 64 bits */
	assert_int_equal(daos_ace_get_size(ace), sizeof(struct daos_ace) +
			aligned_strlen(name));

	daos_ace_free(ace);
}

static void
test_acl_alloc_empty(void **state)
{
	struct daos_acl *acl = daos_acl_alloc(NULL, 0);

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

	ace[0] = daos_ace_alloc(DAOS_ACL_USER, name, sizeof(name));

	acl = daos_acl_alloc(ace, 1);

	assert_non_null(acl);
	assert_int_equal(acl->dal_ver, 1);
	assert_int_equal(acl->dal_len, daos_ace_get_size(ace[0]));
	assert_memory_equal(acl->dal_ace, ace[0], daos_ace_get_size(ace[0]));

	daos_ace_free(ace[0]);
	daos_acl_free(acl);
}


static void
fill_ace_list_with_users(struct daos_ace *ace[], size_t num_aces)
{
	int i;

	for (i = 0; i < num_aces; i++) {
		char name[256];

		snprintf(name, sizeof(name), "user%d@", i + 1);
		ace[i] = daos_ace_alloc(DAOS_ACL_USER, name,
				strlen(name) + 1);
	}
}

static size_t
get_total_ace_list_size(struct daos_ace *ace[], size_t num_aces)
{
	int ace_len = 0;
	int i;

	for (i = 0; i < num_aces; i++) {
		ace_len += daos_ace_get_size(ace[i]);
	}

	return ace_len;
}

static void
free_all_aces(struct daos_ace *ace[], size_t num_aces)
{
	int i;

	for (i = 0; i < num_aces; i++) {
		daos_ace_free(ace[i]);
	}
}

static void
test_acl_alloc_two_users(void **state)
{
	struct daos_acl *acl;
	int		ace_len;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	ace_len = get_total_ace_list_size(ace, num_aces);

	acl = daos_acl_alloc(ace, num_aces);

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
	ace[0] = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);
	ace[1] = daos_ace_alloc(DAOS_ACL_OWNER_GROUP, NULL, 0);
	ace[2] = daos_ace_alloc(DAOS_ACL_USER, user_name,
			strlen(user_name) + 1);
	ace[3] = daos_ace_alloc(DAOS_ACL_OWNER, NULL, 0);
	ace[4] = daos_ace_alloc(DAOS_ACL_GROUP, group_name,
			strlen(group_name) + 1);
}

static void
test_acl_alloc_type_order(void **state)
{
	struct daos_acl			*acl;
	int				i;
	int				ace_len = 0;
	size_t				num_aces = DAOS_ACL_EVERYONE + 1;
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

	acl = daos_acl_alloc(ace, num_aces);

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

	ace[0] = daos_ace_alloc(DAOS_ACL_OWNER, NULL, 0);
	ace[1] = NULL;

	acl = daos_acl_alloc(ace, num_aces);

	/* NULL entry is invalid input, don't do anything with it */
	assert_null(acl);

	/* cleanup */
	daos_ace_free(ace[0]);
}

static void
test_acl_get_first_ace_null_acl(void **state)
{
	assert_null(daos_acl_get_first_ace(NULL));
}

static void
test_acl_get_first_ace_empty_list(void **state)
{
	struct daos_acl *acl = daos_acl_alloc(NULL, 0);

	assert_null(daos_acl_get_first_ace(acl));

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

	acl = daos_acl_alloc(ace, num_aces);

	result = daos_acl_get_first_ace(acl);

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
	struct daos_ace *ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	assert_null(daos_acl_get_next_ace(NULL, ace));

	daos_ace_free(ace);
}

static void
test_acl_get_next_ace_null_ace(void **state)
{
	struct daos_acl *acl = daos_acl_alloc(NULL, 0);

	assert_null(daos_acl_get_next_ace(acl, NULL));

	daos_acl_free(acl);
}

static void
test_acl_get_next_ace_success(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);

	acl = daos_acl_alloc(ace, num_aces);

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

	acl = daos_acl_alloc(ace, num_aces);
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

	acl = daos_acl_alloc(NULL, 0);

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
	acl = daos_acl_alloc(ace, num_aces);

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
	assert_null(daos_acl_get_ace_for_principal(NULL, DAOS_ACL_USER,
			"user1@"));
}

static void
test_acl_get_ace_invalid_type(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_alloc(ace, num_aces);

	/* bad type */
	result = daos_acl_get_ace_for_principal(acl, DAOS_ACL_EVERYONE + 1,
			ace[0]->dae_principal);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_first_item(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_alloc(ace, num_aces);

	result = daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			ace[0]->dae_principal);

	assert_non_null(result);
	assert_ptr_equal(result, acl->dal_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_later_item(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_alloc(ace, num_aces);

	result = daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			ace[1]->dae_principal);

	assert_non_null(result);
	assert_ptr_equal(result, acl->dal_ace + daos_ace_get_size(ace[0]));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_match_wrong_type(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);

	acl = daos_acl_alloc(ace, num_aces);

	result = daos_acl_get_ace_for_principal(acl, DAOS_ACL_GROUP,
			ace[0]->dae_principal);

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_name_not_found(void **state)
{
	struct daos_acl *acl;
	struct daos_ace *result;
	size_t		num_aces = 2;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_users(ace, num_aces);
	acl = daos_acl_alloc(ace, num_aces);

	result = daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER,
			"notinthelist");

	assert_null(result);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_get_ace_name_needed(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_all_types_shuffled(ace, "user1@", "group1@");
	acl = daos_acl_alloc(ace, num_aces);

	assert_null(daos_acl_get_ace_for_principal(acl, DAOS_ACL_USER, NULL));
	assert_null(daos_acl_get_ace_for_principal(acl, DAOS_ACL_GROUP, NULL));

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
expect_acl_get_ace_returns_type(struct daos_acl *acl,
		enum daos_acl_principal_type type)
{
	struct daos_ace	*result;

	result = daos_acl_get_ace_for_principal(acl, type, NULL);
	assert_non_null(result);
	assert_int_equal(result->dae_principal_type, type);
}

static void
test_acl_get_ace_name_not_needed(void **state)
{
	struct daos_acl *acl;
	size_t		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace *ace[num_aces];

	fill_ace_list_with_all_types_shuffled(ace, "user1@", "group1@");
	acl = daos_acl_alloc(ace, num_aces);

	expect_acl_get_ace_returns_type(acl, DAOS_ACL_OWNER);
	expect_acl_get_ace_returns_type(acl, DAOS_ACL_OWNER_GROUP);
	expect_acl_get_ace_returns_type(acl, DAOS_ACL_EVERYONE);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_acl_free(acl);
}

static void
test_acl_add_ace_with_null_acl(void **state)
{
	struct daos_ace *ace;
	struct daos_acl *new_acl = NULL;

	ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	assert_int_equal(daos_acl_add_ace_realloc(NULL, ace, &new_acl),
			-DER_INVAL);

	assert_null(new_acl);

	daos_ace_free(ace);
}

static void
test_acl_add_ace_with_null_new_acl(void **state)
{
	struct daos_ace *ace;
	struct daos_acl *acl;

	acl = daos_acl_alloc(NULL, 0);
	ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	assert_int_equal(daos_acl_add_ace_realloc(acl, ace, NULL),
			-DER_INVAL);

	daos_acl_free(acl);
	daos_ace_free(ace);
}

static void
test_acl_add_ace_with_null_ace(void **state)
{
	struct daos_acl *acl;
	struct daos_acl *new_acl = NULL;

	acl = daos_acl_alloc(NULL, 0);

	assert_int_equal(daos_acl_add_ace_realloc(acl, NULL, &new_acl),
			-DER_INVAL);

	assert_null(new_acl);

	daos_acl_free(acl);
}

static void
expect_empty_acl_adds_ace_as_only_item(struct daos_ace *ace)
{
	struct daos_acl *acl;
	struct daos_acl *new_acl = NULL;
	size_t		ace_len;

	ace_len = daos_ace_get_size(ace);
	acl = daos_acl_alloc(NULL, 0);

	assert_int_equal(daos_acl_add_ace_realloc(acl, ace, &new_acl),
			DER_SUCCESS);

	assert_non_null(new_acl);
	assert_ptr_not_equal(new_acl, acl);

	assert_int_equal(new_acl->dal_ver, acl->dal_ver);
	assert_int_equal(new_acl->dal_len, ace_len);
	assert_memory_equal(new_acl->dal_ace, ace, ace_len);

	daos_acl_free(acl);
	daos_acl_free(new_acl);
}

static void
test_acl_add_ace_without_name(void **state)
{
	struct daos_ace *ace;

	ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);
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

	ace = daos_ace_alloc(DAOS_ACL_USER, name, sizeof(name));
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

	for (i = 0; i < (DAOS_ACL_EVERYONE + 1); i++) {
		if (i == DAOS_ACL_USER) {
			ace[i] = daos_ace_alloc(DAOS_ACL_USER, user_name,
					strlen(user_name) + 1);
		} else if (i == DAOS_ACL_GROUP) {
			ace[i] = daos_ace_alloc(DAOS_ACL_GROUP, group_name,
					strlen(group_name) + 1);
		} else {
			ace[i] = daos_ace_alloc(i, NULL, 0);
		}
	}
}

static size_t
get_offset_for_type(enum daos_acl_principal_type type,
		struct daos_ace *ace[], int num_aces)
{
	int	i;
	size_t	offset = 0;

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
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;
	size_t		expected_len = 0;

	expected_len = get_total_ace_list_size(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	/* Add some permission bits for testing */
	new_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	new_ace->dae_allow_perms = DAOS_ACL_PERM_READ;
	expected_len += daos_ace_get_size(new_ace);

	assert_int_equal(daos_acl_add_ace_realloc(orig_acl, new_ace,
			&result_acl), DER_SUCCESS);

	assert_non_null(result_acl);
	assert_int_equal(result_acl->dal_ver, orig_acl->dal_ver);
	assert_int_equal(result_acl->dal_len, expected_len);

	assert_memory_equal(new_ace, result_acl->dal_ace +
		get_offset_for_type(new_ace->dae_principal_type, ace, num_aces),
		daos_ace_get_size(new_ace));

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
}

static void
test_acl_add_ace_user_to_existing_list(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	new_ace_name[] = "newuser@";

	fill_ace_list_with_all_types(ace, "user1@", "group1@");

	new_ace = daos_ace_alloc(DAOS_ACL_USER, new_ace_name,
			sizeof(new_ace_name));

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_group_to_existing_list(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	const char	new_ace_name[] = "newgroup@";

	fill_ace_list_with_all_types(ace, "user1@", "group1@");

	new_ace = daos_ace_alloc(DAOS_ACL_GROUP, new_ace_name,
			sizeof(new_ace_name));

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

	ace[0] = daos_ace_alloc(DAOS_ACL_USER, user_name,
			sizeof(user_name));
	ace[1] = daos_ace_alloc(DAOS_ACL_OWNER_GROUP, NULL, 0);
	ace[2] = daos_ace_alloc(DAOS_ACL_GROUP, group_name,
			sizeof(group_name));
	ace[3] = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	new_ace = daos_ace_alloc(DAOS_ACL_OWNER, NULL, 0);

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

	ace[0] = daos_ace_alloc(DAOS_ACL_OWNER, NULL, 0);
	ace[1] = daos_ace_alloc(DAOS_ACL_USER, user_name,
			sizeof(user_name));
	ace[2] = daos_ace_alloc(DAOS_ACL_GROUP, group_name,
			sizeof(group_name));
	ace[3] = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	new_ace = daos_ace_alloc(DAOS_ACL_OWNER_GROUP, NULL, 0);

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

	ace[0] = daos_ace_alloc(DAOS_ACL_OWNER, NULL, 0);
	ace[1] = daos_ace_alloc(DAOS_ACL_USER, user_name,
			sizeof(user_name));
	ace[2] = daos_ace_alloc(DAOS_ACL_OWNER_GROUP, NULL, 0);
	ace[3] = daos_ace_alloc(DAOS_ACL_GROUP, group_name,
			sizeof(group_name));

	new_ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);

	expect_ace_inserted_at_correct_location(ace, num_aces, new_ace);

	/* cleanup */
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_duplicate(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	orig_acl = daos_acl_alloc(ace, num_aces);

	/* Create an exact duplicate */
	D_ALLOC(new_ace, daos_ace_get_size(ace[DAOS_ACL_USER]));
	memcpy(new_ace, ace[DAOS_ACL_USER],
			daos_ace_get_size(ace[DAOS_ACL_USER]));

	assert_int_equal(daos_acl_add_ace_realloc(orig_acl, new_ace,
			&result_acl), DER_SUCCESS);

	/* Expect a copy of original */
	assert_non_null(result_acl);
	assert_int_equal(result_acl->dal_len, orig_acl->dal_len);
	assert_memory_equal(result_acl, orig_acl,
			sizeof(struct daos_acl) + orig_acl->dal_len);
	assert_ptr_not_equal(result_acl, orig_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_duplicate_no_name(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	orig_acl = daos_acl_alloc(ace, num_aces);

	/* Create an exact duplicate */
	D_ALLOC(new_ace, daos_ace_get_size(ace[DAOS_ACL_OWNER]));
	memcpy(new_ace, ace[DAOS_ACL_OWNER],
			daos_ace_get_size(ace[DAOS_ACL_OWNER]));

	assert_int_equal(daos_acl_add_ace_realloc(orig_acl, new_ace,
			&result_acl), DER_SUCCESS);

	/* Expect a copy of original */
	assert_non_null(result_acl);
	assert_int_equal(orig_acl->dal_len, result_acl->dal_len);
	assert_memory_equal(orig_acl, result_acl,
			sizeof(struct daos_acl) + orig_acl->dal_len);
	assert_ptr_not_equal(result_acl, orig_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_add_ace_replace(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_ace	*new_ace;
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;
	uint8_t		*result_ace_addr;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	orig_acl = daos_acl_alloc(ace, num_aces);

	/* Create an updated ACE */
	new_ace = daos_ace_alloc(DAOS_ACL_EVERYONE, NULL, 0);
	new_ace->dae_access_flags = DAOS_ACL_FLAG_ACCESS_FAIL |
			DAOS_ACL_FLAG_POOL_INHERIT;
	new_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW |
			DAOS_ACL_ACCESS_ALARM;
	new_ace->dae_allow_perms = DAOS_ACL_PERM_READ;
	new_ace->dae_alarm_perms = DAOS_ACL_PERM_WRITE;

	assert_int_equal(daos_acl_add_ace_realloc(orig_acl, new_ace,
			&result_acl), DER_SUCCESS);

	/* Expect the entry was replaced, not added */
	assert_non_null(result_acl);
	assert_int_equal(orig_acl->dal_len, result_acl->dal_len);

	// type EVERYONE is last, and there is only one ACE for it
	result_ace_addr = result_acl->dal_ace + result_acl->dal_len -
			daos_ace_get_size(new_ace);
	assert_memory_equal(new_ace, result_ace_addr,
		daos_ace_get_size(new_ace));

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
	free_all_aces(ace, num_aces);
	daos_ace_free(new_ace);
}

static void
test_acl_remove_ace_null_acl(void **state)
{
	struct daos_acl	*result_acl = NULL;

	assert_int_equal(daos_acl_remove_ace_realloc(NULL, DAOS_ACL_EVERYONE,
			NULL, 0, &result_acl), -DER_INVAL);

	assert_null(result_acl);
}

static void
test_acl_remove_ace_null_new_acl(void **state)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*acl;

	fill_ace_list_with_all_types(ace, "dontcare", "dontcare");
	acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(acl, DAOS_ACL_EVERYONE,
			NULL, 0, NULL), -DER_INVAL);

	/* cleanup */
	daos_acl_free(acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_invalid_type(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_users(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			DAOS_ACL_EVERYONE + 1, ace[0]->dae_principal,
			ace[0]->dae_principal_len, &result_acl), -DER_INVAL);

	assert_null(result_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_missing_name(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_users(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			DAOS_ACL_USER, NULL, 5, &result_acl), -DER_INVAL);
	assert_null(result_acl);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			DAOS_ACL_GROUP, NULL, 5, &result_acl), -DER_INVAL);
	assert_null(result_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_name_len_zero(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_users(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			DAOS_ACL_USER, "user1@", 0, &result_acl), -DER_INVAL);
	assert_null(result_acl);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			DAOS_ACL_GROUP, "group1@", 0, &result_acl), -DER_INVAL);
	assert_null(result_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_one_user(void **state)
{
	int		num_aces = 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_users(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			ace[0]->dae_principal_type, ace[0]->dae_principal,
			ace[0]->dae_principal_len, &result_acl), DER_SUCCESS);

	/* Result should be empty ACL */
	assert_non_null(result_acl);
	assert_int_equal(result_acl->dal_len, 0);
	assert_ptr_not_equal(result_acl, orig_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_multi_user(void **state)
{
	int		num_aces = 4;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;
	int		removed_idx = 2;
	int		i;

	fill_ace_list_with_users(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			ace[removed_idx]->dae_principal_type,
			ace[removed_idx]->dae_principal,
			ace[removed_idx]->dae_principal_len, &result_acl),
			DER_SUCCESS);

	/* Result should have only removed that user */
	assert_non_null(result_acl);
	assert_ptr_not_equal(result_acl, orig_acl);
	assert_int_equal(result_acl->dal_len,orig_acl->dal_len -
			daos_ace_get_size(ace[removed_idx]));

	for (i = 0; i < num_aces; i++) {
		if (i == removed_idx) {
			assert_null(daos_acl_get_ace_for_principal(result_acl,
					ace[i]->dae_principal_type,
					ace[i]->dae_principal));
		} else {
			assert_non_null(daos_acl_get_ace_for_principal(
					result_acl,
					ace[i]->dae_principal_type,
					ace[i]->dae_principal));
		}
	}

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
	free_all_aces(ace, num_aces);
}

static void
expect_acl_remove_ace_removes_principal(enum daos_acl_principal_type type,
		const char *principal, size_t principal_len)
{
	int		num_aces = DAOS_ACL_EVERYONE + 1;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;

	fill_ace_list_with_all_types(ace, "user1@", "group1@");
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			type, principal, principal_len, &result_acl),
			DER_SUCCESS);

	/* Result should have the specific ACE removed */
	assert_non_null(result_acl);
	assert_ptr_not_equal(result_acl, orig_acl);
	assert_int_equal(result_acl->dal_len,
			orig_acl->dal_len - daos_ace_get_size(ace[type]));
	assert_null(daos_acl_get_ace_for_principal(result_acl,
			type, principal));

	/* cleanup */
	daos_acl_free(orig_acl);
	daos_acl_free(result_acl);
	free_all_aces(ace, num_aces);
}

static void
test_acl_remove_ace_first(void **state)
{
	expect_acl_remove_ace_removes_principal(DAOS_ACL_OWNER, NULL, 0);
}

static void
test_acl_remove_ace_last(void **state)
{
	expect_acl_remove_ace_removes_principal(DAOS_ACL_EVERYONE, NULL, 0);
}

static void
test_acl_remove_ace_with_name(void **state)
{
	const char user_name[] = "user1@";
	const char group_name[] = "group1@";

	expect_acl_remove_ace_removes_principal(DAOS_ACL_USER, user_name,
			sizeof(user_name));
	expect_acl_remove_ace_removes_principal(DAOS_ACL_GROUP, group_name,
			sizeof(group_name));
}

static void
test_acl_remove_ace_not_found(void **state)
{
	int		num_aces = 4;
	struct daos_ace	*ace[num_aces];
	struct daos_acl	*orig_acl;
	struct daos_acl	*result_acl = NULL;
	const char	name[] = "notarealuser@";

	fill_ace_list_with_users(ace, num_aces);
	orig_acl = daos_acl_alloc(ace, num_aces);

	assert_int_equal(daos_acl_remove_ace_realloc(orig_acl,
			DAOS_ACL_USER,
			name, sizeof(name), &result_acl), -DER_NONEXIST);

	/* No point in bothering with it if there's nothing to do */
	assert_null(result_acl);

	/* cleanup */
	daos_acl_free(orig_acl);
	free_all_aces(ace, num_aces);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ace_alloc_principal_user),
		cmocka_unit_test(test_ace_alloc_principal_user_no_name),
		cmocka_unit_test(test_ace_alloc_principal_user_bad_len),
		cmocka_unit_test(test_ace_alloc_principal_group),
		cmocka_unit_test(test_ace_alloc_principal_group_no_name),
		cmocka_unit_test(test_ace_alloc_principal_owner),
		cmocka_unit_test(test_ace_alloc_principal_owner_ignores_name),
		cmocka_unit_test(test_ace_alloc_principal_owner_ignores_len),
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
		cmocka_unit_test(test_acl_get_first_ace_null_acl),
		cmocka_unit_test(test_acl_get_first_ace_empty_list),
		cmocka_unit_test(test_acl_get_first_ace_multiple),
		cmocka_unit_test(test_acl_get_next_ace_null_acl),
		cmocka_unit_test(test_acl_get_next_ace_null_ace),
		cmocka_unit_test(test_acl_get_next_ace_success),
		cmocka_unit_test(test_acl_get_next_ace_last_item),
		cmocka_unit_test(test_acl_get_next_ace_empty),
		cmocka_unit_test(test_acl_get_next_ace_bad_ace),
		cmocka_unit_test(test_acl_get_ace_null_acl),
		cmocka_unit_test(test_acl_get_ace_invalid_type),
		cmocka_unit_test(test_acl_get_ace_first_item),
		cmocka_unit_test(test_acl_get_ace_later_item),
		cmocka_unit_test(test_acl_get_ace_match_wrong_type),
		cmocka_unit_test(test_acl_get_ace_name_not_found),
		cmocka_unit_test(test_acl_get_ace_name_needed),
		cmocka_unit_test(test_acl_get_ace_name_not_needed),
		cmocka_unit_test(test_acl_add_ace_with_null_acl),
		cmocka_unit_test(test_acl_add_ace_with_null_new_acl),
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
		cmocka_unit_test(test_acl_remove_ace_null_acl),
		cmocka_unit_test(test_acl_remove_ace_null_new_acl),
		cmocka_unit_test(test_acl_remove_ace_invalid_type),
		cmocka_unit_test(test_acl_remove_ace_missing_name),
		cmocka_unit_test(test_acl_remove_ace_name_len_zero),
		cmocka_unit_test(test_acl_remove_ace_one_user),
		cmocka_unit_test(test_acl_remove_ace_multi_user),
		cmocka_unit_test(test_acl_remove_ace_first),
		cmocka_unit_test(test_acl_remove_ace_last),
		cmocka_unit_test(test_acl_remove_ace_with_name),
		cmocka_unit_test(test_acl_remove_ace_not_found)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
