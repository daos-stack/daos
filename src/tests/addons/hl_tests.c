/**
 * (C) Copyright 2016 Intel Corporation.
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

#include <daos_test.h>
#include <daos_addons.h>
#include "daos_addons_test.h"

#define DTS_OCLASS_DEF		DAOS_OC_REPL_MAX_RW

static void simple_put_get(void **state);

#define NUM_KEYS 1000

static void
simple_put_get(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_epoch_t	epoch = 4;
	daos_size_t	buf_size = 1024, size;
	daos_event_t	ev;
	const char      *key_fmt = "key%d";
	char		*buf;
	char		*buf_out;
	int		i;
	int		rc;

	buf = malloc(buf_size);
	assert_non_null(buf);
	dts_buf_render(buf, buf_size);

	buf_out = malloc(buf_size);
	assert_non_null(buf_out);

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** open the object */
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	print_message("Inserting %d Keys\n", NUM_KEYS);
	/** Insert Keys */
	for (i = 0; i < NUM_KEYS; i++) {
		char key[10];

		sprintf(key, key_fmt, i);
		rc = daos_obj_put(oh, epoch, key, buf_size, buf,
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
		char key[10];
		int value = NUM_KEYS;

		sprintf(key, key_fmt, NUM_KEYS-1);
		rc = daos_obj_put(oh, epoch, key, sizeof(int), &value,
				  arg->async ? &ev : NULL);

		if (arg->async) {
			bool ev_flag;

			rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
			assert_int_equal(rc, 0);
			assert_int_equal(ev_flag, true);
			assert_int_equal(ev.ev_error, 0);
		}
	}

	print_message("Reading and Checking Keys\n");
	/** read and verify the keys */
	for (i = 0; i < NUM_KEYS; i++) {
		char key[10];

		memset(buf_out, 0, buf_size);

		sprintf(key, key_fmt, i);

		size = DAOS_REC_ANY;
		rc = daos_obj_get(oh, epoch, key, &size, NULL,
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

		rc = daos_obj_get(oh, epoch, key, &size, buf_out,
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

	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	free(buf_out);
	free(buf);

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
	daos_epoch_t	epoch = 6;
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

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** open the object */
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	recx.rx_idx	= 0;
	recx.rx_nr	= buf_size;

	for (i = 0; i < NUM_KEYS; i++) {
		io_array[i].iods = malloc(sizeof(daos_iod_t));
		io_array[i].sgls = malloc(sizeof(daos_sg_list_t));
		io_array[i].dkey = malloc(sizeof(daos_key_t));
		io_array[i].nr = 1;

		buf[i] = malloc(buf_size);
		assert_non_null(buf[i]);
		dts_buf_render(buf[i], buf_size);

		buf_out[i] = malloc(buf_size);
		assert_non_null(buf_out[i]);

		/** init dkey */
		asprintf(&keys[i], key_fmt, i);
		daos_iov_set(io_array[i].dkey, keys[i], strlen(keys[i]));
		/** init scatter/gather */
		daos_iov_set(&sg_iov[i], buf[i], buf_size);
		io_array[i].sgls[0].sg_nr.num		= 1;
		io_array[i].sgls[0].sg_nr.num_out	= 0;
		io_array[i].sgls[0].sg_iovs		= &sg_iov[i];
		/** init I/O descriptor */
		daos_iov_set(&io_array[i].iods[0].iod_name, "akey",
			     strlen("akey"));
		daos_csum_set(&io_array[i].iods[0].iod_kcsum, NULL, 0);
		io_array[i].iods[0].iod_nr	= 1;
		io_array[i].iods[0].iod_size	= 1;
		io_array[i].iods[0].iod_recxs	= &recx;
		io_array[i].iods[0].iod_eprs	= NULL;
		io_array[i].iods[0].iod_csums	= NULL;
		io_array[i].iods[0].iod_type	= DAOS_IOD_ARRAY;
	}

	rc = daos_obj_update_multi(oh, epoch, NUM_KEYS, io_array,
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
		io_array[i].sgls[0].sg_nr.num		= 1;
		io_array[i].sgls[0].sg_nr.num_out	= 0;
		io_array[i].sgls[0].sg_iovs		= &sg_iov[i];
	}

	rc = daos_obj_fetch_multi(oh, epoch, NUM_KEYS, io_array,
				  arg->async ? &ev : NULL);
	if (arg->async) {
		bool ev_flag;

		rc = daos_event_test(&ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(ev.ev_error, 0);
	}

	for (i = 0; i < NUM_KEYS; i++) {
		assert_int_equal(io_array[i].iods[0].iod_size, 1);
		assert_memory_equal(buf_out[i], buf[i], buf_size);
	}

	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	for (i = 0; i < NUM_KEYS; i++) {
		free(io_array[i].iods);
		free(io_array[i].dkey);
		free(io_array[i].sgls);
		free(buf_out[i]);
		free(buf[i]);
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
	return setup(state, SETUP_CONT_CONNECT, true);
}

int
run_hl_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("High Level API tests", hl_tests,
					 hl_setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
