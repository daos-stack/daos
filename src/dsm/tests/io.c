/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of dsm
 *
 * dsm/tests/io.c
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_mgmt.h>
#include <daos_m.h>
#include <daos_event.h>

#define UPDATE_DKEY_SIZE	32
#define UPDATE_DKEY "test_update dkey"
#define UPDATE_AKEY_SIZE	32
#define UPDATE_AKEY "test_update akey"
#define UPDATE_BUF_SIZE		64
#define UPDATE_CSUM_SIZE	32

typedef struct {
	daos_rank_t		ranks[8];
	daos_rank_list_t	svc;
	uuid_t			uuid;
	uuid_t			co_uuid;
	daos_handle_t		eq;
	daos_handle_t		poh;
	daos_handle_t		coh;
	bool			async;
} test_arg_t;

/** very basic update/fetch with data verification */
static void
io_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 oh;
	daos_unit_oid_t	 oid = {{ .lo = 0, .mid = 1, .hi = 2}, 3};
	daos_event_t	 ev;
	daos_event_t	*evp;
	daos_iov_t	 val_iov;
	char		 dkey_buf[UPDATE_DKEY_SIZE];
	char		 akey_buf[UPDATE_AKEY_SIZE];
	char		 update_buf[UPDATE_BUF_SIZE];
	char		 fetch_buf[UPDATE_BUF_SIZE];
	char		 csum_buf[UPDATE_CSUM_SIZE];
	daos_dkey_t	 dkey;
	daos_akey_t	 akey;
	daos_recx_t	 rex;
	daos_epoch_t	 epoch;
	daos_epoch_range_t erange;
	daos_csum_buf_t	 csum;
	daos_vec_iod_t	 vio;
	daos_sg_list_t	 sgl;
	int		 rc;

	/** choose random object */
	oid.id_pub.lo = rand();
	oid.id_pub.mid = rand();
	oid.id_pub.hi = rand();
	oid.id_shard = 0;
	oid.id_pad_32 = rand() % 16; /** must be nr target in the future */

	rc = dsm_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	memset(&vio, 0, sizeof(vio));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	memset(&dkey, 0, sizeof(dkey));
	memset(&akey, 0, sizeof(akey));

	daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
	sgl.sg_nr.num = 1;
	sgl.sg_iovs = &val_iov;

	daos_csum_set(&csum, &csum_buf[0], UPDATE_CSUM_SIZE);

	daos_iov_set(&dkey, &dkey_buf[0], UPDATE_DKEY_SIZE);
	dkey.iov_len = strlen(UPDATE_DKEY);
	strncpy(dkey_buf, UPDATE_DKEY, strlen(UPDATE_DKEY));

	daos_iov_set(&akey, &akey_buf[0], UPDATE_AKEY_SIZE);
	akey.iov_len = strlen(UPDATE_AKEY);
	strncpy(akey_buf, UPDATE_AKEY, strlen(UPDATE_AKEY));

	memset(update_buf, (rand() % 94) + 33, UPDATE_BUF_SIZE);

	rex.rx_nr	= 1;
	rex.rx_rsize	= UPDATE_BUF_SIZE;
	rex.rx_idx	= 0;

	erange.epr_lo = 0;
	erange.epr_hi = DAOS_EPOCH_MAX;

	vio.vd_name	= akey;
	vio.vd_recxs	= &rex;
	vio.vd_csums	= &csum;
	vio.vd_nr	= 1;
	/** required for now since the wire format needs it */
	vio.vd_eprs	= &erange;

	epoch = rand();

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("Update %s: %c\n", dkey_buf, update_buf[0]);

	rc = dsm_obj_update(oh, epoch, &dkey, 1, &vio, &sgl,
			    arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for update completion */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}

	print_message("Fetch %s\n", dkey_buf);
	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);
	rc = dsm_obj_fetch(oh, epoch, &dkey, 1, &vio, &sgl, NULL,
			   arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for fetch completion */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** sanity check the data */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	rc = dsm_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
}

static int
async_enable(void **state)
{
	test_arg_t	*arg = *state;

	arg->async = true;
	return 0;
}

static int
async_disable(void **state)
{
	test_arg_t	*arg = *state;

	arg->async = false;
	return 0;
}

static const struct CMUnitTest io_tests[] = {
	{ "DSM200: simple update/fetch/verify",
	  io_simple, async_disable, NULL},
	{ "DSM201: simple update/fetch/verify (async)",
	  io_simple, async_enable, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	arg->svc.rl_nr.num = 8;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;

	/** create pool with minimal size */
	rc = dmg_pool_create(0, geteuid(), getegid(), "srv_grp", NULL, "pmem",
			     0, &arg->svc, arg->uuid, NULL);
	if (rc)
		return rc;

	/** connect to pool */
	rc = dsm_pool_connect(arg->uuid, NULL /* grp */, &arg->svc,
			      DAOS_PC_RW, NULL /* failed */, &arg->poh,
			      NULL /* ev */);
	if (rc)
		return rc;

	/** create container */
	uuid_generate(arg->co_uuid);
	rc = dsm_co_create(arg->poh, arg->co_uuid, NULL);
	if (rc)
		return rc;

	/** open container */
	rc = dsm_co_open(arg->poh, arg->co_uuid, DAOS_COO_RW, NULL, &arg->coh,
			 NULL, NULL);
	if (rc)
		return rc;

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	rc = dsm_co_close(arg->coh, NULL);
	if (rc)
		return rc;

	rc = dsm_co_destroy(arg->poh, arg->co_uuid, 1, NULL);
	if (rc)
		return rc;

	rc = dsm_pool_disconnect(arg->poh, NULL /* ev */);
	if (rc)
		return rc;

	rc = dmg_pool_destroy(arg->uuid, "srv_grp", 1, NULL);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_io_test(void)
{
	return cmocka_run_group_tests_name("DSM io tests", io_tests,
					   setup, teardown);
}
