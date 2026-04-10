/*
 * (C) Copyright 2020-2022 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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

#include <daos/tests_lib.h>
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
			assert_rc_equal(daos_prop_entry_cmp_acl(entry,
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
	D_STRNDUP_S(prop->dpp_entries[0].dpe_str, "Test");
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
	D_STRNDUP_S(prop1->dpp_entries[0].dpe_str, "Test");
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(3);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_OWNER;
	D_STRNDUP_S(prop2->dpp_entries[0].dpe_str, "test@");
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
	D_STRNDUP_S(prop1->dpp_entries[0].dpe_str, "Test");
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(2);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP_S(prop2->dpp_entries[0].dpe_str, "Updated");
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
	D_STRNDUP_S(prop1->dpp_entries[0].dpe_str, "Test");
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(1);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP_S(prop2->dpp_entries[0].dpe_str, "Updated");

	/* Expecting only one prop to be overwritten */
	exp_result = daos_prop_dup(prop1, false /* pool */, true /* input */);
	entry = daos_prop_entry_get(exp_result, prop2->dpp_entries[0].dpe_type);
	assert_int_equal(daos_prop_entry_copy(&prop2->dpp_entries[0], entry), 0);

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
	D_STRNDUP_S(prop1->dpp_entries[0].dpe_str, "Test");
	prop1->dpp_entries[1].dpe_type = DAOS_PROP_CO_COMPRESS;
	prop1->dpp_entries[1].dpe_val = 1;

	prop2 = daos_prop_alloc(2);
	dup_idx = 0; /* duplicate type to what's in prop1 */
	prop2->dpp_entries[dup_idx].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP_S(prop2->dpp_entries[dup_idx].dpe_str, "Updated");
	new_idx = 1; /* type that isn't in prop1 */
	prop2->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	prop2->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_CRC32;

	/* Expecting duplicate prop to be overwritten, and new to be added */
	exp_nr = prop1->dpp_nr + prop2->dpp_nr - 1;
	exp_result = daos_prop_alloc(exp_nr);
	result_i = 0;
	for (i = 0; i < prop1->dpp_nr; i++, result_i++) {
		entry = &exp_result->dpp_entries[result_i];
		assert_rc_equal(daos_prop_entry_copy(&prop1->dpp_entries[i],
						     entry), 0);
	}

	entry = &exp_result->dpp_entries[result_i];
	assert_rc_equal(daos_prop_entry_copy(&prop2->dpp_entries[new_idx],
					     entry), 0);
	result_i++;
	assert_int_equal(result_i, exp_nr);

	/* Overwrite the entry prop2 is duplicating */
	entry = daos_prop_entry_get(exp_result,
				    prop2->dpp_entries[dup_idx].dpe_type);
	assert_rc_equal(daos_prop_entry_copy(&prop2->dpp_entries[dup_idx],
					      entry),
			0);

	expect_merge_result(prop1, prop2, exp_result);

	daos_prop_free(prop1);
	daos_prop_free(prop2);
	daos_prop_free(exp_result);
}

