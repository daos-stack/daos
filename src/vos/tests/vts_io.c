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
 * This file is part of vos/tests/
 *
 * vos/tests/vts_io.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <time.h>
#include <vts_common.h>

#include <daos/common.h>
#include <daos_srv/vos.h>
#include <vos_obj.h>

#define UPDATE_DKEY_SIZE	32
#define UPDATE_DKEY "test_update_dkey"
#define UPDATE_AKEY_SIZE	32
#define UPDATE_AKEY "test_update akey"
#define	UPDATE_BUF_SIZE		64
#define UPDATE_CSUM_SIZE	32
#define VTS_IO_KEYS		100000

struct io_test_args {
	struct vos_test_ctx	ctx;
	daos_unit_oid_t		oid;
	bool			anchor_flag;
	bool			zero_copy;
};

int		kc;
unsigned int	g_epoch;
/* To verify during enumeration */
unsigned int	total_keys;
daos_epoch_t	max_epoch;
/**
 * Stores the last key and can be used for
 * punching
 */
char		punch_key[UPDATE_DKEY_SIZE];

void
gen_rand_key(char *rkey, char *key, int ksize)
{
	int n;

	memset(rkey, 0, ksize);
	n = snprintf(rkey, ksize, key);
	snprintf(rkey+n, ksize-n, ".%d", kc++);
}

static int
setup(void **state)
{
	struct io_test_args	*arg;
	int			rc = 0;

	arg = malloc(sizeof(struct io_test_args));
	assert_ptr_not_equal(arg, NULL);

	kc	= 0;
	g_epoch = 0;
	total_keys = 0;
	srand(10);

	rc = vts_ctx_init(&arg->ctx, VPOOL_1G);
	assert_int_equal(rc, 0);

	vts_io_set_oid(&arg->oid);
	*state = arg;

	return 0;
}

static int
teardown(void **state)
{
	struct io_test_args	*arg = *state;

	vts_ctx_fini(&arg->ctx);
	free(arg);

	return 0;
}

static int
io_obj_iter_test(struct io_test_args *arg)
{
	int			rc;
	int			dkey_nr = 0;
	vos_iter_param_t	param;
	daos_handle_t		ih;

	memset(&param, 0, sizeof(param));
	param.ip_hdl	= arg->ctx.tc_co_hdl;
	param.ip_oid	= arg->oid;
	param.ip_epr.epr_lo = max_epoch + 10;

	rc = vos_iter_prepare(VOS_ITER_DKEY, &param, &ih);
	if (rc != 0) {
		print_error("Failed to prepare d-key iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		print_error("Failed to set iterator cursor: %d\n",
			    rc);
		goto out;
	}

	dkey_nr = 0;
	while (1) {
		vos_iter_entry_t  dkey_ent;
		vos_iter_entry_t  recx_ent;
		daos_handle_t	  recx_ih;
		daos_hash_out_t	  anchor;
		int		  recx_nr = 0;

		rc = vos_iter_fetch(ih, &dkey_ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing d-key iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch dkey: %d\n", rc);
			goto out;
		}

		param.ip_dkey = dkey_ent.ie_dkey;
		rc = vos_iter_prepare(VOS_ITER_RECX, &param, &recx_ih);
		if (rc != 0) {
			print_error("Failed to create recx iterator: %d\n",
				    rc);
			goto out;
		}

		rc = vos_iter_probe(recx_ih, NULL);
		if (rc != 0 && rc != -DER_NONEXIST) {
			print_error("Failed to set iterator cursor: %d\n",
				    rc);
			goto out;
		}

		while (rc == 0) {
			rc = vos_iter_fetch(recx_ih, &recx_ent, NULL);
			if (rc != 0) {
				print_error("Failed to fetch recx: %d\n",
					    rc);
				goto out;
			}

			recx_nr++;
			if (recx_nr == 1 && VTS_IO_KEYS <= 10) {
				/* output dkey only if it has matched recx */
				D_DEBUG(DF_VOS3, "dkey[%d]: %s\n", dkey_nr,
					(char *)dkey_ent.ie_dkey.iov_buf);
			}

			dkey_nr++;
			if (VTS_IO_KEYS <= 10)
				D_DEBUG(DF_VOS3, "\trecx %u : %s\n",
					(unsigned int)recx_ent.ie_recx.rx_idx,
					recx_ent.ie_iov.iov_len == 0 ?
					"[NULL]" :
					(char *)recx_ent.ie_iov.iov_buf);

			rc = vos_iter_next(recx_ih);
			if (rc != 0 && rc != -DER_NONEXIST) {
				print_error("Failed to move cursor: %d\n",
					    rc);
				goto out;
			}
		}
		vos_iter_finish(recx_ih);

		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;

		if (rc != 0) {
			print_error("Failed to move cursor: %d\n", rc);
			goto out;
		}

		if (!arg->anchor_flag)
			continue;

		rc = vos_iter_fetch(ih, &dkey_ent, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to fetch anchor: %d\n",
				    rc);
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to probe anchor: %d\n",
				    rc);
			goto out;
		}
	}
 out:
	/**
	 * Check if enumerated keys is equal to the number of
	 * keys updated
	 */
	print_message("Enumerated: %d, total_keys: %d\n",
		      dkey_nr, total_keys);
	assert_int_equal(dkey_nr, total_keys);
	vos_iter_finish(ih);
	return rc;
}

