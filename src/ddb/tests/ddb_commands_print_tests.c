/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <ddb_vos.h>
#include <ddb_printer.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

static struct ddb_ctx ctx = {.dc_io_ft.ddb_print_message = dvt_fake_print};

#define assert_str_exact(str) assert_string_equal(str, dvt_fake_print_buffer)
#define assert_str(str) assert_string_contains(dvt_fake_print_buffer, str)

static void
print_container_test(void **state)
{
	struct ddb_cont cont = {0};

	uuid_parse("12345678-1234-1243-1243-123456789012", cont.ddbc_cont_uuid);
	cont.ddbc_idx = 1;

	ddb_print_cont(&ctx, &cont);
	assert_str_exact("[1] 12345678-1234-1243-1243-123456789012\n");
}

static void
print_object_test(void **state)
{
	struct ddb_obj obj = {0};

	obj.ddbo_idx = 2;
	obj.ddbo_oid.lo = 1;
	obj.ddbo_oid.hi = 10;
	obj.ddbo_nr_grps = 2;
	strcpy(obj.ddbo_obj_class_name, "TEST CLASS");
	strcpy(obj.ddbo_otype_str, "TEST TYPE");

	ddb_print_obj(&ctx, &obj, 1);

	assert_str_exact("\t[2] '10.1' (class: TEST CLASS, type: TEST TYPE, groups: 2)\n");
}

static void set_key_buf(struct ddb_key	*key, uint32_t len)
{
	int i;

	for (i = 0; i < len; i++)
		((uint8_t *)key->ddbk_key.iov_buf)[i] = (i % 16 + 0x1);

	key->ddbk_key.iov_len = len;
}

static void
print_key_test(void **state)
{
	struct ddb_key	key = {0};
	char		key_buf[1024] = {0};
	uint64_t	ll = 0x1abc2abc3abc4abc;
	int		i = 0x1234abcd;
	short		s = 0xabcd;

	key.ddbk_idx = 4;
	d_iov_set(&key.ddbk_key, key_buf, ARRAY_SIZE(key_buf));

	ddb_print_key(&ctx, &key, 2);

	/* empty large key */
	assert_str_exact("\t\t[4] '' (1024)\n");
	dvt_fake_print_reset();

	/* Large key buffer, but only part is text */
	strcpy(key_buf, "string key");
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] 'string key' (1024)\n");
	dvt_fake_print_reset();

	/* No ending '\0' */
	strcpy(key_buf, "abcdefghijklmnopqrstuvwxyz");
	key.ddbk_key.iov_len = 5;
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] 'abcde' (5)\n");
	dvt_fake_print_reset();

	/* With ending '\0' in middle ... only prints to null terminator */
	strcpy(key_buf, "abcdefghijklmnopqrstuvwxyz");
	key_buf[10] = '\0';
	key.ddbk_key.iov_len = 26;
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] 'abcdefghij' (26)\n");
	dvt_fake_print_reset();

	/*
	 * Print binary keys.
	 * If key length is a number type, then print as that.
	 */
	memset(key_buf, 0, ARRAY_SIZE(key_buf));

	/* char key */
	key_buf[0] = 0xab;
	key.ddbk_key.iov_len = sizeof(char);
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{uint8:0xab}'\n");
	dvt_fake_print_reset();

	/* short key */
	key.ddbk_key.iov_buf = (uint8_t *)&s;
	key.ddbk_key.iov_len = sizeof(short);
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{uint16:0xabcd}'\n");
	dvt_fake_print_reset();

	/* int key */
	key.ddbk_key.iov_buf = (int *)&i;
	key.ddbk_key.iov_len = sizeof(int);
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{uint32:0x1234abcd}'\n");
	dvt_fake_print_reset();

	/* 64 bit key */
	key.ddbk_key.iov_buf = (uint64_t *)&ll;
	key.ddbk_key.iov_len = sizeof(uint64_t);
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{uint64:0x1abc2abc3abc4abc}'\n");
	dvt_fake_print_reset();

	/* random length binary key */
	key_buf[0] = 0xaa;
	key_buf[1] = 0xbb;
	key_buf[2] = 0xcc;
	key.ddbk_key.iov_buf = key_buf;
	key.ddbk_key.iov_len = 3;
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{bin(3):0xaabbcc}'\n");
	dvt_fake_print_reset();

	set_key_buf(&key, 12);
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{bin(12):0x0102030405060708090a0b0c}'\n");
	dvt_fake_print_reset();

	set_key_buf(&key, 128);
	ddb_print_key(&ctx, &key, 0);
	assert_str_exact("[4] '{bin(128):0x0102030405...0c0d0e0f10}'\n");
	dvt_fake_print_reset();
}

static void
print_sv_test(void **state)
{
	struct ddb_sv sv = {.ddbs_record_size = 19089555};

	ddb_print_sv(&ctx, &sv, 1);
	assert_str_exact("\t[0] Single Value (Length: 19089555 bytes)\n");
}

static void
print_array_test(void **state)
{
	struct ddb_array array = {
		.ddba_recx.rx_idx = 64,
		.ddba_recx.rx_nr = 128,
		.ddba_record_size = 3,
		.ddba_idx = 8,
	};

	ddb_print_array(&ctx, &array, 2);
	assert_str_exact("\t\t[8] Array Value (Length: 128 records, "
		   "Record Indexes: {64-191}, Record Size: 3)\n");
}

