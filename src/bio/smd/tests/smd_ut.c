/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>

#include <daos/common.h>
#include <daos_srv/smd.h>
#include "../smd_internal.h"
#include <daos/tests_lib.h>
#include <daos/sys_db.h>

#define SMD_STORAGE_PATH	"/mnt/daos"
#define DB_LIST_NR		(SMD_DEV_TYPE_MAX * 2 + 1)

struct ut_db {
	struct sys_db	ud_db;
	d_list_t	ud_lists[DB_LIST_NR];
};

static struct ut_db	ut_db;

struct ut_chain {
	d_list_t	 uc_link;
	void		*uc_key;
	void		*uc_val;
	int		 uc_key_size;
	int		 uc_val_size;
};

d_list_t *
db_name2list(struct sys_db *db, char *name)
{
	struct ut_db *ud = container_of(db, struct ut_db, ud_db);
	enum smd_dev_type st;

	if (!strcmp(name, TABLE_DEV))
		return &ud->ud_lists[0];
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		if (!strcmp(name, TABLE_TGTS[st]))
			return &ud->ud_lists[st + 1];
		if (!strcmp(name, TABLE_POOLS[st]))
			return &ud->ud_lists[st + SMD_DEV_TYPE_MAX + 1];
	}
	D_ASSERT(0);
	return NULL;
}

struct ut_chain *
db_chain_alloc(int key_size, int val_size)
{
	struct ut_chain	*chain;

	chain = calloc(1, sizeof(*chain));
	D_ASSERT(chain);
	chain->uc_key_size = key_size;
	chain->uc_key = calloc(1, key_size);
	D_ASSERT(chain->uc_key);

	chain->uc_val_size = val_size;
	chain->uc_val = calloc(1, val_size);
	D_ASSERT(chain->uc_val);

	return chain;
}

void
db_chain_free(struct ut_chain *chain)
{
	if (chain->uc_key)
		free(chain->uc_key);
	if (chain->uc_val)
		free(chain->uc_val);
	free(chain);
}

struct ut_chain *
db_find(d_list_t *head, d_iov_t *key)
{
	struct ut_chain *chain;

	d_list_for_each_entry(chain, head, uc_link) {
		D_ASSERT(key->iov_len == chain->uc_key_size);
		if (!memcmp(key->iov_buf, chain->uc_key, chain->uc_key_size))
			return chain;
	}
	return NULL;
}

static int
db_fetch(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	d_list_t	*head = db_name2list(db, table);
	struct ut_chain *chain;

	chain = db_find(head, key);
	if (chain) {
		memcpy(val->iov_buf, chain->uc_val, chain->uc_val_size);
		val->iov_len = chain->uc_val_size;
		return 0;
	}
	val->iov_len = 0;
	return -DER_NONEXIST;
}

static int
db_upsert(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	d_list_t	*head = db_name2list(db, table);
	struct ut_chain *chain;

	chain = db_find(head, key);
	if (chain) {
		D_ASSERT(val->iov_len == chain->uc_val_size);
		memcpy(chain->uc_val, val->iov_buf, val->iov_len);
		return 0;
	}
	chain = db_chain_alloc(key->iov_len, val->iov_len);
	D_ASSERT(chain);

	memcpy(chain->uc_key, key->iov_buf, key->iov_len);
	memcpy(chain->uc_val, val->iov_buf, val->iov_len);
	d_list_add_tail(&chain->uc_link, head);
	return 0;
}

static int
db_delete(struct sys_db *db, char *table, d_iov_t *key)
{
	d_list_t	*head = db_name2list(db, table);
	struct ut_chain *chain;

	chain = db_find(head, key);
	if (chain) {
		d_list_del(&chain->uc_link);
		db_chain_free(chain);
		return 0;
	}
	return -DER_NONEXIST;
}

static int
db_traverse(struct sys_db *db, char *table, sys_db_trav_cb_t cb, void *args)
{
	d_list_t	*head = db_name2list(db, table);
	struct ut_chain *chain;

	d_list_for_each_entry(chain, head, uc_link) {
		d_iov_t		key;

		d_iov_set(&key, chain->uc_key, chain->uc_key_size);
		cb(db, table, &key, args);
	}
	return 0;
}

static int
db_init(void)
{
	int	i;

	ut_db.ud_db.sd_fetch	= db_fetch;
	ut_db.ud_db.sd_upsert	= db_upsert;
	ut_db.ud_db.sd_delete	= db_delete;
	ut_db.ud_db.sd_traverse	= db_traverse;

	for (i = 0; i < DB_LIST_NR; i++)
		D_INIT_LIST_HEAD(&ut_db.ud_lists[i]);
	return 0;
}