static void
test_daos_prop_from_str(void **state)
{
	/** Valid prop entries & values */
	char		*LABEL		= "label:hello";
	char		*CSUM		= "cksum:crc64";
	char		*CSUM_SIZE	= "cksum_size:1048576";
	char		*DEDUP		= "dedup:hash";
	char		*DEDUP_TH	= "dedup_threshold:8192";
	char		*COMP		= "compression:lz4";
	char		*ENC		= "encryption:aes-xts128";
	char		*RF		= "rd_fac:2";
	char		*RF_OLD		= "rf:2";
	char		*EC_CELL	= "ec_cell_sz:2021";
	char		*EC_PDA		= "ec_pda:1";
	char		*RP_PDA		= "rp_pda:4";

	/** Valid prop entries, wrong values */
	char		*CSUM_INV	= "cksum:crc2000";
	char            *RF_INV		= "rd_fac:64";
	char            *RF_OLD_INV	= "rf:64";

	/** Read only props, that should not be parsed */
	char		*OID		= "alloc_oid:25";
	char		*LAYOUT		= "layout_type:posix";

	/** Invalid prop entries */
	char		*PROP_INV1	= "hello:world";
	char		*PROP_INV2	= "helloworld";
	char		*PROP_INV3	= ":helloworld";
	char		*PROP_INV4	= "helloworld:";

	char			buf[1024] = {0};
	daos_prop_t		*prop;
	struct daos_prop_entry  *entry;
	int			rc;

	rc = daos_prop_from_str(NULL, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	rc = daos_prop_from_str(buf, sizeof(buf), NULL);
	assert_int_equal(rc, -DER_INVAL);
	rc = daos_prop_from_str(buf, 0, &prop);
	assert_int_equal(rc, -DER_INVAL);

	/** Buffer containing read only entries should fail */
	sprintf(buf, "%s;%s", CSUM, OID);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s", CSUM, LAYOUT);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);

	/** Buffer containing invalid entries should fail */
	sprintf(buf, "%s;%s;%s", CSUM, LABEL, PROP_INV1);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s;%s", CSUM, LABEL, PROP_INV2);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s;%s", CSUM, LABEL, PROP_INV3);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s;%s", CSUM, LABEL, PROP_INV4);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);

	/** Buffer containing invalid values should fail */
	sprintf(buf, "%s;%s", CSUM_INV, RF);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s", CSUM_INV, RF_OLD);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s", CSUM, RF_INV);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);
	sprintf(buf, "%s;%s", CSUM, RF_OLD_INV);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, -DER_INVAL);

	sprintf(buf, "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s",
		LABEL, CSUM, CSUM_SIZE, DEDUP, DEDUP_TH, COMP, ENC, RF,
		EC_CELL, EC_PDA, RP_PDA);
	rc = daos_prop_from_str(buf, sizeof(buf), &prop);
	assert_int_equal(rc, 0);

	/** verify entry values */
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LABEL);
	assert_string_equal(entry->dpe_str, "hello");

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_CSUM);
	assert_int_equal(entry->dpe_val, DAOS_PROP_CO_CSUM_CRC64);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_CSUM_CHUNK_SIZE);
	assert_int_equal(entry->dpe_val, 1048576);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_DEDUP);
	assert_int_equal(entry->dpe_val, DAOS_PROP_CO_DEDUP_HASH);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_DEDUP_THRESHOLD);
	assert_int_equal(entry->dpe_val, 8192);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_COMPRESS);
	assert_int_equal(entry->dpe_val, DAOS_PROP_CO_COMPRESS_LZ4);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ENCRYPT);
	assert_int_equal(entry->dpe_val, DAOS_PROP_CO_ENCRYPT_AES_XTS128);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
	assert_int_equal(entry->dpe_val, DAOS_PROP_CO_REDUN_RF2);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_EC_CELL_SZ);
	assert_int_equal(entry->dpe_val, 2021);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_EC_PDA);
	assert_int_equal(entry->dpe_val, 1);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_RP_PDA);
	assert_int_equal(entry->dpe_val, 4);

	daos_prop_free(prop);
}

static void
test_daos_prop_valid_null(void **state)
{
	assert_false(daos_prop_valid(NULL, false, false));
}

static void
test_daos_prop_valid_too_many(void **state)
{
	daos_prop_t *prop = daos_prop_alloc(0);
	uint32_t     nr   = prop->dpp_nr;

	prop->dpp_nr = DAOS_PROP_ENTRIES_MAX_NR + 1;

	assert_false(daos_prop_valid(prop, true, false));

	prop->dpp_nr = nr;
	daos_prop_free(prop);
}

static void
test_daos_prop_valid_inconsistent_nr_entries(void **state)
{
	const int               nr      = 2;
	daos_prop_t            *prop    = daos_prop_alloc(nr);
	struct daos_prop_entry *entries = prop->dpp_entries;

	D_PRINT("zero entries, but non-null array\n");
	prop->dpp_nr = 0;
	assert_false(daos_prop_valid(prop, false, false));

	D_PRINT("non-zero entries, but null array\n");
	prop->dpp_nr      = nr;
	prop->dpp_entries = NULL;
	assert_false(daos_prop_valid(prop, false, false));

	/* cleanup */
	prop->dpp_entries = entries;
	daos_prop_free(prop);
}