static int
io_test_obj_update(struct io_test_args *arg, int epoch,
		   daos_dkey_t *dkey, daos_vec_iod_t *vio,
		   daos_sg_list_t *sgl)
{
	daos_sg_list_t	*vec_sgl;
	daos_iov_t	*vec_iov;
	daos_iov_t	*srv_iov;
	daos_handle_t	 ioh;
	int		 rc;

	if (!arg->zero_copy) {
		rc = vos_obj_update(arg->ctx.tc_co_hdl,
				    arg->oid, epoch, dkey, 1, vio,
				    sgl, NULL);
		if (rc != 0)
			print_error("Failed to update: %d\n", rc);
		return rc;
	}

	rc = vos_obj_zc_update_begin(arg->ctx.tc_co_hdl,
				     arg->oid, epoch, dkey, 1, vio,
				     &ioh, NULL);
	if (rc != 0) {
		print_error("Failed to prepare ZC update: %d\n", rc);
		return -1;
	}

	srv_iov = &sgl->sg_iovs[0];

	vos_obj_zc_vec2sgl(ioh, 0, &vec_sgl);
	assert_int_equal(vec_sgl->sg_nr.num, 1);
	vec_iov = &vec_sgl->sg_iovs[0];

	assert_true(srv_iov->iov_len == vec_iov->iov_len);
	memcpy(vec_iov->iov_buf, srv_iov->iov_buf, srv_iov->iov_len);

	rc = vos_obj_zc_update_end(ioh, dkey, 1, vio, 0, NULL);
	if (rc != 0)
		print_error("Failed to submit ZC update: %d\n", rc);

	return rc;
}

static int
io_test_obj_fetch(struct io_test_args *arg, int epoch,
		  daos_dkey_t *dkey, daos_vec_iod_t *vio, daos_sg_list_t *sgl)
{
	daos_sg_list_t	*vec_sgl;
	daos_iov_t	*vec_iov;
	daos_iov_t	*dst_iov;
	daos_handle_t	 ioh;
	int		 rc;

	if (!arg->zero_copy) {
		rc = vos_obj_fetch(arg->ctx.tc_co_hdl,
				   arg->oid, epoch, dkey, 1, vio,
				   sgl, NULL);
		if (rc != 0)
			print_error("Failed to fetch: %d\n", rc);

		return rc;
	}

	rc = vos_obj_zc_fetch_begin(arg->ctx.tc_co_hdl,
				    arg->oid, epoch, dkey, 1, vio,
				    &ioh, NULL);
	if (rc != 0) {
		print_error("Failed to prepare ZC update: %d\n", rc);
		return -1;
	}

	dst_iov = &sgl->sg_iovs[0];

	vos_obj_zc_vec2sgl(ioh, 0, &vec_sgl);
	assert_true(vec_sgl->sg_nr.num == 1);
	vec_iov = &vec_sgl->sg_iovs[0];

	assert_true(dst_iov->iov_buf_len >= vec_iov->iov_len);
	memcpy(dst_iov->iov_buf, vec_iov->iov_buf, vec_iov->iov_len);
	dst_iov->iov_len = vec_iov->iov_len;

	rc = vos_obj_zc_fetch_end(ioh, dkey, 1, vio, 0, NULL);
	if (rc != 0)
		print_error("Failed to submit ZC update: %d\n", rc);

	return rc;
}