static void
db_fini(void)
{
	int	i;

	for (i = 0; i < DB_LIST_NR; i++) {
		while (!d_list_empty(&ut_db.ud_lists[i])) {
			struct ut_chain	*chain;

			chain = d_list_entry(ut_db.ud_lists[i].next,
					     struct ut_chain, uc_link);
			d_list_del(&chain->uc_link);
			db_chain_free(chain);
		}
	}
}

uuid_t	dev_id1;
uuid_t	dev_id2;

static int
smd_ut_setup(void **state)
{
	int	rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing the debug instance\n");
		return rc;
	}
	db_init();
	rc = smd_init(&ut_db.ud_db);

	if (rc) {
		print_error("Error initializing SMD store: %d\n", rc);
		daos_debug_fini();
		return rc;
	}

	return 0;
}

static int
smd_ut_teardown(void **state)
{
	smd_fini();
	db_fini();
	daos_debug_fini();
	return 0;
}

static void
verify_dev(struct smd_dev_info *dev_info, uuid_t id, int dev_idx)
{
	int	i;

	assert_int_equal(uuid_compare(dev_info->sdi_id, id), 0);
	if (dev_idx == 1) {
		assert_int_equal(dev_info->sdi_state, SMD_DEV_NORMAL);
		assert_int_equal(dev_info->sdi_tgt_cnt, 3);
		for (i = 0; i < 3; i++)
			assert_int_equal(dev_info->sdi_tgts[i], i);
	} else {
		assert_int_equal(dev_info->sdi_state, SMD_DEV_FAULTY);
		assert_int_equal(dev_info->sdi_tgt_cnt, 3);
		for (i = 3; i < 6; i++)
			assert_int_equal(dev_info->sdi_tgts[i - 3], i);
	}
}

static void
ut_device(void **state)
{
	struct smd_dev_info	*dev_info, *tmp;
	d_list_t		 dev_list;
	uuid_t			 id3;
	enum smd_dev_type	 st;
	int			 i, dev_cnt = 0, rc;

	uuid_generate(dev_id1);
	uuid_generate(dev_id2);
	uuid_generate(id3);

	/* Assigned dev1 to target 0, 1, 2, dev2 to target 3. 4. 5 */
	rc = smd_dev_add_tgt(dev_id1, 0, SMD_DEV_TYPE_DATA);
	assert_rc_equal(rc, 0);

	rc = smd_dev_add_tgt(dev_id1, 0, SMD_DEV_TYPE_DATA);
	assert_rc_equal(rc, -DER_EXIST);

	for (i = 1; i < 3; i++) {
		rc = smd_dev_add_tgt(dev_id1, i, SMD_DEV_TYPE_DATA);
		assert_rc_equal(rc, 0);
	}

	rc = smd_dev_add_tgt(dev_id2, 1, SMD_DEV_TYPE_DATA);
	assert_rc_equal(rc, -DER_EXIST);

	for (i = 3; i < 6; i++) {
		st = (i < 4) ? SMD_DEV_TYPE_DATA : SMD_DEV_TYPE_DATA + i - 3;
		rc = smd_dev_add_tgt(dev_id2, i, st);
		assert_rc_equal(rc, 0);
	}

	rc = smd_dev_set_state(dev_id2, SMD_DEV_FAULTY);
	assert_rc_equal(rc, 0);

	rc = smd_dev_get_by_id(id3, &dev_info);
	assert_rc_equal(rc, -DER_NONEXIST);

	rc = smd_dev_get_by_id(dev_id1, &dev_info);
	assert_rc_equal(rc, 0);
	verify_dev(dev_info, dev_id1, 1);

	smd_dev_free_info(dev_info);

	rc = smd_dev_get_by_tgt(4, SMD_DEV_TYPE_DATA, &dev_info);
	assert_rc_equal(rc, -DER_NONEXIST);

	for (i = 3; i < 6; i++) {
		st = (i < 4) ? SMD_DEV_TYPE_DATA : SMD_DEV_TYPE_DATA + i - 3;
		rc = smd_dev_get_by_tgt(i, st, &dev_info);
		assert_rc_equal(rc, 0);
		verify_dev(dev_info, dev_id2, 2);
		smd_dev_free_info(dev_info);
	}

	D_INIT_LIST_HEAD(&dev_list);
	rc = smd_dev_list(&dev_list, &dev_cnt);
	assert_rc_equal(rc, 0);
	assert_int_equal(dev_cnt, 2);

	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, sdi_link) {
		if (uuid_compare(dev_info->sdi_id, dev_id1) == 0)
			verify_dev(dev_info, dev_id1, 1);
		else if (uuid_compare(dev_info->sdi_id, dev_id2) == 0)
			verify_dev(dev_info, dev_id2, 2);
		else
			assert_true(false);

		d_list_del(&dev_info->sdi_link);
		smd_dev_free_info(dev_info);
	}
}