static daos_prop_t *
get_prop_with_duplicates(int total_nr, bool pool)
{
	daos_prop_t *prop = daos_prop_alloc(total_nr);
	int          i;
	uint32_t     prop_min;

	assert_true(total_nr >= 2);

	if (pool)
		prop_min = DAOS_PROP_PO_MIN;
	else
		prop_min = DAOS_PROP_CO_MIN;

	for (i = 0; i < total_nr; i++)
		prop->dpp_entries[i].dpe_type = prop_min + i + 1;

	/* last one is a duplicate type */
	prop->dpp_entries[total_nr - 1].dpe_type = prop->dpp_entries[total_nr - 2].dpe_type;

	return prop;
}

static void
check_daos_prop_valid_duplicate_types(bool pool)
{
	daos_prop_t *prop;

	prop = get_prop_with_duplicates(5, pool);

	assert_false(daos_prop_valid(prop, pool, false));

	daos_prop_free(prop);
}

static void
test_daos_prop_valid_duplicate_types(void **state)
{
	D_PRINT("check pool prop with duplicate types\n");
	check_daos_prop_valid_duplicate_types(true);

	D_PRINT("check container prop with duplicate types\n");
	check_daos_prop_valid_duplicate_types(false);
}

static void
test_daos_prop_valid_pool_success_no_val_check(void **state)
{
	const int    nr   = DAOS_PROP_PO_MAX - DAOS_PROP_PO_MIN - 1;
	daos_prop_t *prop = daos_prop_alloc(nr);
	int          i;

	for (i = 0; i < nr; i++)
		prop->dpp_entries[i].dpe_type = DAOS_PROP_PO_MIN + i + 1;

	assert_true(daos_prop_valid(prop, true, false));

	daos_prop_free(prop);
}

static void
test_daos_prop_valid_cont_success_no_val_check(void **state)
{
	const int    nr   = DAOS_PROP_CO_MAX - DAOS_PROP_CO_MIN - 1;
	daos_prop_t *prop = daos_prop_alloc(nr);
	int          i;

	for (i = 0; i < nr; i++)
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_MIN + i + 1;

	assert_true(daos_prop_valid(prop, false, false));

	daos_prop_free(prop);
}

static void
test_daos_prop_has_byteval_types(void **state)
{
	struct daos_prop_entry entry = {0};

	entry.dpe_type = DAOS_PROP_PO_POOL_CA;
	assert_true(daos_prop_has_byteval(&entry));

	entry.dpe_type = DAOS_PROP_PO_CERT_WATERMARKS;
	assert_true(daos_prop_has_byteval(&entry));

	/* Non-byteval types must remain non-byteval. */
	entry.dpe_type = DAOS_PROP_PO_LABEL;
	assert_false(daos_prop_has_byteval(&entry));
	entry.dpe_type = DAOS_PROP_PO_ACL;
	assert_false(daos_prop_has_byteval(&entry));

	/* And they must NOT also be advertised as ptr-typed: byteval has
	 * its own copy/free path that owns the daos_prop_byteval wrapper. */
	entry.dpe_type = DAOS_PROP_PO_POOL_CA;
	assert_false(daos_prop_has_ptr(&entry));
	entry.dpe_type = DAOS_PROP_PO_CERT_WATERMARKS;
	assert_false(daos_prop_has_ptr(&entry));
}

/*
 * The byteval tests below exercise byteval semantics, not any particular
 * property's payload format. They use whichever prop types currently
 * report has_byteval=true as fixtures; payloads are opaque bytes.
 */