static int
io_update_and_fetch_dkey(struct io_test_args *arg, daos_epoch_t update_epoch,
			 daos_epoch_t fetch_epoch, bool punch)
{

	int			rc = 0;
	daos_iov_t		val_iov;
	daos_akey_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_vec_iod_t		vio;
	daos_sg_list_t		sgl;
	daos_csum_buf_t		csum;
	daos_dkey_t		dkey;

	memset(&vio, 0, sizeof(vio));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	memset(dkey_buf, 0, UPDATE_DKEY_SIZE);
	memset(update_buf, 0, UPDATE_BUF_SIZE);

	if (!punch)
		daos_iov_set(&dkey, &dkey_buf[0], UPDATE_DKEY_SIZE);
	else
		daos_iov_set(&dkey, &punch_key[0], UPDATE_DKEY_SIZE);

	gen_rand_key(&dkey_buf[0], UPDATE_DKEY, UPDATE_DKEY_SIZE);
	dkey.iov_len = strlen(dkey_buf);
	memset(punch_key, 0, UPDATE_DKEY_SIZE);

	sgl.sg_nr.num = 1;
	sgl.sg_iovs = &val_iov;

	if (!punch) {
		memcpy(punch_key, dkey_buf, UPDATE_DKEY_SIZE);
		memset(update_buf, (rand() % 94) + 33, UPDATE_BUF_SIZE);
		daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
		rex.rx_rsize	= val_iov.iov_len;
	} else {
		daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
		rex.rx_rsize	= 0;
	}

	rex.rx_nr	= 1;
	rex.rx_idx	= daos_hash_string_u32(dkey_buf, dkey.iov_len);
	rex.rx_idx	%= 1000000;

	vio.vd_recxs	= &rex;
	vio.vd_nr	= 1;

	rc = io_test_obj_update(arg, update_epoch, &dkey, &vio, &sgl);
	if (rc)
		goto exit;
	total_keys++;


	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	rex.rx_rsize = 0;
	rc = io_test_obj_fetch(arg, fetch_epoch, &dkey, &vio, &sgl);
	if (rc)
		goto exit;
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

exit:
	return rc;
}

static inline int
hold_object_refs(struct vos_obj_ref **refs,
		 struct vos_obj_cache *occ,
		 daos_handle_t *coh,
		 daos_unit_oid_t *oid,
		 int start, int end)
{
	int i = 0, rc = 0;

	for (i = start; i < end; i++) {
		rc = vos_obj_ref_hold(occ, *coh, *oid, &refs[i]);
		assert_int_equal(rc, 0);
	}

	return rc;
}

static void
io_oi_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj		*obj[2];
	daos_unit_oid_t		oid;
	int			rc = 0;

	vts_io_set_oid(&oid);
	rc = vos_oi_lookup(arg->ctx.tc_co_hdl, oid, &obj[0]);
	assert_int_equal(rc, 0);

	rc = vos_oi_lookup(arg->ctx.tc_co_hdl, oid, &obj[1]);
	assert_int_equal(rc, 0);
}

static void
io_obj_cache_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj_cache	*occ = NULL;
	daos_unit_oid_t		oid[2];
	struct vos_obj_ref	*refs[20];
	struct vos_test_ctx	*ctx = &arg->ctx;
	int			rc, i;

	rc = vos_obj_cache_create(10, &occ);
	assert_int_equal(rc, 0);

	rc = hold_object_refs(refs, occ, &ctx->tc_co_hdl, &oid[0], 0, 10);
	assert_int_equal(rc, 0);

	rc = hold_object_refs(refs, occ, &ctx->tc_co_hdl, &oid[1], 10, 15);
	assert_int_equal(rc, 0);

	for (i = 0; i < 5; i++)
		vos_obj_ref_release(occ, refs[i]);
	for (i = 10; i < 15; i++)
		vos_obj_ref_release(occ, refs[i]);

	rc = hold_object_refs(refs, occ, &ctx->tc_co_hdl, &oid[1], 15, 20);
	assert_int_equal(rc, 0);

	for (i = 5; i < 10; i++)
		vos_obj_ref_release(occ, refs[i]);
	for (i = 15; i < 20; i++)
		vos_obj_ref_release(occ, refs[i]);

	vos_obj_cache_destroy(occ);
}


static void
io_multiple_dkey(void **state)
{
	struct io_test_args	*arg = *state;
	int			i, rc = 0;
	daos_epoch_t		epoch = rand();

	arg->zero_copy = false;
	arg->anchor_flag = false;
	max_epoch = MAX(max_epoch, epoch);

	for (i = 0; i < VTS_IO_KEYS; i++) {
		rc = io_update_and_fetch_dkey(arg, epoch, epoch, false);
		assert_int_equal(rc, 0);
	}
}

static void
io_multiple_dkey_zc(void **state)
{
	struct io_test_args	*arg = *state;
	int			i, rc = 0;
	daos_epoch_t		epoch = rand();

	arg->zero_copy = true;
	arg->anchor_flag = false;
	max_epoch = MAX(max_epoch, epoch);

	for (i = 0; i < VTS_IO_KEYS; i++) {
		rc = io_update_and_fetch_dkey(arg, epoch, epoch, false);
		assert_int_equal(rc, 0);
	}
}

