/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/tests/suite/daos_kv.c
 */
#include <daos.h>
#include <daos_kv.h>
#include "daos_test.h"

#if D_HAS_WARNING(4, "-Wframe-larger-than")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

static daos_ofeat_t feat = DAOS_OF_KV_FLAT;

static void simple_put_get(void **state);

#define NUM_KEYS 1000
#define ENUM_KEY_NR     1000
#define ENUM_DESC_NR    10
#define ENUM_DESC_BUF   (ENUM_DESC_NR * ENUM_KEY_NR)

static void
list_keys(daos_handle_t oh, int *num_keys)
{
	char		*buf;
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	int		key_nr = 0;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;

	buf = malloc(ENUM_DESC_BUF);
	d_iov_set(&sg_iov, buf, ENUM_DESC_BUF);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	nr = ENUM_DESC_NR;
		int		rc;

		memset(buf, 0, ENUM_DESC_BUF);
		rc = daos_kv_list(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor,
				  NULL);
		assert_int_equal(rc, 0);

		if (nr == 0)
			continue;
#if 0
		uint32_t	i;
		char		*ptr;

		for (ptr = buf, i = 0; i < nr; i++) {
			char key[10];

			snprintf(key, kds[i].kd_key_len + 1, "%s", ptr);
			print_message("i:%d dkey:%s len:%d\n",
				      i + key_nr, key, (int)kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
		}
#endif
		key_nr += nr;
	}
	*num_keys = key_nr;
}

