/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the DAOS property API
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_prop.h>
#include <daos/common.h>

static void
test_daos_prop_merge_null(void **state)
{
	daos_prop_t	*prop;

	prop = daos_prop_alloc(0);

	assert_null(daos_prop_merge(prop, NULL));
	assert_null(daos_prop_merge(NULL, prop));

	daos_prop_free(prop);
}

static void
expect_merge_result(daos_prop_t *old, daos_prop_t *new, daos_prop_t *exp_result)
{
	daos_prop_t		*result;
	struct daos_prop_entry	*entry;
	struct daos_prop_entry	*exp_entry;
	uint32_t		i;

	result = daos_prop_merge(old, new);
	assert_non_null(result);
	assert_int_equal(result->dpp_nr, exp_result->dpp_nr);
	for (i = 0; i < exp_result->dpp_nr; i++) {
		/*
		 * This check is agnostic to the order of the properties.
		 */
		exp_entry = &exp_result->dpp_entries[i];
		printf("- Checking for property type %u\n",
		       exp_entry->dpe_type);
		entry = daos_prop_entry_get(result, exp_entry->dpe_type);
		assert_non_null(entry);

		switch (entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
		case DAOS_PROP_CO_LABEL:
		case DAOS_PROP_PO_OWNER:
		case DAOS_PROP_CO_OWNER:
		case DAOS_PROP_PO_OWNER_GROUP:
		case DAOS_PROP_CO_OWNER_GROUP:
			assert_string_equal(entry->dpe_str, exp_entry->dpe_str);
			break;
		case DAOS_PROP_PO_ACL:
		case DAOS_PROP_CO_ACL:
			assert_int_equal(daos_prop_entry_cmp_acl(entry,
								 exp_entry),
					 0);
			break;
		default:
			assert_int_equal(entry->dpe_val, exp_entry->dpe_val);
			break;
		};
	}

	daos_prop_free(result);
}

static void
test_daos_prop_merge_empty(void **state)
{
	daos_prop_t	*prop_empty;
	daos_prop_t	*prop;

	prop_empty = daos_prop_alloc(0);
	prop = daos_prop_alloc(2);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_LABEL;
	D_STRNDUP(prop->dpp_entries[0].dpe_str, "Test",
		  DAOS_PROP_LABEL_MAX_LEN);
	prop->dpp_entries[1].dpe_type = DAOS_PROP_PO_RECLAIM;
	prop->dpp_entries[1].dpe_val = DAOS_RECLAIM_LAZY;

	printf("Case: Two empty props\n");
	expect_merge_result(prop_empty, prop_empty, prop_empty);

	printf("Case: Add empty to non-empty\n");
	expect_merge_result(prop, prop_empty, prop);

	printf("Case: Add non-empty to empty\n");
	expect_merge_result(prop_empty, prop, prop);

	daos_prop_free(prop_empty);
	daos_prop_free(prop);
}

static void
test_daos_prop_merge_add_only(void **state)
{
	daos_prop_t	*prop1;
	daos_prop_t	*prop2;
	uint32_t	exp_nr, i, result_i;
	daos_prop_t	*exp_result;

	prop1 = daos_prop_alloc(2);
	prop1->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop1->dpp_entries[0].dpe_str, "Test",
		  DAOS_PROP_LABEL_MAX_LEN);
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(3);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_OWNER;
	D_STRNDUP(prop2->dpp_entries[0].dpe_str, "test@",
		  DAOS_ACL_MAX_PRINCIPAL_LEN);
	prop2->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	prop2->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_CRC32;
	prop2->dpp_entries[2].dpe_type = DAOS_PROP_CO_ENCRYPT;
	prop2->dpp_entries[2].dpe_val = 1;

	/* Should be set of all the prop entries, no conflicts */
	exp_nr = prop1->dpp_nr + prop2->dpp_nr;
	exp_result = daos_prop_alloc(exp_nr);
	result_i = 0;
	for (i = 0; i < prop1->dpp_nr; i++, result_i++)
		daos_prop_entry_copy(&prop1->dpp_entries[i],
				     &exp_result->dpp_entries[result_i]);
	for (i = 0; i < prop2->dpp_nr; i++, result_i++)
		daos_prop_entry_copy(&prop2->dpp_entries[i],
				     &exp_result->dpp_entries[result_i]);

	expect_merge_result(prop1, prop2, exp_result);

	daos_prop_free(prop1);
	daos_prop_free(prop2);
	daos_prop_free(exp_result);
}