#define assert_hr_bytes(expected_str, bytes) \
	do { \
		uint32_t __buf_len = 32; \
		char __buf[__buf_len]; \
	ddb_bytes_hr(bytes, __buf, __buf_len); \
	assert_string_equal(expected_str, __buf); \
	} while (0)

static void
bytes_hr_tests(void **state)
{
	assert_hr_bytes("1KB", 1024);
	assert_hr_bytes("1KB", 1025);
	assert_hr_bytes("1KB", 1025);
	assert_hr_bytes("1KB", 1024 + 50);
	assert_hr_bytes("2KB", 1024 * 2);
	assert_hr_bytes("1MB", 1024 * 1024);
	assert_hr_bytes("1GB", 1024 * 1024 * 1024);
	assert_hr_bytes("1TB", 0x10000000000);
}

static void
print_superblock_test(void **state)
{
	struct ddb_superblock sb = {
		.dsb_scm_sz = 0x100000000, /* 4 GB */
		.dsb_nvme_sz = 0x40000000000, /* 4 TB */
		.dsb_cont_nr = 2,
		.dsb_durable_format_version = 23,
		.dsb_blk_sz = 4096,
		.dsb_hdr_blks = 1024,
		.dsb_tot_blks = 0x40000000000,
	};

	uuid_parse("12345678-1234-1234-1234-123456789012", sb.dsb_id);

	ddb_print_superblock(&ctx, &sb);

	assert_str("Pool UUID: 12345678-1234-1234-1234-123456789012\n");
	assert_str("Format Version: 23\n");
	assert_str("Containers: 2\n");
	assert_str("SCM Size: 4GB\n");
	assert_str("NVME Size: 4TB\n");
	assert_str("Block Size: 4KB\n");
	assert_str("Reserved Blocks: 1024\n");
	assert_str("Block Device Capacity: 4TB\n");
}

static void
print_ilog_test(void **state)
{
	struct ddb_ilog_entry ilog = {
		.die_status = 1,
		.die_status_str = "TEST STATUS",
		.die_epoch = 1234567890,
		.die_idx = 1,
		.die_tx_id = 2
	};

	ddb_print_ilog_entry(&ctx, &ilog);

	assert_str_exact("Index: 1\n"
			 "Status: TEST STATUS (1)\n"
			 "Epoch: 1234567890\n"
			 "Txn ID: 2\n");
}

static void
print_dtx_active(void **state)
{
	struct dv_dtx_active_entry entry = {
		.ddtx_uuid = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc},
		.ddtx_reindex = false,
		.ddtx_exist = true,
		.ddtx_invalid = false,
		.ddtx_handle_time = 12345690,
		.ddtx_epoch = 99,
		.ddtx_oid_cnt = 1,
		.ddtx_start_time = 489387371,
		.ddtx_committable = true,
		.ddtx_committed = false,
		.ddtx_aborted = false,
		.ddtx_maybe_shared = true,
		.ddtx_prepared = true,
		.ddtx_grp_cnt = 3,
		.ddtx_ver = 1,
		.ddtx_rec_cnt = 1,
		.ddtx_mbs_flags = 1,
		.ddtx_flags = 0,
		.ddtx_oid = g_oids[0],
	};

	ddb_print_dtx_active(&ctx, &entry);

	assert_str("UUID: 12345678-9abc-0000-0000-000000000000\n");
	assert_str("Epoch: 99\n");
	assert_str("Exist: true\n");
	assert_str("Invalid: false\n");
	assert_str("Reindex: false\n");
	assert_str("Handle Time: 12345690\n");
	assert_str("Oid Cnt: 1\n");
	assert_str("Start Time: 489387371\n");
	assert_str("Committable: true\n");
	assert_str("Committed: false\n");
	assert_str("Aborted: false\n");
	assert_str("Maybe Shared: true\n");
	assert_str("Prepared: true\n");
	assert_str("Grp Cnt: 3\n");
	assert_str("Ver: 1\n");
	assert_str("Rec Cnt: 1\n");
	assert_str("Mbs Flags: 1\n");
	assert_str("Flags: 0\n");
	assert_str("Oid: 281479271743488.4294967296.0\n");
}

static void
print_dtx_committed(void **state)
{
	struct dv_dtx_committed_entry entry = {
		.ddtx_epoch = 1234,
		.ddtx_exist = true,
		.ddtx_invalid = false,
		.ddtx_uuid = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc},
	};

	ddb_print_dtx_committed(&ctx, &entry);

	assert_str("UUID: 12345678-9abc-0000-0000-000000000000\n");
	assert_str("Epoch: 1234\n");
	assert_str("Exist: true\n");
	assert_str("Invalid: false\n");
}

static int
ddb_print_setup(void **state)
{
	dvt_fake_print_reset();
	return 0;
}

#define TEST(x) { #x, x, ddb_print_setup, NULL }
static const struct CMUnitTest tests[] = {
	TEST(print_container_test),
	TEST(print_object_test),
	TEST(print_key_test),
	TEST(print_sv_test),
	TEST(print_array_test),
	TEST(bytes_hr_tests),
	TEST(print_superblock_test),
	TEST(print_ilog_test),
	TEST(print_dtx_active),
	TEST(print_dtx_committed),
};

int
ddb_commands_print_tests_run()
{
	return cmocka_run_group_tests_name("ddb commands printer", tests, NULL, NULL);
}