static void
simple_put_get(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_size_t	buf_size = 1024, size;
	daos_event_t	ev;
	const char      *key_fmt = "key%d";
	char		key[10];
	char		*buf;
	char		*buf_out;
	int		i, num_keys;
	int		rc;

	D_ALLOC(buf, buf_size);
	assert_non_null(buf);
	dts_buf_render(buf, buf_size);

	D_ALLOC(buf_out, buf_size);
	assert_non_null(buf_out);

	oid = dts_oid_gen(OC_SX, feat, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** open the object */
	rc = daos_kv_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	rc = daos_kv_put(oh, DAOS_TX_NONE, 0, NULL, buf_size, buf, NULL);
	assert_int_equal(rc, -DER_INVAL);
	rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key, 0, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);
	rc = daos_kv_get(oh, DAOS_TX_NONE, 0, NULL, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);
	rc = daos_kv_remove(oh, DAOS_TX_NONE, 0, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	print_message("Inserting %d Keys\n", NUM_KEYS);
	/** Insert Keys */
	for (i = 0; i < NUM_KEYS; i++) {
		sprintf(key, key_fmt, i);
		rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key, buf_size, buf,
				 arg->async ? &ev : NULL);

		if (arg->async) {
			bool ev_flag;

			rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
			assert_int_equal(rc, 0);
			assert_int_equal(ev_flag, true);
			assert_int_equal(ev.ev_error, 0);
		}
	}

	print_message("Overwriting Last Key\n");
	/** overwrite the last key */
	{
		int value = NUM_KEYS;

		sprintf(key, key_fmt, NUM_KEYS-1);
		rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key, sizeof(int), &value,
				 arg->async ? &ev : NULL);

		if (arg->async) {
			bool ev_flag;

			rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
			assert_int_equal(rc, 0);
			assert_int_equal(ev_flag, true);
			assert_int_equal(ev.ev_error, 0);
		}
	}

	print_message("Enumerating Keys\n");
	list_keys(oh, &num_keys);
	assert_int_equal(num_keys, NUM_KEYS);

	print_message("Reading and Checking Keys\n");
	/** read and verify the keys */
	for (i = 0; i < NUM_KEYS; i++) {
		size_t	tmp_size = sizeof(int) * 4;
		int	tmp_buf[4];

		memset(buf_out, 0, buf_size);

		sprintf(key, key_fmt, i);

		/** 1st test: just query the value size */
		size = DAOS_REC_ANY;
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, key, &size, NULL,
				 arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);
		if (arg->async) {
			bool ev_flag;

			rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
			assert_int_equal(rc, 0);
			assert_int_equal(ev_flag, true);
			assert_int_equal(ev.ev_error, 0);
		}
		if (i != NUM_KEYS - 1)
			assert_int_equal(size, buf_size);
		else
			assert_int_equal(size, sizeof(int));

		/** 2nd test: get value with small buffer */
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, key, &tmp_size, tmp_buf,
				 arg->async ? &ev : NULL);
		if (arg->async) {
			bool ev_flag;

			rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
			assert_int_equal(rc, 0);
			assert_int_equal(ev_flag, true);
			rc = ev.ev_error;
		}
		if (i != NUM_KEYS - 1) {
			assert_int_equal(rc, -DER_REC2BIG);
			assert_int_equal(tmp_size, buf_size);
		} else {
			assert_int_equal(rc, 0);
			assert_int_equal(tmp_size, sizeof(int));
			assert_int_equal(NUM_KEYS, tmp_buf[0]);
		}

		/** 3rd test: get value with exact buffer*/
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, key, &size, buf_out,
				 arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);
		if (arg->async) {
			bool ev_flag;

			rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
			assert_int_equal(rc, 0);
			assert_int_equal(ev_flag, true);
			assert_int_equal(ev.ev_error, 0);
		}
		if (i != NUM_KEYS - 1) {
			assert_int_equal(size, buf_size);
			assert_memory_equal(buf_out, buf, size);
		} else {
			assert_int_equal(size, sizeof(int));
			assert_int_equal(NUM_KEYS, *((int *)buf_out));
		}
	}


	print_message("Remove 10 Keys\n");
	for (i = 0; i < 10; i++) {
		sprintf(key, key_fmt, i);
		rc = daos_kv_remove(oh, DAOS_TX_NONE, 0, key, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("Enumerating Keys\n");
	list_keys(oh, &num_keys);
	assert_int_equal(num_keys, NUM_KEYS - 10);

	print_message("Destroying KV\n");
	rc = daos_kv_destroy(oh, DAOS_TX_NONE, NULL);
	assert_int_equal(rc, 0);

	rc = daos_kv_close(oh, NULL);
	assert_int_equal(rc, 0);

	D_FREE(buf_out);
	D_FREE(buf);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	print_message("all good\n");
} /* End simple_put_get */

static void
kv_cond_ops(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	int		val, val_out;
	size_t		size;
	int		rc;

	oid = dts_oid_gen(OC_SX, feat, arg->myrank);

	/** open the object */
	rc = daos_kv_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	val_out = 5;
	size = sizeof(int);
	print_message("Conditional FETCH of non existent Key(should fail)\n");
	rc = daos_kv_get(oh, DAOS_TX_NONE, DAOS_COND_KEY_GET, "Key2",
			 &size, &val_out, NULL);
	assert_int_equal(rc, -DER_NONEXIST);
	assert_int_equal(val_out, 5);

	val = 1;
	print_message("Conditional UPDATE of non existent Key(should fail)\n");
	rc = daos_kv_put(oh, DAOS_TX_NONE, DAOS_COND_KEY_UPDATE, "Key1",
			 sizeof(int), &val, NULL);
	assert_int_equal(rc, -DER_NONEXIST);

	print_message("Conditional INSERT of non existent Key\n");
	rc = daos_kv_put(oh, DAOS_TX_NONE, DAOS_COND_KEY_INSERT, "Key1",
			 sizeof(int), &val, NULL);
	assert_int_equal(rc, 0);

	val = 2;
	print_message("Conditional INSERT of existing Key (Should fail)\n");
	rc = daos_kv_put(oh, DAOS_TX_NONE, DAOS_COND_KEY_INSERT, "Key1",
			 sizeof(int), &val, NULL);
	assert_int_equal(rc, -DER_EXIST);

	size = sizeof(int);
	print_message("Conditional FETCH of existing Key\n");
	rc = daos_kv_get(oh, DAOS_TX_NONE, DAOS_COND_KEY_GET, "Key1",
			 &size, &val_out, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(val_out, 1);

	print_message("Conditional Remove non existing Key (should fail)\n");
	rc = daos_kv_remove(oh, DAOS_TX_NONE, DAOS_COND_KEY_REMOVE, "Key2",
			    NULL);
	assert_int_equal(rc, -DER_NONEXIST);

	print_message("Conditional Remove existing Key\n");
	rc = daos_kv_remove(oh, DAOS_TX_NONE, DAOS_COND_KEY_REMOVE, "Key1",
			    NULL);
	assert_int_equal(rc, 0);

	print_message("Destroying KV\n");
	rc = daos_kv_destroy(oh, DAOS_TX_NONE, NULL);
	assert_int_equal(rc, 0);

	rc = daos_kv_close(oh, NULL);
	assert_int_equal(rc, 0);

	print_message("all good\n");
} /* End simple_put_get */

static const struct CMUnitTest kv_tests[] = {
	{"KV: Object Put/GET (blocking)",
	 simple_put_get, async_disable, NULL},
	{"KV: Object Put/GET (non-blocking)",
	 simple_put_get, async_enable, NULL},
	{"KV: Object Conditional Ops (blocking)",
	 kv_cond_ops, async_disable, NULL},
};

int
kv_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  0, NULL);
}

int
run_daos_kv_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DAOS KV API tests", kv_tests,
					 kv_setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