static void
test_daos_prop_byteval_set_round_trip(void **state)
{
	const uint8_t             payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03};
	daos_prop_t              *prop;
	struct daos_prop_entry   *entry;
	struct daos_prop_byteval *bv;

	prop = daos_prop_alloc(1);
	assert_non_null(prop);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_POOL_CA;

	assert_rc_equal(daos_prop_set_byteval(prop, DAOS_PROP_PO_POOL_CA, payload, sizeof(payload)),
			0);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_POOL_CA);
	assert_non_null(entry);
	bv = entry->dpe_val_ptr;
	assert_non_null(bv);
	assert_int_equal(bv->dpb_len, sizeof(payload));
	assert_memory_equal(bv->dpb_data, payload, sizeof(payload));

	daos_prop_free(prop);
}

static void
test_daos_prop_byteval_oversize_rejected(void **state)
{
	struct daos_prop_entry entry = {0};
	uint8_t               *blob;
	size_t                 oversize;
	int                    rc;

	entry.dpe_type = DAOS_PROP_PO_POOL_CA;

	oversize = (size_t)DAOS_PROP_BYTEVAL_MAX_LEN + 1;
	D_ALLOC(blob, 16);
	assert_non_null(blob);

	rc = daos_prop_entry_set_byteval(&entry, blob, oversize);
	assert_rc_equal(rc, -DER_INVAL);
	assert_null(entry.dpe_val_ptr);

	D_FREE(blob);
}

static void
test_daos_prop_byteval_dup_preserves_value(void **state)
{
	const uint8_t             payload[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
	daos_prop_t              *src;
	daos_prop_t              *dst;
	struct daos_prop_entry   *src_entry;
	struct daos_prop_entry   *dst_entry;
	struct daos_prop_byteval *src_bv;
	struct daos_prop_byteval *dst_bv;

	src = daos_prop_alloc(1);
	assert_non_null(src);
	src->dpp_entries[0].dpe_type = DAOS_PROP_PO_CERT_WATERMARKS;
	assert_rc_equal(
	    daos_prop_set_byteval(src, DAOS_PROP_PO_CERT_WATERMARKS, payload, sizeof(payload)), 0);

	dst = daos_prop_dup(src, true /* pool */, false /* input */);
	assert_non_null(dst);

	src_entry = daos_prop_entry_get(src, DAOS_PROP_PO_CERT_WATERMARKS);
	dst_entry = daos_prop_entry_get(dst, DAOS_PROP_PO_CERT_WATERMARKS);
	assert_non_null(src_entry);
	assert_non_null(dst_entry);

	src_bv = src_entry->dpe_val_ptr;
	dst_bv = dst_entry->dpe_val_ptr;
	assert_non_null(dst_bv);
	assert_ptr_not_equal(src_bv, dst_bv);
	assert_int_equal(dst_bv->dpb_len, sizeof(payload));
	assert_memory_equal(dst_bv->dpb_data, payload, sizeof(payload));

	daos_prop_free(src);
	daos_prop_free(dst);
}

static int
suite_setup(void **state)
{
	return d_log_init();
}

static int
suite_teardown(void **state)
{
	d_log_fini();
	return 0;
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
	    cmocka_unit_test(test_daos_prop_from_str),
	    cmocka_unit_test(test_daos_prop_valid_null),
	    cmocka_unit_test(test_daos_prop_valid_too_many),
	    cmocka_unit_test(test_daos_prop_valid_inconsistent_nr_entries),
	    cmocka_unit_test(test_daos_prop_valid_duplicate_types),
	    cmocka_unit_test(test_daos_prop_valid_pool_success_no_val_check),
	    cmocka_unit_test(test_daos_prop_valid_cont_success_no_val_check),
	    cmocka_unit_test(test_daos_prop_has_byteval_types),
	    cmocka_unit_test(test_daos_prop_byteval_set_round_trip),
	    cmocka_unit_test(test_daos_prop_byteval_oversize_rejected),
	    cmocka_unit_test(test_daos_prop_byteval_dup_preserves_value),
	};

	return cmocka_run_group_tests_name("common_prop", tests, suite_setup, suite_teardown);
}