static void
verify_pool(struct smd_pool_info *pool_info, uuid_t id, int shift)
{
	enum smd_dev_type	st;
	int			i, j;

	assert_int_equal(uuid_compare(pool_info->spi_id, id), 0);
	assert_int_equal(pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA], 4);
	assert_int_equal(pool_info->spi_tgt_cnt[SMD_DEV_TYPE_META], 1);
	assert_int_equal(pool_info->spi_tgt_cnt[SMD_DEV_TYPE_WAL], 1);

	for (i = 0; i < 6; i++) {
		st = (i < 4) ? SMD_DEV_TYPE_DATA : SMD_DEV_TYPE_DATA + i - 3;
		j = (i < 4) ? i : 0;
		assert_int_equal(pool_info->spi_tgts[st][j], i);
		assert_int_equal(pool_info->spi_blobs[st][j], i << shift);
	}
}

static void
ut_pool(void **state)
{
	struct smd_pool_info	*pool_info, *tmp;
	uuid_t			 id1, id2, id3;
	uint64_t		 blob_id;
	d_list_t		 pool_list;
	enum smd_dev_type	 st;
	int			 i, pool_cnt = 0, rc;

	uuid_generate(id1);
	uuid_generate(id2);
	uuid_generate(id3);

	for (i = 0; i < 6; i++) {
		st = (i < 4) ? SMD_DEV_TYPE_DATA : SMD_DEV_TYPE_DATA + i - 3;
		rc = smd_pool_add_tgt(id1, i, i << 10, st, 100);
		assert_rc_equal(rc, 0);

		rc = smd_pool_add_tgt(id2, i, i << 20, st, 200);
		assert_rc_equal(rc, 0);
	}

	rc = smd_pool_add_tgt(id1, 0, 5000, SMD_DEV_TYPE_DATA, 100);
	assert_rc_equal(rc, -DER_EXIST);

	rc = smd_pool_add_tgt(id1, 4, 4 << 10, SMD_DEV_TYPE_DATA, 200);
	assert_rc_equal(rc, -DER_INVAL);

	rc = smd_pool_add_tgt(id1, 4, 5000, SMD_DEV_TYPE_META, 100);
	assert_rc_equal(rc, -DER_EXIST);

	rc = smd_pool_add_tgt(id1, 0, 4 << 10, SMD_DEV_TYPE_META, 200);
	assert_rc_equal(rc, -DER_INVAL);

	rc = smd_pool_add_tgt(id1, 5, 5000, SMD_DEV_TYPE_WAL, 100);
	assert_rc_equal(rc, -DER_EXIST);

	rc = smd_pool_add_tgt(id1, 0, 4 << 10, SMD_DEV_TYPE_WAL, 200);
	assert_rc_equal(rc, -DER_INVAL);

	rc = smd_pool_get_info(id1, &pool_info);
	assert_rc_equal(rc, 0);
	verify_pool(pool_info, id1, 10);

	smd_pool_free_info(pool_info);

	rc = smd_pool_get_info(id3, &pool_info);
	assert_rc_equal(rc, -DER_NONEXIST);

	for (i = 0; i < 6; i++) {
		st = (i < 4) ? SMD_DEV_TYPE_DATA : SMD_DEV_TYPE_DATA + i - 3;
		rc = smd_pool_get_blob(id1, i, st, &blob_id);
		assert_rc_equal(rc, 0);
		assert_int_equal(blob_id, i << 10);

		rc = smd_pool_get_blob(id2, i, st, &blob_id);
		assert_rc_equal(rc, 0);
		assert_int_equal(blob_id, i << 20);
	}

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		rc = smd_pool_get_blob(id1, 6, st, &blob_id);
		assert_rc_equal(rc, -DER_NONEXIST);
	}

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	assert_rc_equal(rc, 0);
	assert_int_equal(pool_cnt, 2);

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		if (uuid_compare(pool_info->spi_id, id1) == 0)
			verify_pool(pool_info, id1, 10);
		else if (uuid_compare(pool_info->spi_id, id2) == 0)
			verify_pool(pool_info, id2, 20);
		else
			assert_true(false);

		d_list_del(&pool_info->spi_link);
		smd_pool_free_info(pool_info);
	}

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		rc = smd_pool_del_tgt(id1, 6, st);
		assert_rc_equal(rc, -DER_NONEXIST);
	}

	for (i = 0; i < 6; i++) {
		st = (i < 4) ? SMD_DEV_TYPE_DATA : SMD_DEV_TYPE_DATA + i - 3;
		rc = smd_pool_del_tgt(id1, i, st);
		assert_rc_equal(rc, 0);

		rc = smd_pool_del_tgt(id2, i, st);
		assert_rc_equal(rc, 0);
	}

	rc = smd_pool_get_info(id1, &pool_info);
	assert_rc_equal(rc, -DER_NONEXIST);
}