static void
test_daos_prop_merge_total_update(void **state)
{
	daos_prop_t	*prop1;
	daos_prop_t	*prop2;

	prop1 = daos_prop_alloc(2);
	prop1->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop1->dpp_entries[0].dpe_str, "Test",
		  DAOS_PROP_LABEL_MAX_LEN);
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(2);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop2->dpp_entries[0].dpe_str, "Updated",
		  DAOS_PROP_LABEL_MAX_LEN);
	prop2->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop2->dpp_entries[1].dpe_val = 0;

	/* Expecting all props to be overwritten */
	expect_merge_result(prop1, prop2, prop2);

	daos_prop_free(prop1);
	daos_prop_free(prop2);
}

static void
test_daos_prop_merge_subset_update(void **state)
{
	daos_prop_t		*prop1;
	daos_prop_t		*prop2;
	daos_prop_t		*exp_result;
	struct daos_prop_entry	*entry;

	prop1 = daos_prop_alloc(2);
	prop1->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop1->dpp_entries[0].dpe_str, "Test",
		  DAOS_PROP_LABEL_MAX_LEN);
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(1);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop2->dpp_entries[0].dpe_str, "Updated",
		  DAOS_PROP_LABEL_MAX_LEN);

	/* Expecting only one prop to be overwritten */
	exp_result = daos_prop_dup(prop1, false);
	entry = daos_prop_entry_get(exp_result, prop2->dpp_entries[0].dpe_type);
	assert_int_equal(daos_prop_entry_copy(&prop2->dpp_entries[0], entry),
			 0);

	expect_merge_result(prop1, prop2, exp_result);

	daos_prop_free(prop1);
	daos_prop_free(prop2);
	daos_prop_free(exp_result);
}

static void
test_daos_prop_merge_add_and_update(void **state)
{
	daos_prop_t		*prop1;
	daos_prop_t		*prop2;
	uint32_t		exp_nr;
	uint32_t		i, result_i;
	uint32_t		new_idx, dup_idx;
	daos_prop_t		*exp_result;
	struct daos_prop_entry	*entry;

	prop1 = daos_prop_alloc(2);
	prop1->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop1->dpp_entries[0].dpe_str, "Test",
		  DAOS_PROP_LABEL_MAX_LEN);
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(2);
	dup_idx = 0; /* duplicate type to what's in prop1 */
	prop2->dpp_entries[dup_idx].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop2->dpp_entries[dup_idx].dpe_str, "Updated",
		  DAOS_PROP_LABEL_MAX_LEN);
	new_idx = 1; /* type that isn't in prop1 */
	prop2->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	prop2->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_CRC32;

	/* Expecting duplicate prop to be overwritten, and new to be added */
	exp_nr = prop1->dpp_nr + prop2->dpp_nr - 1;
	exp_result = daos_prop_alloc(exp_nr);
	result_i = 0;
	for (i = 0; i < prop1->dpp_nr; i++, result_i++) {
		entry = &exp_result->dpp_entries[result_i];
		assert_int_equal(daos_prop_entry_copy(&prop1->dpp_entries[i],
						      entry), 0);
	}

	entry = &exp_result->dpp_entries[result_i];
	assert_int_equal(daos_prop_entry_copy(&prop2->dpp_entries[new_idx],
					      entry), 0);
	result_i++;
	assert_int_equal(result_i, exp_nr);

	/* Overwrite the entry prop2 is duplicating */
	entry = daos_prop_entry_get(exp_result,
				    prop2->dpp_entries[dup_idx].dpe_type);
	assert_int_equal(daos_prop_entry_copy(&prop2->dpp_entries[dup_idx],
					      entry),
			 0);

	expect_merge_result(prop1, prop2, exp_result);

	daos_prop_free(prop1);
	daos_prop_free(prop2);
	daos_prop_free(exp_result);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_daos_prop_merge_null),
		cmocka_unit_test(test_daos_prop_merge_empty),
		cmocka_unit_test(test_daos_prop_merge_add_only),
		cmocka_unit_test(test_daos_prop_merge_total_update),
		cmocka_unit_test(test_daos_prop_merge_subset_update),
		cmocka_unit_test(test_daos_prop_merge_add_and_update),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