static void
io_iter_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;

	arg->anchor_flag = false;
	arg->zero_copy = false;

	rc = io_obj_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}

static void
io_iter_test_with_anchor(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;

	arg->anchor_flag = true;
	arg->zero_copy = false;

	rc = io_obj_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}


static void
io_simple_one_key_zc(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->anchor_flag = false;
	arg->zero_copy = true;

	rc = io_update_and_fetch_dkey(arg, 1, 1, false);
	assert_int_equal(rc, 0);
}

static void
io_simple_one_key(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->anchor_flag = false;
	arg->zero_copy = false;

	rc = io_update_and_fetch_dkey(arg, 1, 1, false);
	assert_int_equal(rc, 0);
}

static void
io_simple_punch(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->anchor_flag = false;
	arg->zero_copy = false;
	/*
	 * Punch the last updated key at a future
	 * epoch
	 */
	rc = io_update_and_fetch_dkey(arg, 10, 10, true);
	assert_int_equal(rc, 0);
}



static void
io_simple_near_epoch(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;
	daos_epoch_t		epoch = rand();

	arg->anchor_flag = false;
	arg->zero_copy = false;
	max_epoch = MAX(epoch, max_epoch);

	rc = io_update_and_fetch_dkey(arg, epoch, epoch+1000, false);
	assert_int_equal(rc, 0);
}

static void
io_simple_near_epoch_zc(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;
	daos_epoch_t		epoch = rand();

	arg->anchor_flag = false;
	arg->zero_copy = true;
	max_epoch = MAX(epoch, max_epoch);

	rc = io_update_and_fetch_dkey(arg, epoch, epoch+1000, false);
	assert_int_equal(rc, 0);
}

static void
io_pool_overflow_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc, i = 0;
	daos_epoch_t		epoch = rand();

	arg->zero_copy = false;
	arg->anchor_flag = false;
	max_epoch = MAX(max_epoch, epoch);
	vts_ctx_fini(&arg->ctx);

	rc = vts_ctx_init(&arg->ctx, VPOOL_16M);
	assert_int_equal(rc, 0);

	vts_io_set_oid(&arg->oid);

	for (i = 0; i < VTS_IO_KEYS; i++) {
		rc = io_update_and_fetch_dkey(arg, epoch, epoch, false);
		if (rc) {
			assert_int_equal(rc, -DER_NOSPACE);
			break;
		}
	}
}


static int
io_pool_overflow_teardown(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	if (vts_file_exists(arg->ctx.tc_po_name)) {
		rc = remove(arg->ctx.tc_po_name);
		assert_int_equal(rc, 0);
	}

	free(arg);

	arg = malloc(sizeof(struct io_test_args));
	assert_ptr_not_equal(arg, NULL);


	rc = vts_ctx_init(&arg->ctx, VPOOL_1G);
	assert_int_equal(rc, 0);
	vts_io_set_oid(&arg->oid);

	*state = arg;

	return rc;
}


static const struct CMUnitTest io_tests[] = {
	{ "VOS201: VOS object IO index",
		io_oi_test, NULL, NULL},
	{ "VOS202: VOS object cache test",
		io_obj_cache_test, NULL, NULL},
	{ "VOS203: Simple update/fetch/verify test",
		io_simple_one_key, NULL, NULL},
	{ "VOS204: Simple Punch test",
		io_simple_punch, NULL, NULL},
	{ "VOS205: Simple update/fetch/verify test (for dkey) with zero-copy",
		io_simple_one_key_zc, NULL, NULL},
	{ "VOS206: Simple near-epoch retrieval test",
		io_simple_near_epoch, NULL, NULL},
	{ "VOS207: Simple near-epoch retrieval test with zero-copy",
		io_simple_near_epoch_zc, NULL, NULL},
	{ "VOS208: 100K update/fetch/verify test (for dkey)",
		io_multiple_dkey, NULL, NULL},
	{ "VOS209: 100k update/fetch/verify test (for dkey) with zero-copy",
		io_multiple_dkey_zc, NULL, NULL},
	{ "VOS210: KV Iter tests (for dkey)",
		io_iter_test, NULL, NULL},
	{ "VOS211: KV Iter tests with anchor (for dkey)",
		io_iter_test_with_anchor, NULL, NULL},
	{ "VOS212: Space overflow negative error test",
		io_pool_overflow_test, NULL, io_pool_overflow_teardown},

};

int
run_io_test(void)
{
	return cmocka_run_group_tests_name("VOS IO tests", io_tests,
					   setup, teardown);
}