static void
ut_dev_replace(void **state)
{
	struct smd_dev_info	*dev_info, *tmp_dev;
	struct smd_pool_info	*pool_info, *tmp_pool;
	uuid_t			 dev_id3, pool_id1, pool_id2;
	d_list_t		 pool_list, dev_list;
	uint64_t		 blob_id;
	int			 i, rc, pool_cnt = 0, dev_cnt = 0;

	uuid_generate(dev_id3);
	uuid_generate(pool_id1);
	uuid_generate(pool_id2);

	/* Assign pools, they were unassigned in prior pool test */
	for (i = 0; i < 4; i++) {
		rc = smd_pool_add_tgt(pool_id1, i, i << 10, SMD_DEV_TYPE_DATA, 100);
		assert_rc_equal(rc, 0);

		rc = smd_pool_add_tgt(pool_id2, i, i << 20, SMD_DEV_TYPE_DATA, 200);
		assert_rc_equal(rc, 0);
	}

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	assert_rc_equal(rc, 0);
	assert_int_equal(pool_cnt, 2);

	/* Assign different blobs to dev1's targets 0, 1, 2 */
	d_list_for_each_entry(pool_info, &pool_list, spi_link) {
		pool_info->spi_blobs[SMD_DEV_TYPE_DATA][0] = 555;
		pool_info->spi_blobs[SMD_DEV_TYPE_DATA][1] = 666;
		pool_info->spi_blobs[SMD_DEV_TYPE_DATA][2] = 777;
	}

	/* Replace dev1 with dev3 without marking dev1 as faulty */
	rc = smd_dev_replace(dev_id1, dev_id3, &pool_list);
	assert_rc_equal(rc, -DER_INVAL);

	rc = smd_dev_set_state(dev_id1, SMD_DEV_FAULTY);
	assert_rc_equal(rc, 0);

	/* Replace dev1 with dev2 */
	rc = smd_dev_replace(dev_id1, dev_id2, &pool_list);
	assert_rc_equal(rc, -DER_INVAL);

	/* Replace dev1 with dev3 */
	rc = smd_dev_replace(dev_id1, dev_id3, &pool_list);
	assert_rc_equal(rc, 0);

	/* Verify device after replace */
	D_INIT_LIST_HEAD(&dev_list);
	rc = smd_dev_list(&dev_list, &dev_cnt);
	assert_rc_equal(rc, 0);
	assert_int_equal(dev_cnt, 2);

	d_list_for_each_entry_safe(dev_info, tmp_dev, &dev_list, sdi_link) {
		if (uuid_compare(dev_info->sdi_id, dev_id3) == 0)
			verify_dev(dev_info, dev_id3, 1);
		else if (uuid_compare(dev_info->sdi_id, dev_id2) == 0)
			verify_dev(dev_info, dev_id2, 2);
		else
			assert_true(false);

		d_list_del(&dev_info->sdi_link);
		smd_dev_free_info(dev_info);
	}

	/* Verify blob IDs after device replace */
	d_list_for_each_entry_safe(pool_info, tmp_pool, &pool_list, spi_link) {
		for (i = 0; i < 3; i++) {
			rc = smd_pool_get_blob(pool_info->spi_id, i, SMD_DEV_TYPE_DATA, &blob_id);
			assert_rc_equal(rc, 0);
			if (i == 0)
				assert_int_equal(blob_id, 555);
			else if (i == 1)
				assert_int_equal(blob_id, 666);
			else
				assert_int_equal(blob_id, 777);
		}
		d_list_del(&pool_info->spi_link);
		smd_pool_free_info(pool_info);
	}
}

static const struct CMUnitTest smd_uts[] = {
	{ "smd_ut_device", ut_device, NULL, NULL},
	{ "smd_ut_pool", ut_pool, NULL, NULL},
	{ "smd_ut_dev_replace", ut_dev_replace, NULL, NULL},
};

static void
print_usage(char *name)
{
	print_message(
		"\n\nCOMMON TESTS\n==========================\n");
	print_message("%s -h|--help\n", name);
}

const char *s_opts = "hl";
static int idx;
static struct option l_opts[] = {
	{"help", no_argument,	NULL, 'h'},
};

int main(int argc, char **argv)
{
	int	rc;
	int	opt;

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_PRINT("Error initializing ABT\n");
		return rc;
	}

	while ((opt = getopt_long(argc, argv, s_opts, l_opts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			print_usage(argv[0]);
			rc = 0;
			goto out;
		default:
			rc = 1;
			goto out;
		}
	}
	rc = cmocka_run_group_tests_name("SMD unit tests", smd_uts,
					 smd_ut_setup, smd_ut_teardown);

out:
	ABT_finalize();
	return rc;
}
