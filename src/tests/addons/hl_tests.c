/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * This file is part of daos_m
 *
 * src/tests/addons/
 */
#if !defined(__has_warning)  /* gcc */
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#else
	#if __has_warning("-Wframe-larger-than=") /* valid clang warning */
		#pragma GCC diagnostic ignored "-Wframe-larger-than="
	#endif
#endif
#include <daos_types.h>
#include <daos_addons.h>
#include "daos_test.h"
#include "daos_addons_test.h"

#define DTS_OCLASS_DEF		DAOS_OC_REPL_MAX_RW

static void simple_put_get(void **state);

#define NUM_KEYS 1000
#define ENUM_KEY_NR     1000
#define ENUM_DESC_NR    10
#define ENUM_DESC_BUF   (ENUM_DESC_NR * ENUM_KEY_NR)

static void
list_keys(daos_handle_t oh, int *num_keys)
{
	char		*buf;
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_anchor_t	 anchor = {0};
	int		 key_nr = 0;
	daos_sg_list_t	 sgl;
	daos_iov_t       sg_iov;

	buf = malloc(ENUM_DESC_BUF);
	daos_iov_set(&sg_iov, buf, ENUM_DESC_BUF);
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

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, 0, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** open the object */
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	print_message("Inserting %d Keys\n", NUM_KEYS);
	/** Insert Keys */
	for (i = 0; i < NUM_KEYS; i++) {
		sprintf(key, key_fmt, i);
		rc = daos_kv_put(oh, DAOS_TX_NONE, key, buf_size, buf,
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
		rc = daos_kv_put(oh, DAOS_TX_NONE, key, sizeof(int), &value,
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
		memset(buf_out, 0, buf_size);

		sprintf(key, key_fmt, i);

		size = DAOS_REC_ANY;
		rc = daos_kv_get(oh, DAOS_TX_NONE, key, &size, NULL,
				 arg->async ? &ev : NULL);
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

		rc = daos_kv_get(oh, DAOS_TX_NONE, key, &size, buf_out,
				 arg->async ? &ev : NULL);
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
		rc = daos_kv_remove(oh, DAOS_TX_NONE, key, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("Enumerating Keys\n");
	list_keys(oh, &num_keys);
	assert_int_equal(num_keys, NUM_KEYS - 10);

	rc = daos_obj_close(oh, NULL);
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
simple_multi_io(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_size_t	buf_size = 128;
	daos_event_t	ev;
	daos_iov_t	sg_iov[NUM_KEYS];
	daos_recx_t	recx;
	const char      *key_fmt = "key%d";
	char		*buf[NUM_KEYS];
	char		*buf_out[NUM_KEYS];
	char		*keys[NUM_KEYS];
	daos_dkey_io_t	io_array[NUM_KEYS];
	int		i;
	int		rc;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, 0, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** open the object */
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	recx.rx_idx	= 0;
	recx.rx_nr	= buf_size;

	for (i = 0; i < NUM_KEYS; i++) {
		D_ALLOC(io_array[i].ioa_iods, sizeof(daos_iod_t));
		D_ALLOC(io_array[i].ioa_sgls, sizeof(daos_sg_list_t));
		D_ALLOC(io_array[i].ioa_dkey, sizeof(daos_key_t));
		io_array[i].ioa_nr = 1;

		D_ALLOC(buf[i], buf_size);
		assert_non_null(buf[i]);
		dts_buf_render(buf[i], buf_size);

		D_ALLOC(buf_out[i], buf_size);
		assert_non_null(buf_out[i]);

		/** init dkey */
		rc = asprintf(&keys[i], key_fmt, i);
		assert_non_null(keys[i]);
		assert_int_not_equal(rc, -1);

		daos_iov_set(io_array[i].ioa_dkey, keys[i], strlen(keys[i]));
		/** init scatter/gather */
		daos_iov_set(&sg_iov[i], buf[i], buf_size);
		io_array[i].ioa_sgls[0].sg_nr		= 1;
		io_array[i].ioa_sgls[0].sg_nr_out	= 0;
		io_array[i].ioa_sgls[0].sg_iovs		= &sg_iov[i];
		/** init I/O descriptor */
		daos_iov_set(&io_array[i].ioa_iods[0].iod_name, "akey",
			     strlen("akey"));
		daos_csum_set(&io_array[i].ioa_iods[0].iod_kcsum, NULL, 0);
		io_array[i].ioa_iods[0].iod_nr	= 1;
		io_array[i].ioa_iods[0].iod_size	= 1;
		io_array[i].ioa_iods[0].iod_recxs	= &recx;
		io_array[i].ioa_iods[0].iod_eprs	= NULL;
		io_array[i].ioa_iods[0].iod_csums	= NULL;
		io_array[i].ioa_iods[0].iod_type	= DAOS_IOD_ARRAY;
	}

	rc = daos_obj_update_multi(oh, DAOS_TX_NONE, NUM_KEYS, io_array,
				   arg->async ? &ev : NULL);
	if (arg->async) {
		bool ev_flag;

		rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(ev.ev_error, 0);
	}

	for (i = 0; i < NUM_KEYS; i++) {
		/** init scatter/gather */
		daos_iov_set(&sg_iov[i], buf_out[i], buf_size);
		io_array[i].ioa_sgls[0].sg_nr		= 1;
		io_array[i].ioa_sgls[0].sg_nr_out	= 0;
		io_array[i].ioa_sgls[0].sg_iovs		= &sg_iov[i];
	}

	rc = daos_obj_fetch_multi(oh, DAOS_TX_NONE, NUM_KEYS, io_array,
				  arg->async ? &ev : NULL);
	if (arg->async) {
		bool ev_flag;

		rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(ev.ev_error, 0);
	}

	for (i = 0; i < NUM_KEYS; i++) {
		assert_int_equal(io_array[i].ioa_iods[0].iod_size, 1);
		assert_memory_equal(buf_out[i], buf[i], buf_size);
	}

	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	for (i = 0; i < NUM_KEYS; i++) {
		D_FREE(io_array[i].ioa_iods);
		D_FREE(io_array[i].ioa_dkey);
		D_FREE(io_array[i].ioa_sgls);
		D_FREE(buf_out[i]);
		D_FREE(buf[i]);
	}

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	print_message("all good\n");
} /* End simple_multi_io */

static const struct CMUnitTest hl_tests[] = {
	{"HL: Object Put/GET (blocking)",
	 simple_put_get, async_disable, NULL},
	{"HL: Object Put/GET (non-blocking)",
	 simple_put_get, async_enable, NULL},
	{"HL: Multi DKEY Update/Fetch (blocking)",
	 simple_multi_io, async_disable, NULL},
	{"HL: Multi DKEY Update/Fetch (non-blocking)",
	 simple_multi_io, async_enable, NULL},
};

int
hl_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

int
run_hl_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("High Level API tests", hl_tests,
					 hl_setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
