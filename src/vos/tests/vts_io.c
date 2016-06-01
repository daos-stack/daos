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
	char			*fname;
	uuid_t			pool_uuid;
	uuid_t			co_uuid;
	daos_handle_t		coh;
	daos_handle_t		poh;
	daos_unit_oid_t		oid;
	bool			anchor_flag;
};

int		kc;
unsigned int	g_epoch;

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
	srand(10);

	rc = alloc_gen_fname(&arg->fname);
	assert_int_equal(rc, 0);
	if (file_exists(arg->fname)) {
		rc = remove(arg->fname);
		assert_int_equal(rc, 0);
	}
	uuid_generate_time_safe(arg->pool_uuid);
	uuid_generate_time_safe(arg->co_uuid);
	rc = vos_pool_create(arg->fname, arg->pool_uuid,
			     1073741824, &arg->poh, NULL);
	assert_int_equal(rc, 0);

	rc = vos_co_create(arg->poh, arg->co_uuid, NULL);
	assert_int_equal(rc, 0);

	rc = vos_co_open(arg->poh, arg->co_uuid, &arg->coh,
			 NULL);
	assert_int_equal(rc, 0);
	io_set_oid(&arg->oid);

	*state = arg;
	return 0;
}

static int
teardown(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;

	rc = vos_co_close(arg->coh, NULL);
	assert_int_equal(rc, 0);

	rc = vos_pool_destroy(arg->poh, NULL);
	assert_int_equal(rc, 0);

	if (arg->fname) {
		remove(arg->fname);
		free(arg->fname);
	}

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
	param.ip_hdl	= arg->coh;
	param.ip_oid	= arg->oid;
	param.ip_epr.epr_lo = 11;

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
				dkey_nr++;
			}
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
	print_message("Enumerated %d dkeys\n", dkey_nr);
	vos_iter_finish(ih);
	return 0;
}

static int
io_update_and_fetch_dkey(struct io_test_args *arg)
{

	int			rc = 0;
	daos_iov_t		val_iov;
	daos_akey_t		akey;
	daos_recx_t		rex;
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	char			csum_buf[UPDATE_CSUM_SIZE];
	daos_vec_iod_t		vio;
	daos_sg_list_t		sgl;
	daos_epoch_t		epoch;
	daos_csum_buf_t		csum;
	daos_dkey_t		dkey;

	memset(&vio, 0, sizeof(vio));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	memset(dkey_buf, 0, UPDATE_DKEY_SIZE);
	memset(update_buf, 0, UPDATE_BUF_SIZE);
	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	memset(akey_buf, 0, UPDATE_AKEY_SIZE);

	daos_iov_set(&dkey, &dkey_buf[0], UPDATE_DKEY_SIZE);
	gen_rand_key(&dkey_buf[0], UPDATE_DKEY, UPDATE_DKEY_SIZE);
	dkey.iov_len = strlen(dkey_buf);
	daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
	daos_csum_set(&csum, &csum_buf[0], UPDATE_CSUM_SIZE);

	sgl.sg_nr.num = 1;
	sgl.sg_iovs = &val_iov;
	epoch	= g_epoch++;

	daos_iov_set(&akey, &akey_buf[0], UPDATE_AKEY_SIZE);
	gen_rand_key(&akey_buf[0], UPDATE_AKEY, UPDATE_AKEY_SIZE);
	akey.iov_len = strlen(akey_buf);
	memset(update_buf, (rand() % 94) + 33, UPDATE_BUF_SIZE);
	rex.rx_nr	= 1;
	rex.rx_rsize	= UPDATE_BUF_SIZE;
	rex.rx_idx	= daos_hash_string_u32(dkey_buf, dkey.iov_len);
	rex.rx_idx	%= 1000000;

	vio.vd_name	= akey;
	vio.vd_csums	= &csum;
	vio.vd_recxs	= &rex;
	vio.vd_nr	= 1;

	rc = vos_obj_update(arg->coh, arg->oid, epoch, &dkey, 1, &vio,
			    &sgl, NULL);
	assert_int_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	rc = vos_obj_fetch(arg->coh, arg->oid, epoch, &dkey, 1, &vio,
			   &sgl, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

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

	io_set_oid(&oid);
	rc = vos_oi_lookup(arg->coh, oid, &obj[0]);
	assert_int_equal(rc, 0);

	rc = vos_oi_lookup(arg->coh, oid, &obj[1]);
	assert_int_equal(rc, 0);
}

static void
io_obj_cache_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj_cache	*occ = NULL;
	daos_unit_oid_t		oid[2];
	struct vos_obj_ref	*refs[20];
	int			rc, i;

	rc = vos_obj_cache_create(10, &occ);
	assert_int_equal(rc, 0);

	rc = hold_object_refs(refs, occ, &arg->coh, &oid[0], 0, 10);
	assert_int_equal(rc, 0);

	rc = hold_object_refs(refs, occ, &arg->coh, &oid[1], 10, 15);
	assert_int_equal(rc, 0);

	for (i = 0; i < 5; i++)
		vos_obj_ref_release(occ, refs[i]);
	for (i = 10; i < 15; i++)
		vos_obj_ref_release(occ, refs[i]);

	rc = hold_object_refs(refs, occ, &arg->coh, &oid[1], 15, 20);
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

	for (i = 0; i < VTS_IO_KEYS; i++) {
		rc = io_update_and_fetch_dkey(arg);
		assert_int_equal(rc, 0);
	}
}

static void
io_iter_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;

	arg->anchor_flag = false;
	rc = io_obj_iter_test(arg);
	assert_int_equal(rc, 0);
}

static void
io_simple_one_key(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	rc = io_update_and_fetch_dkey(arg);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest io_tests[] = {
	{ "VOS200: Simple update/fetch/verify test",
		io_simple_one_key, NULL, NULL},
	{ "VOS201: Multiple update/fetch/verify test (for dkey)",
		io_multiple_dkey, NULL, NULL},
	{ "VOS202: KV Iter tests (for dkey)",
		io_iter_test, NULL, NULL},
	{ "VOS203: VOS object IO index",
		io_oi_test, NULL, NULL},
	{ "VOS204: VOS object cache test",
		io_obj_cache_test, NULL, NULL},
};


int
run_io_test(void)
{
	return cmocka_run_group_tests_name("VOS IO tests", io_tests,
					   setup, teardown);
}
