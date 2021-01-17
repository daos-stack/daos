/**
 * (C) Copyright 2019-2021 Intel Corporation.
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
 * This file is part of vos/tests/
 *
 * vos/tests/vts_pm.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdarg.h>
#include "vts_io.h"
#include "vts_array.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(...)		\
	do {				\
		printf(__VA_ARGS__);	\
		fflush(stdout);		\
	} while (0)
#else
#define VERBOSE_PRINT(...) (void)0
#endif


static int start_epoch = 5;
#define BUF_SIZE 2000
static int buf_size = BUF_SIZE;
struct pm_info {
	daos_unit_oid_t	pi_oid;
	daos_handle_t	pi_aoh;
	daos_epoch_t	pi_epoch;
	char		pi_fetch_buf[BUF_SIZE];
	char		pi_update_buf[BUF_SIZE];
	char		pi_fill_buf[BUF_SIZE];
};


static int
pm_setup(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info;
	int			 rc;

	arg->custom = NULL;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return -DER_NOMEM;

	info->pi_epoch = 1;

	rc = vts_array_alloc(arg->ctx.tc_co_hdl, info->pi_epoch,
			     sizeof(int32_t), 0, 0, &info->pi_oid);
	if (rc != 0)
		goto fail;

	memset(info->pi_fill_buf, 0xa, sizeof(info->pi_fill_buf));

	rc = vts_array_open(arg->ctx.tc_co_hdl, info->pi_oid, &info->pi_aoh);
	if (rc == 0) {
		arg->custom = info;
		return 0;
	}

	vts_array_free(arg->ctx.tc_co_hdl, info->pi_oid);
fail:
	D_FREE(info);
	return rc;
}

static int
pm_teardown(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;

	vts_array_close(info->pi_aoh);
	vts_array_free(arg->ctx.tc_co_hdl, info->pi_oid);

	D_FREE(info);

	return 0;
}

struct counts {
	int num_objs;
	int num_punched_objs;
	int num_dkeys;
	int num_punched_dkeys;
	int num_akeys;
	int num_punched_akeys;
	int num_recx;
	int num_punched_recx;
};

static int
count_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	 vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	struct counts	*counts = cb_arg;

	switch (type) {
	default:
		break;
	case VOS_ITER_DKEY:
		counts->num_dkeys++;
		if (entry->ie_punch)
			counts->num_punched_dkeys++;
		break;
	case VOS_ITER_AKEY:
		counts->num_akeys++;
		if (entry->ie_punch)
			counts->num_punched_akeys++;
		break;
	case VOS_ITER_RECX:
		counts->num_recx++;
		if (bio_addr_is_hole(&entry->ie_biov.bi_addr))
			counts->num_punched_recx++;
		break;
	case VOS_ITER_OBJ:
		counts->num_objs++;
		if (entry->ie_punch)
			counts->num_punched_objs++;
		break;
	}

	return 0;
}

static void
vos_check(void **state, vos_iter_param_t *param, vos_iter_type_t type,
	  const struct counts *expected)
{
	struct counts		 counts = {0};
	struct vos_iter_anchors	 anchors = {0};
	int			 rc;

	rc = vos_iterate(param, type, true, &anchors, count_cb, NULL, &counts,
			 NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(expected->num_objs, counts.num_objs);
	assert_int_equal(expected->num_dkeys, counts.num_dkeys);
	assert_int_equal(expected->num_akeys, counts.num_akeys);
	assert_int_equal(expected->num_recx, counts.num_recx);
	assert_int_equal(expected->num_punched_objs, counts.num_punched_objs);
	assert_int_equal(expected->num_punched_dkeys, counts.num_punched_dkeys);
	assert_int_equal(expected->num_punched_akeys, counts.num_punched_akeys);
	assert_int_equal(expected->num_punched_recx, counts.num_punched_recx);
}

static void
vos_check_obj(void **state, daos_epoch_t epoch, int flags, int objs,
	      int punched_objs, int dkeys, int punched_dkeys, int akeys,
	      int punched_akeys, int recxs, int punched_recx)
{
	struct io_test_args	*arg = *state;
	vos_iter_param_t	 param = {0};
	struct counts		 counts = {0};

	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_flags = flags;
	param.ip_ih = DAOS_HDL_INVAL;
	param.ip_epr.epr_hi = epoch;
	param.ip_epr.epr_lo = 0;
	counts.num_objs = objs;
	counts.num_dkeys = dkeys;
	counts.num_akeys = akeys;
	counts.num_recx = recxs;
	counts.num_punched_objs = punched_objs;
	counts.num_punched_dkeys = punched_dkeys;
	counts.num_punched_akeys = punched_akeys;
	counts.num_punched_recx = punched_recx;

	vos_check(state, &param, VOS_ITER_OBJ, &counts);
}

static void
vos_check_dkey(void **state, daos_epoch_t epoch, int flags, daos_unit_oid_t oid,
	       int dkeys, int punched_dkeys, int akeys, int punched_akeys,
	       int recxs, int punched_recx)
{
	struct io_test_args	*arg = *state;
	vos_iter_param_t	 param = {0};
	struct counts		 counts = {0};

	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_flags = flags;
	param.ip_ih = DAOS_HDL_INVAL;
	param.ip_epr.epr_hi = epoch;
	param.ip_epr.epr_lo = 0;
	param.ip_oid = oid;
	counts.num_dkeys = dkeys;
	counts.num_akeys = akeys;
	counts.num_recx = recxs;
	counts.num_punched_dkeys = punched_dkeys;
	counts.num_punched_akeys = punched_akeys;
	counts.num_punched_recx = punched_recx;

	vos_check(state, &param, VOS_ITER_DKEY, &counts);
}

static void
vos_check_akey(void **state, daos_epoch_t epoch, int flags, daos_unit_oid_t oid,
	       daos_key_t *dkey, int akeys, int punched_akeys, int recxs,
	       int punched_recx)
{
	struct io_test_args	*arg = *state;
	vos_iter_param_t	 param = {0};
	struct counts		 counts = {0};

	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_flags = flags;
	param.ip_ih = DAOS_HDL_INVAL;
	param.ip_epr.epr_hi = epoch;
	param.ip_epr.epr_lo = 0;
	param.ip_oid = oid;
	param.ip_dkey = *dkey;
	counts.num_akeys = akeys;
	counts.num_recx = recxs;
	counts.num_punched_akeys = punched_akeys;
	counts.num_punched_recx = punched_recx;

	vos_check(state, &param, VOS_ITER_AKEY, &counts);
}

static void
vos_check_recx(void **state, daos_epoch_t epoch, int flags, daos_unit_oid_t oid,
	       daos_key_t *dkey, daos_key_t *akey, int recxs, int punched_recx)
{
	struct io_test_args	*arg = *state;
	vos_iter_param_t	 param = {0};
	struct counts		 counts = {0};

	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_flags = flags;
	param.ip_ih = DAOS_HDL_INVAL;
	param.ip_epr.epr_hi = epoch;
	param.ip_epr.epr_lo = 0;
	param.ip_oid = oid;
	param.ip_dkey = *dkey;
	param.ip_akey = *akey;
	counts.num_recx = recxs;
	counts.num_punched_recx = punched_recx;

	vos_check(state, &param, VOS_ITER_RECX, &counts);
}

static void
array_set_get_size(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	size_t			 size;
	int			 rc;
	int			 flags;

	rc = vts_array_set_size(info->pi_aoh, 2, 1000);
	assert_int_equal(rc, 0);

	rc = vts_array_get_size(info->pi_aoh, 3, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 1000);

	rc = vts_array_set_size(info->pi_aoh, 4, 5);
	assert_int_equal(rc, 0);

	rc = vts_array_get_size(info->pi_aoh, 5, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 5);

	rc = vts_array_reset(&info->pi_aoh, 6, 7, 1, 0, 0);
	assert_int_equal(rc, 0);

	rc = vts_array_get_size(info->pi_aoh, 8, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 0);

	flags = VOS_IT_EPC_RR | VOS_IT_RECX_VISIBLE;
	vos_check_obj(state, 9, flags, 1, 0, 1, 0, 1, 0, 0, 0);
	vos_check_obj(state, 3, flags, 1, 1, 2, 1, 2, 0, 1, 0);
	vos_check_obj(state, 5, flags, 1, 1, 2, 0, 2, 0, 2, 1);
}

static void
array_size_write(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	uint64_t		 start_size;
	uint64_t		 punch_size;
	uint64_t		 per_key = 3;
	uint64_t		 akey_size = 5;
	daos_epoch_t		 epoch = 10;
	int			 rc;
	int			 i;

	rc = vts_array_reset(&info->pi_aoh, epoch, epoch + 1, 1, 2, 1);
	epoch += 2;

	memset(info->pi_update_buf, 'x', buf_size);

	for (i = 0; i < 5; i++) {
		for (start_size = buf_size, punch_size = 0;
		     punch_size < start_size;
		     start_size -= 11, punch_size += 53) {
			rc = vts_array_write(info->pi_aoh, epoch++, 0,
					     start_size, info->pi_update_buf);
			assert_int_equal(rc, 0);

			memcpy(info->pi_fetch_buf, info->pi_fill_buf, buf_size);
			rc = vts_array_read(info->pi_aoh, epoch++, 0, buf_size,
					    info->pi_fetch_buf);
			assert_int_equal(rc, 0);
			assert_memory_equal(info->pi_fetch_buf,
					    info->pi_update_buf, start_size);
			assert_memory_equal(info->pi_fetch_buf + start_size,
					    info->pi_fill_buf,
					    buf_size - start_size);

			rc = vts_array_set_size(info->pi_aoh, epoch++,
						punch_size);
			assert_int_equal(rc, 0);

			memcpy(info->pi_fetch_buf, info->pi_fill_buf, buf_size);
			rc = vts_array_read(info->pi_aoh, epoch++, 0, buf_size,
					    info->pi_fetch_buf);
			assert_int_equal(rc, 0);
			assert_memory_equal(info->pi_fetch_buf,
					    info->pi_update_buf, punch_size);
			assert_memory_equal(info->pi_fetch_buf + punch_size,
					    info->pi_fill_buf,
					    buf_size - punch_size);
		}

		rc = vts_array_reset(&info->pi_aoh, epoch, epoch + 1, 1,
				     per_key, akey_size);
		per_key += 11;
		akey_size += 7;
		epoch += 2;
	}
}

/* User fills update buffer, after finished, fetch buffer should have identical
 * contents.
 */
static void
array_read_write_punch_size(void **state, daos_epoch_t epc_in,
			    daos_epoch_t *epc_out, bool punch, int iter,
			    size_t rec_size, size_t max_elem, int inc)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	daos_epoch_t		 epoch = epc_in;
	daos_size_t		 size;
	uint64_t		 per_key = 5;
	uint64_t		 akey_size = 1;
	uint64_t		 new_size = 7;
	int			 rc;
	int			 div = 2;
	int			 i;

	rc = vts_array_reset(&info->pi_aoh, epoch, epoch + 1, rec_size,
			     per_key, akey_size);
	assert_int_equal(rc, 0);
	epoch += 2;

	per_key += inc;
	akey_size += inc;

	for (i = 0; i < iter; i++) {
		rc = vts_array_write(info->pi_aoh, epoch++, 0, max_elem,
				     info->pi_update_buf);
		assert_int_equal(rc, 0);

		rc = vts_array_get_size(info->pi_aoh, epoch++, &size);
		assert_int_equal(rc, 0);
		assert_int_equal(size, max_elem);

		memset(info->pi_fetch_buf, 0xa, max_elem * rec_size);
		rc = vts_array_read(info->pi_aoh, epoch++, 0, max_elem,
				    info->pi_fetch_buf);
		assert_int_equal(rc, 0);
		assert_memory_equal(info->pi_update_buf, info->pi_fetch_buf,
				    max_elem * rec_size);

		rc = vts_array_set_size(info->pi_aoh, epoch++, new_size);
		assert_int_equal(rc, 0);

		memset(info->pi_fetch_buf, 0xa, max_elem * rec_size);
		rc = vts_array_read(info->pi_aoh, epoch++, 0, max_elem,
				    info->pi_fetch_buf);
		assert_int_equal(rc, 0);
		assert_memory_equal(info->pi_update_buf, info->pi_fetch_buf,
				    new_size * rec_size);
		assert_memory_equal(info->pi_fetch_buf + (rec_size * new_size),
				    info->pi_fill_buf,
				    (max_elem - new_size) * rec_size);

		rc = vts_array_get_size(info->pi_aoh, epoch++, &size);
		assert_int_equal(rc, 0);
		assert_int_equal(size, new_size);

		new_size++;
		if (!punch)
			continue;

		rc = vts_array_reset(&info->pi_aoh, epoch, epoch + 1,
				     rec_size, per_key, akey_size);
		assert_int_equal(rc, 0);
		epoch += 2;
		per_key += inc;
		akey_size += inc;

		rc = vts_array_set_iosize(info->pi_aoh, per_key / div);
		div++;

		rc = vts_array_get_size(info->pi_aoh, epoch++, &size);
		assert_int_equal(rc, 0);
		assert_int_equal(size, 0);

	}

	/* Now make sure fetch buf == update buf by writing again and reading */
	rc = vts_array_write(info->pi_aoh, epoch++, 0, max_elem,
			     info->pi_update_buf);
	assert_int_equal(rc, 0);

	memset(info->pi_fetch_buf, 0xa, max_elem * rec_size);
	rc = vts_array_read(info->pi_aoh, epoch++, 0, max_elem,
			    info->pi_fetch_buf);
	assert_int_equal(rc, 0);

	*epc_out = epoch;
}

static void
array_1(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	int32_t			*fetch_buf = (int32_t *)info->pi_fetch_buf;
	int32_t			*update_buf = (int32_t *)info->pi_update_buf;
	daos_epoch_t		 epoch = 0;
	int			 max_elem = buf_size / sizeof(int32_t);
	int			 i;

	/** Setup initial input */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = i;

	array_read_write_punch_size(state, 2, &epoch, true, 10,
				    sizeof(int32_t), max_elem, 1);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);

	/** Change up the input array */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = i + 1;

	/* Run it again without the punches */
	array_read_write_punch_size(state, epoch, &epoch, false, 10,
				    sizeof(int32_t), max_elem, 1);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);
}

static void
array_2(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	char			*fetch_buf = (char *)info->pi_fetch_buf;
	char			*update_buf = (char *)info->pi_update_buf;
	daos_epoch_t		 epoch = 0;
	int			 max_elem = buf_size / sizeof(char);
	int			 i;

	/** Setup initial input */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = i % 26 + 'a';

	array_read_write_punch_size(state, 2, &epoch, true, 10,
				    sizeof(char), max_elem, 1);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);

	/** Change up the input array */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = (i + 1) % 26 + 'a';

	/* Run it again without the punches */
	array_read_write_punch_size(state, epoch, &epoch, false, 10,
				    sizeof(char), max_elem, 1);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);
}

static void
array_3(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	int32_t			*fetch_buf = (int32_t *)info->pi_fetch_buf;
	int32_t			*update_buf = (int32_t *)info->pi_update_buf;
	daos_epoch_t		 epoch = 0;
	int			 max_elem = buf_size / sizeof(int32_t);
	int			 i;

	/** Setup initial input */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = i;

	array_read_write_punch_size(state, 2, &epoch, true, 10,
				    sizeof(int32_t), max_elem, 0);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);

	/** Change up the input array */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = i + 1;

	/* Run it again without the punches */
	array_read_write_punch_size(state, epoch, &epoch, false, 10,
				    sizeof(int32_t), max_elem, 0);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);
}

static void
array_4(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	char			*fetch_buf = (char *)info->pi_fetch_buf;
	char			*update_buf = (char *)info->pi_update_buf;
	daos_epoch_t		 epoch = 0;
	int			 max_elem = buf_size / sizeof(char);
	int			 i;

	/** Setup initial input */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = i % 26 + 'a';

	array_read_write_punch_size(state, 2, &epoch, true, 10,
				    sizeof(char), max_elem, 0);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);

	/** Change up the input array */
	for (i = 0; i < max_elem; i++)
		update_buf[i] = (i + 1) % 26 + 'a';

	/* Run it again without the punches */
	array_read_write_punch_size(state, epoch, &epoch, false, 10,
				    sizeof(char), max_elem, 0);

	assert_memory_equal(update_buf, fetch_buf,
			    sizeof(update_buf[0]) * max_elem);
}


static void
punch_model_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	const char		*expected = "HelloWorld";
	const char		*under = "HelloLonelyWorld";
	const char		*latest = "Goodbye";
	char			buf[32] = {0};
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	daos_unit_oid_t		oid;

	test_args_reset(arg, VPOOL_SIZE);

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* Set up dkey and akey */
	oid = gen_oid(arg->ofeat);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rex.rx_idx = 0;
	rex.rx_nr = strlen(under);

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_name = akey;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	/* Allocate memory for the scatter-gather list */
	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

	d_iov_set(&sgl.sg_iovs[0], (void *)under, strlen(under));

	/* Write the original value (under) */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 1, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);
	/* Punch the akey */
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, 2, 0, 0, &dkey, 1, &akey,
			   NULL);
	assert_int_equal(rc, 0);

	/* Write the new value (expected) */
	rex.rx_nr = strlen(expected);
	d_iov_set(&sgl.sg_iovs[0], (void *)expected, strlen(expected));
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 3, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/* Now read back original # of bytes */
	rex.rx_nr = strlen(under);
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, strlen(under));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 4, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);

	assert_int_equal(strncmp(buf, expected, strlen(under)), 0);

	/* Write the original value at latest epoch (under) */
	d_iov_set(&sgl.sg_iovs[0], (void *)under, strlen(under));
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 5, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);
	/* Punch the dkey */
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, 6, 0, 0, &dkey, 0, NULL,
			   NULL);
	assert_int_equal(rc, 0);

	/* Write the new value (expected) at latest epoch*/
	rex.rx_nr = strlen(expected);
	d_iov_set(&sgl.sg_iovs[0], (void *)expected, strlen(expected));
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 7, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	memset(buf, 0, sizeof(buf));
	/* Now read back original # of bytes */
	rex.rx_nr = strlen(under);
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, strlen(under));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 8, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);

	assert_int_equal(strncmp(buf, expected, strlen(under)), 0);

	/* Write one more at 9 */
	rex.rx_nr = strlen(expected);
	d_iov_set(&sgl.sg_iovs[0], (void *)expected, strlen(expected));
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 9, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/* Punch the object at 10 */
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, 10, 0, 0, NULL, 0, NULL,
			   NULL);
	assert_int_equal(rc, 0);

	/* Write one more at 11 */
	rex.rx_nr = strlen(latest);
	d_iov_set(&sgl.sg_iovs[0], (void *)latest, strlen(latest));
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 11, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/** read old one for sanity */
	memset(buf, 0, sizeof(buf));
	rex.rx_nr = strlen(under);
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, strlen(under));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 5, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_int_equal(strncmp(buf, under, strlen(under)), 0);

	/* Non recursive iteration first */
	vos_check_recx(state, 8, 0, oid, &dkey, &akey, 1, 0);

	(void)vos_check_akey; /* For now, unused. Reference to avoid warning */

	/* Now recurse at an epoch prior to punches */
	vos_check_dkey(state, 1, 0, oid, 1, 1, 1, 1, 1, 0);

	/* Now recurse including punched entries */
	vos_check_dkey(state, 8, VOS_IT_PUNCHED, oid, 1, 0, 1, 0, 4, 0);

	/* Now recurse after punch, not including punched entries */
	vos_check_obj(state, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	/* Now recurse including punched entries after object punch */
	vos_check_obj(state, 10, VOS_IT_PUNCHED, 1, 0, 1, 0, 1, 0, 5, 0);

	/* Now recurse visible entries at 11 */
	vos_check_obj(state, 11, 0, 1, 0, 1, 0, 1, 0, 1, 0);

	/** Read the value at 11 */
	memset(buf, 0, sizeof(buf));
	rex.rx_nr = strlen(under);
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, strlen(under));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 11, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_int_equal(sgl.sg_iovs[0].iov_len, strlen(latest));
	assert_int_equal(strncmp(buf, latest, strlen(latest)), 0);

	d_sgl_fini(&sgl, false);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_RECX | DAOS_GET_MAX,
			       11, &dkey, &akey, &rex, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(rex.rx_idx, 0);
	assert_int_equal(rex.rx_nr, strlen(latest));
}

static void
simple_multi_update(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_iod_t		iod[2] = {0};
	d_sg_list_t		sgl[2] = {0};
	const char		*values[2] = {"HelloWorld", "HelloLonelyWorld"};
	const char		*overwrite[2] = {"ByeWorld", "ByeLonelyWorld"};
	uint8_t			dkey_val = 0;
	char			akey[2] = {'a', 'b'};
	char			buf[2][32] = {0};
	daos_unit_oid_t		oid;
	int			i;

	test_args_reset(arg, VPOOL_SIZE);

	memset(iod, 0, sizeof(iod));

	/* Set up dkey and akey */
	oid = gen_oid(0);
	d_iov_set(&dkey, &dkey_val, sizeof(dkey_val));

	for (i = 0; i < 2; i++) {
		rc = d_sgl_init(&sgl[i], 1);
		assert_int_equal(rc, 0);
		iod[i].iod_type = DAOS_IOD_SINGLE;
		iod[i].iod_size = strlen(values[i]) + 1;
		d_iov_set(&sgl[i].sg_iovs[0], (void *)values[i],
			  strlen(values[i]) + 1);
		d_iov_set(&iod[i].iod_name, &akey[i], sizeof(akey[i]));
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = NULL;
	}

	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 1, 0,
			    0, &dkey, 2, iod, NULL, sgl);
	assert_int_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		iod[i].iod_size = 0; /* size fetch */
		d_iov_set(&sgl[i].sg_iovs[0], (void *)buf[i], sizeof(buf[i]));
	}

	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 1, 0, &dkey, 2,
			   iod, sgl);
	assert_int_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		assert_true(iod[i].iod_size == (strlen(values[i]) + 1));
		assert_memory_equal(buf[i], values[i], strlen(values[i]) + 1);
		/* Setup next update */
		iod[i].iod_size = strlen(overwrite[i]) + 1;
		d_iov_set(&sgl[i].sg_iovs[0], (void *)overwrite[i],
			  strlen(overwrite[i]) + 1);
	}

	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, 2, 0, 0, NULL, 0, NULL,
			   NULL);
	assert_int_equal(rc, 0);

	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 1, 0,
			    0, &dkey, 2, iod, NULL, sgl);
	assert_int_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		iod[i].iod_size = 0; /* size fetch */
		d_iov_set(&sgl[i].sg_iovs[0], (void *)buf[i], sizeof(buf[i]));
	}

	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 1, 0, &dkey, 2,
			   iod, sgl);
	assert_int_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		assert_true(iod[i].iod_size == (strlen(overwrite[i]) + 1));
		assert_memory_equal(buf[i], overwrite[i],
				    strlen(overwrite[i]) + 1);
		d_sgl_fini(&sgl[i], false);
	}
}

static void
object_punch_and_fetch(void **state)
{
	struct io_test_args	*arg = *state;
	const char		*value = "HelloCruelWorld";
	daos_key_t		 update_keys[2];
	daos_key_t		 punch_keys[2];
	daos_key_t		*punch_akeys[2];
	daos_key_t		*actual_keys[2];
	daos_key_t		 dkey;
	daos_iod_t		 iod = {0};
	d_sg_list_t		 sgl = {0};
	daos_unit_oid_t		 oid;
	daos_epoch_t		 epoch = 1;
	int			 i = 0;
	int			 rc = 0;
	uint8_t			 stable_key = 0;
	char			 key1 = 'a';
	char			 key2 = 'b';
	char			 buf[32] = {0};

	test_args_reset(arg, VPOOL_SIZE);

	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);
	d_iov_set(&update_keys[0], &stable_key, sizeof(stable_key));
	d_iov_set(&update_keys[1], &key1, sizeof(key1));
	d_iov_set(&punch_keys[0], &stable_key, sizeof(stable_key));
	d_iov_set(&punch_keys[1], &key2, sizeof(key2));
	punch_akeys[0] = &punch_keys[1];
	punch_akeys[1] = NULL;
	actual_keys[0] = &dkey;
	actual_keys[1] = &iod.iod_name;

	for (i = 0; i < 2; i++) {
		/* Set up dkey and akey */
		oid = gen_oid(0);

		*actual_keys[0] = update_keys[i];
		*actual_keys[1] = update_keys[1 - i];

		iod.iod_type = DAOS_IOD_SINGLE;
		iod.iod_size = 0;
		d_iov_set(&sgl.sg_iovs[0], (void *)value, strlen(value) + 1);
		iod.iod_nr = 1;
		iod.iod_recxs = NULL;

		rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
				    0, &dkey, 1, &iod, NULL,
				    &sgl);
		assert_int_equal(rc, 0);

		*actual_keys[0] = punch_keys[i];
		*actual_keys[1] = punch_keys[1 - i];

		rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0,
				   &dkey, 1 - i, punch_akeys[i], NULL);
		assert_int_equal(rc, 0);

		iod.iod_size = 0;
		d_iov_set(&sgl.sg_iovs[0], (void *)buf, sizeof(buf));

		rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch++, 0, &dkey,
				   1, &iod, &sgl);
		assert_int_equal(rc, 0);
		assert_int_equal(iod.iod_size, 0);
	}

	d_sgl_fini(&sgl, false);
}

#define SM_BUF_LEN 64

static void
sgl_test(void **state)
{
	struct io_test_args	*arg = *state;
	daos_key_t		 dkey;
	daos_iod_t		 iod = {0};
	d_iov_t			 sg_iov[SM_BUF_LEN] = {0};
	d_sg_list_t		 sgl;
	daos_recx_t		 recx[SM_BUF_LEN];
	char			 rbuf[SM_BUF_LEN];
	daos_unit_oid_t		 oid;
	daos_epoch_t		 epoch = 1;
	int			 i = 0;
	int			 rc = 0;
	char			 val = 'x';
	char			 key1 = 'a';
	char			 key2 = 'b';

	test_args_reset(arg, VPOOL_SIZE);

	oid = gen_oid(0);

	d_iov_set(&dkey, &key1, sizeof(key1));
	d_iov_set(&iod.iod_name, &key2, sizeof(key2));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	d_iov_set(&sg_iov[0], &val, sizeof(val));
	iod.iod_nr = 1;
	iod.iod_size = 1;
	recx[0].rx_nr = 1;
	iod.iod_recxs = recx;
	iod.iod_type = DAOS_IOD_ARRAY;

	/* Write just index 2 */
	recx[0].rx_idx = 2;
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	memset(rbuf, 'a', sizeof(rbuf));
	iod.iod_size = 0;
	d_iov_set(&sg_iov[0], rbuf, sizeof(rbuf));
	recx[0].rx_idx = 0;
	recx[0].rx_nr = SM_BUF_LEN;

	/* Fetch whole buffer */
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch++, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);
	for (i = 0; i < SM_BUF_LEN; i++) {
		if (i == 2)
			assert_int_equal((int)rbuf[i], (int)val);
		else
			assert_int_equal((int)rbuf[i], (int)'a');
	}

	/* Fetch every other record to contiguous buffer */
	memset(rbuf, 'a', sizeof(rbuf));
	d_iov_set(&sg_iov[0], rbuf, SM_BUF_LEN / 2);
	iod.iod_size = 0;
	iod.iod_nr = SM_BUF_LEN / 2;
	for (i = 0; i < SM_BUF_LEN / 2; i++) {
		recx[i].rx_idx = i * 2;
		recx[i].rx_nr = 1;
	}
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch++, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);
	for (i = 0; i < SM_BUF_LEN; i++) {
		if (i == 1)
			assert_int_equal((int)rbuf[i], (int)val);
		else
			assert_int_equal((int)rbuf[i], (int)'a');
	}

	/* Fetch every other record to non-contiguous buffer */
	memset(rbuf, 'a', sizeof(rbuf));
	for (i = 0; i < SM_BUF_LEN / 2; i++) {
		/* Mix it up a bit with the offsets */
		d_iov_set(&sg_iov[i], &rbuf[((i + 3) * 2) % SM_BUF_LEN], 1);
	}
	sgl.sg_nr = SM_BUF_LEN;
	iod.iod_size = 0;
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch++, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);
	for (i = 0; i < SM_BUF_LEN; i++) {
		if (i == 8)
			assert_int_equal((int)rbuf[i], (int)val);
		else
			assert_int_equal((int)rbuf[i], (int)'a');
	}

}

enum {
	OP_PUNCH_OBJ,
	OP_PUNCH_DKEY,
	OP_PUNCH_AKEY,
	OP_UPDATE,
	OP_FETCH,
};

/** maximum value/string length for conditional tests */
#define OP_MAX_STRING 32

static void
copy_str(char *buf, const char *src, size_t *len)
{
	memset(buf, 0, OP_MAX_STRING);

	if (src == NULL) {
		*len = 0;
		return;
	}

	*len = strnlen(src, OP_MAX_STRING - 1);
	strncpy(buf, src, OP_MAX_STRING - 1);
}

static void
obj_punch_op(void **state, daos_handle_t coh, daos_unit_oid_t oid,
	     daos_epoch_t epoch, uint64_t flags)
{
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	int			 rc;

	vts_dtx_begin(&oid, coh, epoch, 0, &dth);
	rc = vos_obj_punch(coh, oid, epoch, 0, flags, NULL, 0, NULL, dth);
	xid = dth->dth_xid;
	vts_dtx_end(dth);

	assert_int_equal(rc, 0);

	rc = vos_dtx_commit(coh, &xid, 1, NULL);
	assert_int_equal(rc, 1);
}

static void
cond_dkey_punch_op(void **state, daos_handle_t coh, daos_unit_oid_t oid,
		   daos_epoch_t epoch, const char *dkey_str, uint64_t flags,
		   int expected_rc)
{
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	char			 dkey_buf[OP_MAX_STRING];
	size_t			 dkey_len;
	d_iov_t			 dkey;
	int			 rc;

	copy_str(dkey_buf, dkey_str, &dkey_len);
	d_iov_set(&dkey, dkey_buf, dkey_len);

	vts_dtx_begin(&oid, coh, epoch, 0, &dth);
	rc = vos_obj_punch(coh, oid, epoch, 0, flags, &dkey, 0, NULL, dth);
	xid = dth->dth_xid;
	vts_dtx_end(dth);

	assert_int_equal(rc, expected_rc);

	if (expected_rc == 0) {
		rc = vos_dtx_commit(coh, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}
}

static void
cond_akey_punch_op(void **state, daos_handle_t coh, daos_unit_oid_t oid,
		   daos_epoch_t epoch, const char *dkey_str,
		   const char *akey_str, uint64_t flags, int expected_rc)
{
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	char			 dkey_buf[OP_MAX_STRING];
	size_t			 dkey_len;
	d_iov_t			 dkey;
	char			 akey_buf[OP_MAX_STRING];
	size_t			 akey_len;
	d_iov_t			 akey;
	int			 rc;

	copy_str(dkey_buf, dkey_str, &dkey_len);
	d_iov_set(&dkey, dkey_buf, dkey_len);
	copy_str(akey_buf, akey_str, &akey_len);
	d_iov_set(&akey, akey_buf, akey_len);

	vts_dtx_begin(&oid, coh, epoch, 0, &dth);
	rc = vos_obj_punch(coh, oid, epoch, 0, flags, &dkey, 1, &akey, dth);
	xid = dth->dth_xid;
	vts_dtx_end(dth);

	assert_int_equal(rc, expected_rc);

	if (expected_rc == 0) {
		rc = vos_dtx_commit(coh, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}
}

#define cond_fetch_op(...)						\
	do {								\
		VERBOSE_PRINT("Called cond_fetch_op at %s:%d\n",	\
			      __FILE__, __LINE__);			\
		cond_fetch_op_(__VA_ARGS__);				\
	} while (0)
static void
cond_fetch_op_(void **state, daos_handle_t coh, daos_unit_oid_t oid,
	       daos_epoch_t epoch, bool use_tx, const char *dkey_str,
	       const char *akey_str, uint64_t flags, int expected_rc,
	       d_sg_list_t *sgl, const char *value_str, char fill_char)
{
	struct dtx_handle	*dth = NULL;
	char			 dkey_buf[OP_MAX_STRING];
	char			 akey_buf[OP_MAX_STRING];
	char			 value_buf[OP_MAX_STRING];
	char			 read_buf[OP_MAX_STRING];
	daos_iod_t		 iod = {0};
	d_iov_t			 dkey;
	size_t			 dkey_len;
	size_t			 akey_len;
	size_t			 value_len;
	int			 rc;

	copy_str(value_buf, value_str, &value_len);
	copy_str(dkey_buf, dkey_str, &dkey_len);
	d_iov_set(&dkey, dkey_buf, dkey_len);
	copy_str(akey_buf, akey_str, &akey_len);
	d_iov_set(&iod.iod_name, akey_buf, akey_len);
	memset(read_buf, fill_char, OP_MAX_STRING);

	iod.iod_type = DAOS_IOD_SINGLE;
	iod.iod_size = value_len;
	d_iov_set(&sgl->sg_iovs[0], read_buf, OP_MAX_STRING);
	iod.iod_nr = 1;
	iod.iod_recxs = NULL;

	if (use_tx)
		vts_dtx_begin(&oid, coh, epoch, 0, &dth);
	rc = vos_obj_fetch_ex(coh, oid, epoch, flags, &dkey, 1, &iod, sgl, dth);
	assert_int_equal(rc, expected_rc);
	if (use_tx)
		vts_dtx_end(dth);

	if (value_len == 0)
		return;

	assert_int_equal(memcmp(value_buf, read_buf, value_len), 0);
}

#define cond_updaten_op(...)						\
	do {								\
		VERBOSE_PRINT("Called cond_updaten_op at %s:%d\n",	\
			      __FILE__, __LINE__);			\
		cond_updaten_op_(__VA_ARGS__);				\
	} while (0)
static void
cond_updaten_op_(void **state, daos_handle_t coh, daos_unit_oid_t oid,
		 daos_epoch_t epoch, const char *dkey_str,
		 uint64_t flags, int expected_rc, d_sg_list_t *sgl, int n, ...)
{
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	const char		*val_arg;
	const char		*akey_arg;
	va_list			 list;
	char			 dkey_buf[OP_MAX_STRING];
	char			 akey_buf[n][OP_MAX_STRING];
	char			 value_buf[n][OP_MAX_STRING];
	daos_iod_t		 iod[n];
	d_iov_t			 dkey;
	size_t			 dkey_len;
	size_t			 akey_len[n];
	size_t			 value_len[n];
	int			 rc;
	int			 i;

	memset(&iod, 0, sizeof(iod[0]) * n);

	copy_str(dkey_buf, dkey_str, &dkey_len);
	d_iov_set(&dkey, dkey_buf, dkey_len);
	va_start(list, n);
	for (i = 0; i < n; i++) {
		akey_arg = va_arg(list, const char *);
		val_arg = va_arg(list, const char *);
		copy_str(value_buf[i], val_arg, &value_len[i]);
		copy_str(akey_buf[i], akey_arg, &akey_len[i]);
		d_iov_set(&iod[i].iod_name, akey_buf[i], akey_len[i]);
		d_iov_set(&sgl[i].sg_iovs[0], value_buf[i], value_len[i]);
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;

		iod[i].iod_type = DAOS_IOD_SINGLE;
		iod[i].iod_size = value_len[i];
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = NULL;

	}
	va_end(list);
	vts_dtx_begin(&oid, coh, epoch, 0, &dth);
	rc = vos_obj_update_ex(coh, oid, epoch, 0, flags, &dkey, n, iod, NULL,
			       sgl, dth);
	xid = dth->dth_xid;
	assert_int_equal(rc, expected_rc);
	vts_dtx_end(dth);

	if (expected_rc == 0) {
		rc = vos_dtx_commit(coh, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}

}

#define cond_update_op(...)						\
	do {								\
		VERBOSE_PRINT("Called cond_update_op at %s:%d\n",	\
			      __FILE__, __LINE__);			\
		cond_update_op_(__VA_ARGS__);				\
	} while (0)
static void
cond_update_op_(void **state, daos_handle_t coh, daos_unit_oid_t oid,
		daos_epoch_t epoch, const char *dkey_str, const char *akey_str,
		uint64_t flags, int expected_rc, d_sg_list_t *sgl,
		const char *value_str)
{

	cond_updaten_op_(state, coh, oid, epoch, dkey_str, flags, expected_rc,
			 sgl, 1, akey_str, value_str);
}

#define MAX_SGL 10
static void
cond_test(void **state)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t		 oid;
	d_sg_list_t		 sgl[MAX_SGL] = {0};
	d_iov_t			 iov[MAX_SGL];
	daos_epoch_t		 epoch = start_epoch;
	int			 i;

	test_args_reset(arg, VPOOL_SIZE);

	oid = gen_oid(0);

	for (i = 0; i < MAX_SGL; i++) {
		sgl[i].sg_iovs = &iov[i];
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 1;
	}

	/** Conditional update of non-existed key should fail */
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "a", "b",
		       VOS_OF_COND_DKEY_UPDATE,
		       -DER_NONEXIST, sgl, "foo");
	/** Non conditional update should fail due to later read */
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch - 3, "a", "b",
		       0, -DER_TX_RESTART, sgl, "foo");
	/** Conditional insert should succeed */
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "a", "b",
		       VOS_OF_COND_DKEY_INSERT, 0, sgl,
		       "foo");
	/** Conditional insert should fail */
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "a", "b",
		       VOS_OF_COND_DKEY_INSERT,
		       -DER_EXIST, sgl, "bar");
	/** Check the value */
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, true, "a", "b",
		      0, 0, sgl, "foo", 'x');
	/** Check the value before, should be empty */
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch - 4, true, "a", "b",
		      0, 0, sgl, "xxxx", 'x');
	obj_punch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, 0);
	/** Non conditional fetch should not see data anymore */
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, true, "a", "b",
		      0, 0, sgl, "xxxx", 'x');
	/** Conditional update of non-existent key should fail */
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch - 1, "a", "b",
		       VOS_OF_COND_DKEY_UPDATE,
		       -DER_NONEXIST, sgl, "foo");
	/** Conditional punch of non-existent akey should fail */
	cond_akey_punch_op(state, arg->ctx.tc_co_hdl, oid, epoch, "a", "b",
			   VOS_OF_COND_PUNCH, -DER_NONEXIST);
	/** Key doesn't exist still, that supersedes read conflict */
	cond_dkey_punch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "a",
			   VOS_OF_COND_PUNCH, -DER_NONEXIST);
	/** Conditional punch of non-existed dkey should fail */
	cond_dkey_punch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "a",
			   VOS_OF_COND_PUNCH, -DER_NONEXIST);
	cond_updaten_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "z",
			VOS_OF_COND_DKEY_UPDATE,
			-DER_NONEXIST, sgl, 5, "a", "foo", "b", "bar", "c",
			"foobar", "d", "value", "e", "abc");
	cond_updaten_op(state, arg->ctx.tc_co_hdl, oid, epoch - 2, "z",
			VOS_OF_COND_DKEY_INSERT,
			-DER_TX_RESTART, sgl, 5, "a", "foo", "b", "bar", "c",
			"foobar", "d", "value", "e", "abc");
	cond_updaten_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "z",
			VOS_OF_COND_DKEY_INSERT,
			0, sgl, 5, "a", "foo", "b", "bar", "c",
			"foobar", "d", "value", "e", "abc");
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, true, "z", "a",
		      VOS_OF_COND_AKEY_FETCH, 0, sgl,
		      "foo", 'x');
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, true, "a",
		      "nonexist", VOS_OF_COND_AKEY_FETCH, -DER_NONEXIST,
		      sgl, "xxx", 'x');
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch - 2, "a",
		       "nonexist", 0, -DER_TX_RESTART, sgl,
		       "foo");
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, true, "nonexist",
		      "a", VOS_OF_COND_DKEY_FETCH,
		      -DER_NONEXIST, sgl, "xxx", 'x');
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch - 2, "nonexist",
		       "a", 0, -DER_TX_RESTART, sgl, "foo");
	cond_update_op(state, arg->ctx.tc_co_hdl, oid, epoch++, "nonexist",
		       "a", 0, 0, sgl, "foo");
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, true, "nonexist",
		      "a", VOS_OF_COND_DKEY_FETCH, 0, sgl,
		      "foo", 'x');
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, false,
		      "nonexist", "a", VOS_OF_COND_DKEY_FETCH, 0, sgl,
		      "foo", 'x');
	cond_fetch_op(state, arg->ctx.tc_co_hdl, oid, epoch++, false,
		      "dead", "a", VOS_OF_COND_DKEY_FETCH, -DER_NONEXIST, sgl,
		      "xxx", 'x');

	oid = gen_oid(0);
	/** Test duplicate akey */
	cond_updaten_op(state, arg->ctx.tc_co_hdl, oid, epoch, "a",
			0, -DER_NO_PERM, sgl, 5, "c", "foo",
			"c", "bar", "d", "val", "e", "flag", "f", "temp");
	cond_updaten_op(state, arg->ctx.tc_co_hdl, oid, epoch, "a",
			0, -DER_NO_PERM, sgl, 5, "new",
			"foo", "f", "bar", "d", "val", "e", "flag", "new",
			"temp");

	start_epoch = epoch + 1;
}

/** Making the oid generation deterministic, I get to 304 before I hit a false
 *  collision on the oid.   This may indicate the hashing needs to be improved
 *  but for now, it's good enough.  In general, the chance of a single
 *  collision is very high well before we get close to saturation due
 *  to the birthday paradox.
 */
#define NUM_OIDS 304
static void
multiple_oid_cond_test(void **state)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t		 oid;
	d_sg_list_t		 sgl = {0};
	d_iov_t			 iov = {0};
	daos_epoch_t		 epoch = start_epoch + NUM_OIDS * 3;
	int			 i;

	start_epoch = epoch + 1;

	test_args_reset(arg, VPOOL_SIZE);
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;

	reset_oid_stable(0xdeadbeef);

	/** Same dkey/akey, multiple objects */
	for (i = 0; i < NUM_OIDS; i++) {
		oid = gen_oid_stable(0);
		cond_update_op(state, arg->ctx.tc_co_hdl, oid,
			       epoch - 2, "dkey", "akey", 0, 0, &sgl, "foo");
		cond_update_op(state, arg->ctx.tc_co_hdl, oid,
			       epoch - 1, "dkey", "akey2",
			       VOS_OF_COND_AKEY_UPDATE, -DER_NONEXIST, &sgl,
			       "foo");
		cond_update_op(state, arg->ctx.tc_co_hdl, oid,
			       epoch, "dkey", "akey2", VOS_OF_COND_AKEY_INSERT,
			       0, &sgl, "foo");
		epoch -= 3;
	}
}

#define REM_VAL1 "xyz"
#define REM_VAL2 "zyx"
#define REM_VAL3 "abcd"

static void
remove_test(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_range_t	 epr;
	daos_key_t		 dkey;
	daos_iod_t		 iod = {0};
	d_iov_t			 sg_iov[SM_BUF_LEN] = {0};
	d_sg_list_t		 sgl;
	daos_recx_t		 recx[SM_BUF_LEN];
	char			 rbuf[SM_BUF_LEN];
	daos_unit_oid_t		 oid;
	daos_epoch_t		 epoch = start_epoch;
	int			 rc = 0;
	char			 key1 = 'a';
	char			 key2 = 'b';

	test_args_reset(arg, VPOOL_SIZE);

	oid = gen_oid(0);

	d_iov_set(&dkey, &key1, sizeof(key1));
	d_iov_set(&iod.iod_name, &key2, sizeof(key2));
	sgl.sg_nr = 3;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	d_iov_set(&sg_iov[0], REM_VAL1, sizeof(REM_VAL1) - 1);
	d_iov_set(&sg_iov[1], REM_VAL2, sizeof(REM_VAL2) - 1);
	d_iov_set(&sg_iov[2], REM_VAL3, sizeof(REM_VAL3) - 1);
	iod.iod_nr = 3;
	iod.iod_size = 1;
	recx[0].rx_idx = 0;
	recx[0].rx_nr = sizeof(REM_VAL1) - 1;
	recx[1].rx_idx = recx[0].rx_idx + recx[0].rx_nr;
	recx[1].rx_nr = sizeof(REM_VAL2) - 1;
	recx[2].rx_idx = recx[1].rx_idx + recx[1].rx_nr;
	recx[2].rx_nr = sizeof(REM_VAL3) - 1;
	iod.iod_recxs = recx;
	iod.iod_type = DAOS_IOD_ARRAY;

	/* Write the records */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/* Try removing partial entries */
	recx[3].rx_idx = 1;
	recx[3].rx_nr = 10;
	epr.epr_lo = 0;
	epr.epr_hi = epoch;
	rc = vos_obj_array_remove(arg->ctx.tc_co_hdl, oid, &epr, &dkey,
				  &iod.iod_name, &recx[3]);
	assert_int_equal(rc, -DER_NO_PERM);

	/* Swap 1 and 2 and write again */
	d_iov_set(&sg_iov[1], REM_VAL1, sizeof(REM_VAL1) - 1);
	d_iov_set(&sg_iov[0], REM_VAL2, sizeof(REM_VAL2) - 1);
	iod.iod_nr = 2;
	iod.iod_size = 1;
	recx[0].rx_idx = 0;
	recx[0].rx_nr = sizeof(REM_VAL2) - 1;
	recx[1].rx_idx = recx[0].rx_idx + recx[0].rx_nr;
	recx[0].rx_nr = sizeof(REM_VAL1) - 1;

	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
			    0, &dkey, 1, &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	recx[0].rx_idx = 0;
	recx[0].rx_nr = sizeof(REM_VAL1) + sizeof(REM_VAL2) +
		sizeof(REM_VAL3) - 3;
	iod.iod_nr = 1;
	d_iov_set(&sg_iov[0], rbuf, sizeof(rbuf));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod,
			   &sgl);
	assert_int_equal(rc, 0);
	assert_memory_equal(rbuf, REM_VAL2, sizeof(REM_VAL2) - 1);
	assert_memory_equal(rbuf + sizeof(REM_VAL2) - 1, REM_VAL1,
			    sizeof(REM_VAL1) - 1);
	assert_memory_equal(rbuf + sizeof(REM_VAL2) + sizeof(REM_VAL1) - 2,
			    REM_VAL3, sizeof(REM_VAL3) - 1);

	/* Now remove the last update only */
	recx[3].rx_idx = 0;
	recx[3].rx_nr = recx[2].rx_idx;
	epr.epr_hi = epoch;
	epr.epr_lo = epoch - 1;
	rc = vos_obj_array_remove(arg->ctx.tc_co_hdl, oid, &epr, &dkey,
				  &iod.iod_name, &recx[3]);
	assert_int_equal(rc, 0);

	/* Now fetch again, should see old value */
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod,
			   &sgl);
	assert_int_equal(rc, 0);
	assert_memory_equal(rbuf, REM_VAL1, sizeof(REM_VAL1) - 1);
	assert_memory_equal(rbuf + sizeof(REM_VAL1) - 1, REM_VAL2,
			    sizeof(REM_VAL2) - 1);
	assert_memory_equal(rbuf + sizeof(REM_VAL2) + sizeof(REM_VAL1) - 2,
			    REM_VAL3, sizeof(REM_VAL3) - 1);

	start_epoch = epoch + 1;
}

static void
small_sgl(void **state)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t	 oid;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[3];
	d_iov_t		 sg_iov[3];
	daos_iod_t	 iod[3];
	char		 buf1[24];
	char		 buf2[24];
	char		 buf3[24];
	int		 i, rc;

	dts_buf_render(buf1, 24);
	dts_buf_render(buf2, 24);
	dts_buf_render(buf3, 24);

	test_args_reset(arg, VPOOL_SIZE);

	oid = gen_oid(0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], buf1, 4);
	d_iov_set(&sg_iov[1], buf2, 8);
	d_iov_set(&sg_iov[2], buf3, 4);

	for (i = 0; i < 3; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		sgl[i].sg_iovs = &sg_iov[i];
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = NULL;
		iod[i].iod_type = DAOS_IOD_SINGLE;
	}

	d_iov_set(&iod[0].iod_name, "akey1", strlen("akey1"));
	d_iov_set(&iod[1].iod_name, "akey2", strlen("akey2"));
	d_iov_set(&iod[2].iod_name, "akey3", strlen("akey2"));
	iod[0].iod_size = 4;
	iod[1].iod_size = 8;
	iod[2].iod_size = 4;

	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, 1, 0, 0, &dkey, 3, iod,
			    NULL, sgl);
	assert_int_equal(rc, 0);

	/** setup for fetch */
	d_iov_set(&sg_iov[0], buf1, 4);
	d_iov_set(&sg_iov[1], buf2, 2);
	d_iov_set(&sg_iov[2], buf3, 9);
	for (i = 0; i < 3; i++)
		iod[i].iod_size = DAOS_REC_ANY;

	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, 2, 0, &dkey, 3,
			   iod, sgl);
	assert_int_equal(rc, -DER_REC2BIG);
}

static void
minor_epoch_punch_sv(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_epoch_t		epoch = start_epoch;
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	const char		*expected = "xxxxx";
	const char		*first = "Hello";
	char			buf[32] = {0};
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	daos_unit_oid_t		oid;

	test_args_reset(arg, VPOOL_SIZE);

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* Set up dkey and akey */
	oid = gen_oid(arg->ofeat);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rex.rx_idx = 0;
	rex.rx_nr = strlen(first);

	iod.iod_type = DAOS_IOD_SINGLE;
	iod.iod_size = strlen(first);
	iod.iod_name = akey;
	iod.iod_recxs = NULL;
	iod.iod_nr = 1;

	/* Allocate memory for the scatter-gather list */
	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

	d_iov_set(&sgl.sg_iovs[0], (void *)first, iod.iod_size);

	vts_dtx_begin_ex(&oid, arg->ctx.tc_co_hdl, epoch++, 0, 0, 2, &dth);

	/* Write the first value */
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid,
			       0 /* epoch comes from dth */, 0, 0, &dkey, 1,
			       &iod, NULL, &sgl, dth);
	if (rc != 0)
		goto tx_end;

	/* Punch the akey */
	dth->dth_op_seq = 2;
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid,
			   0 /* epoch comes from dth */, 0, 0, &dkey, 1, &akey,
			   dth);
tx_end:
	xid = dth->dth_xid;
	vts_dtx_end(dth);
	assert_int_equal(rc, 0);

	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
	assert_int_equal(rc, 1);

	/* Now read back original # of bytes */
	iod.iod_size = 0;
	memset(buf, 'x', sizeof(buf));
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, sizeof(buf));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch++, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);

	assert_int_equal(iod.iod_size, 0);
	assert_memory_equal(buf, expected, strlen(expected));

	d_sgl_fini(&sgl, false);
	start_epoch = epoch + 1;
}

static void
minor_epoch_punch_array(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_epoch_t		epoch = start_epoch;
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	const char		*expected = "xxxxxLonelyWorld";
	const char		*first = "Hello";
	const char		*second = "LonelyWorld";
	char			buf[32] = {0};
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	daos_unit_oid_t		oid;

	test_args_reset(arg, VPOOL_SIZE);

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* Set up dkey and akey */
	oid = gen_oid(arg->ofeat);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rex.rx_idx = 0;
	rex.rx_nr = strlen(first);

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_name = akey;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	/* Allocate memory for the scatter-gather list */
	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

	d_iov_set(&sgl.sg_iovs[0], (void *)first, rex.rx_nr);

	vts_dtx_begin_ex(&oid, arg->ctx.tc_co_hdl, epoch++, 0, 0, 3, &dth);

	/* Write the first value */
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid,
			       0 /* epoch comes from dth */, 0, 0, &dkey, 1,
			       &iod, NULL, &sgl, dth);
	if (rc != 0)
		goto tx_end;

	/* Punch the akey */
	dth->dth_op_seq = 2;
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid,
			   0 /* epoch comes from dth */, 0, 0, &dkey, 1, &akey,
			   dth);
	if (rc != 0)
		goto tx_end;

	/* Do next update */
	dth->dth_op_seq = 3;
	rex.rx_idx = rex.rx_nr;
	rex.rx_nr = strlen(second);
	d_iov_set(&sgl.sg_iovs[0], (void *)second, strlen(second));
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid,
			       0 /* epoch comes from dth */, 0, 0, &dkey, 1,
			       &iod, NULL, &sgl, dth);
tx_end:
	xid = dth->dth_xid;
	vts_dtx_end(dth);
	assert_int_equal(rc, 0);

	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
	assert_int_equal(rc, 1);

	/* Now read back original # of bytes */
	rex.rx_idx = 0;
	rex.rx_nr = strlen(expected);
	memset(buf, 'x', sizeof(buf));
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, strlen(expected));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch++, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);

	assert_memory_equal(buf, expected, strlen(expected));

	d_sgl_fini(&sgl, false);
	start_epoch = epoch + 1;
}

static void
minor_epoch_punch_rebuild(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_epoch_t		epoch = start_epoch;
	const char		*expected = "xxxxxlonelyworld";
	const char		*first = "hello";
	const char		*second = "lonelyworld";
	char			buf[32] = {0};
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_DKEY_SIZE];
	daos_unit_oid_t		oid;

	test_args_reset(arg, VPOOL_SIZE);

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* set up dkey and akey */
	oid = gen_oid(arg->ofeat);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

	rex.rx_idx = 0;
	rex.rx_nr = strlen(first);

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_name = akey;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	/** let's simulate replay scenario where rebuild replays a minor epoch
	 * punch and an update with the same major epoch
	 *
	 * this can happen with something like the following
	 * dtx does an update and commits at epoch 1.
	 * daos takes a snapshot at epoch 2.
	 * dtx does a distributed transaction that does punch and update at
	 * 3.1 and 3.2, respectively and commits
	 *
	 * at some later time, rebuild runs.  while rebuilding the snapshot,
	 * it will replay the future punch at 3.   internally, on replay,
	 * the minor epoch is set to max for updates and max - 1 for punch.
	 */
	d_iov_set(&sgl.sg_iovs[0], (void *)first, rex.rx_nr);
	epoch++;
	/** First write the punched extent */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/** Now the "replay" punch */
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch + 1, 0,
			   VOS_OF_REPLAY_PC, &dkey, 1, &akey, NULL);
	assert_int_equal(rc, 0);

	/** Now write the update at the same major epoch that is after the
	 *  punched extent
	 */
	rex.rx_idx = strlen(first);
	rex.rx_nr = strlen(second);
	d_iov_set(&sgl.sg_iovs[0], (void *)second, rex.rx_nr);
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch + 1, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/** Now check the value matches the expected value */
	memset(buf, 'x', sizeof(buf));
	rex.rx_idx = 0;
	rex.rx_nr = sizeof(buf);
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, sizeof(buf));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch + 2, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_memory_equal(buf, expected, strlen(expected));
	epoch += 2;

	d_sgl_fini(&sgl, false);

	start_epoch = epoch + 1;
}

#define NUM_RANKS 100
#define NUM_KEYS 1000
#define DKEY_NAME "dkey"
static void
many_keys(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	int			i, num_keys = NUM_KEYS;
	int			rank;
	daos_key_t		dkey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_epoch_t		epoch = start_epoch;
	const char		*w = "x";
	char			*dkey_buf = DKEY_NAME;
	char			akey_buf[UPDATE_DKEY_SIZE];
	daos_unit_oid_t		oid;

	if (DAOS_ON_VALGRIND)
		num_keys /= 500;

	test_args_reset(arg, VPOOL_10G);

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* set up oid and dkey */
	oid = gen_oid(0);
	d_iov_set(&dkey, &dkey_buf[0], sizeof(DKEY_NAME) - 1);

	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

	rex.rx_idx = 0;
	rex.rx_nr = sizeof(w) - 1;

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	d_iov_set(&sgl.sg_iovs[0], (void *)w, rex.rx_nr);

	/** Attempt to create a hash collision */
	for (rank = 0; rank < NUM_RANKS; rank++) {
		for (i = 0; i < num_keys; i++) {
			epoch++;
			memset(akey_buf, 0, sizeof(akey_buf));
			sprintf(&akey_buf[0], "file.mdtest.%d.%d", rank,
				i);
			d_iov_set(&iod.iod_name, &akey_buf[0],
				  strlen(akey_buf));

			rc = vos_obj_update(arg->ctx.tc_co_hdl, oid,
					    epoch, 0, 0, &dkey, 1, &iod,
					    NULL, &sgl);
			assert_int_equal(rc, 0);
		}
	}

	d_sgl_fini(&sgl, false);

	start_epoch = epoch + 1;
}

static void
test_inprogress_parent_punch(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_key_t		akey1;
	daos_key_t		akey2;
	daos_key_t		akey3;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	struct dtx_handle	*dth1;
	struct dtx_handle	*dth2;
	daos_epoch_t		epoch = start_epoch;
	struct dtx_id		xid1;
	struct dtx_id		xid2;
	const char		*expected = "xxxxx";
	const char		*first = "hello";
	char			buf[32] = {0};
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey1_buf[UPDATE_DKEY_SIZE];
	char			akey2_buf[UPDATE_DKEY_SIZE];
	char			akey3_buf[UPDATE_DKEY_SIZE];
	daos_unit_oid_t		oid;

	test_args_reset(arg, VPOOL_SIZE);

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* set up dkey and akey */
	oid = gen_oid(arg->ofeat);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey1_buf[0], arg->akey_size, false, arg);
	vts_key_gen(&akey2_buf[0], arg->akey_size, false, arg);
	vts_key_gen(&akey3_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey1, &akey1_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);
	set_iov(&akey2, &akey2_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);
	set_iov(&akey3, &akey3_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

	rex.rx_idx = 0;
	rex.rx_nr = strlen(first);

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_name = akey1;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	/** This test replays one tricky scenario
	 *
	 * u(c/o/d/a1, 10) // committed
	 * u(c/o/d/a2, 15) // committed
	 * u(c/o/d/a3, 20) // prepared
	 * p(c/o/d/a1, 25) // committed - This should succeed because a1 is
	 *		      committed so tree is not empty
	 * p(c/o/d/a2, 30) // This should return -DER_INPROGRESS because a3 is
	 *		      not committed and is the only entry left
	 */

	/** First, a committed update to a1*/
	d_iov_set(&sgl.sg_iovs[0], (void *)first, rex.rx_nr);
	epoch++;
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/** Second, committed update to a2 */
	epoch++;
	iod.iod_name = akey2;
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_int_equal(rc, 0);

	/** Now prepared update to akey 3 */
	epoch++;
	iod.iod_name = akey3;
	vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth1);
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			       &iod, NULL, &sgl, dth1);
	assert_int_equal(rc, 0);
	xid1 = dth1->dth_xid;
	vts_dtx_end(dth1);

	/** Now committed punch to akey 1 */
	epoch++;
	iod.iod_name = akey1;
	vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth2);
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			   &akey1, dth2);
	assert_int_equal(rc, 0);
	xid2 = dth2->dth_xid;
	vts_dtx_end(dth2);
	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid2, 1, NULL);
	assert_int_equal(rc, 1);

	/** Now try to punch akey 2, should fail */
	epoch++;
	vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth2);
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			   &akey2, dth2);
	assert_int_equal(rc, -DER_INPROGRESS);

	/** Now commit the in progress punch and try again */
	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid1, 1, NULL);
	assert_int_equal(rc, 1);

	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			   &akey2, dth2);
	assert_int_equal(rc, 0);
	xid2 = dth2->dth_xid;
	vts_dtx_end(dth2);
	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid2, 1, NULL);
	assert_int_equal(rc, 1);

	memset(buf, 'x', sizeof(buf));
	rex.rx_idx = 0;
	rex.rx_nr = sizeof(buf);
	iod.iod_name = akey2;
	d_iov_set(&sgl.sg_iovs[0], (void *)buf, sizeof(buf));
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, oid, epoch + 2, 0, &dkey, 1,
			   &iod, &sgl);
	assert_int_equal(rc, 0);
	assert_memory_equal(buf, expected, strlen(expected));

	d_sgl_fini(&sgl, false);

	start_epoch = epoch + 1;
}

#define NR_OBJ 10
#define NR_DKEY 20
#define NR_AKEY 50
#define NR_TX 30
enum {
	TX_OP_PUNCH_OBJ,
	TX_OP_PUNCH_DKEY,
	TX_OP_COND_PUNCH_DKEY,
	TX_OP_PUNCH_AKEY,
	TX_OP_COND_PUNCH_AKEY,
	TX_OP_UPDATE_AKEY,
	TX_OP_UPDATE_DKEY,
	TX_OP_INSERT_AKEY,
	TX_OP_INSERT_DKEY,
	TX_OP_UPDATE1,
	TX_OP_UPDATE2,
	TX_OP_FETCH_AKEY,
	TX_OP_FETCH_DKEY,
	TX_OP_FETCH1,
	TX_OP_FETCH2,
	TX_OP_FETCH3,
	TX_NUM_OPS,
};

struct vos_ioreq {
	daos_handle_t		 coh;
	daos_unit_oid_t		 oid;
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	d_sg_list_t		*sgl;
	d_sg_list_t		*fetch_sgl;
	daos_key_t		*dkey;
	daos_key_t		*akey;
	daos_iod_t		*iod;
	uint64_t		 flags;
	int			 akey_nr;
	bool			 commit;
	bool			 success;
};

static void
do_punch(struct vos_ioreq *req)
{
	int	rc;

	rc = vos_obj_punch(req->coh, req->oid, 0, 0, req->flags, req->dkey,
			   req->akey_nr, req->akey, req->dth);
	if (rc == 0) {
		req->commit = true;
		req->success = true;
	}
}

static void
do_io(struct vos_ioreq *req, int op)
{
	int		rc;
	bool		fetch = false;

	switch (op) {
	case TX_OP_UPDATE_AKEY:
		req->flags = DAOS_COND_AKEY_UPDATE;
		break;
	case TX_OP_UPDATE_DKEY:
		req->flags = DAOS_COND_DKEY_UPDATE;
		break;
	case TX_OP_INSERT_AKEY:
		req->flags = DAOS_COND_AKEY_INSERT;
		break;
	case TX_OP_INSERT_DKEY:
		req->flags = DAOS_COND_DKEY_INSERT;
		break;
	case TX_OP_UPDATE1:
	case TX_OP_UPDATE2:
		break;
	case TX_OP_FETCH_AKEY:
		req->flags = DAOS_COND_AKEY_FETCH;
		fetch = true;
		break;
	case TX_OP_FETCH_DKEY:
		req->flags = DAOS_COND_DKEY_FETCH;
	case TX_OP_FETCH1:
	case TX_OP_FETCH2:
	case TX_OP_FETCH3:
		fetch = true;
		break;
	}

	if (!fetch) {
		rc = vos_obj_update_ex(req->coh, req->oid, 0, 0, req->flags,
				       req->dkey, req->akey_nr, req->iod, NULL,
				       req->sgl, req->dth);
		if (rc == 0) {
			req->commit = true;
			req->success = true;
		}
		return;
	}

	rc = vos_obj_fetch_ex(req->coh, req->oid, 0, req->flags, req->dkey,
			      req->akey_nr, req->iod, req->fetch_sgl, req->dth);

	if (rc == 0)
		req->success = true;
}

static void
many_tx(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	bool			done = false;
	daos_key_t		dkey[NR_DKEY];
	daos_key_t		akey[NR_AKEY];
	daos_iod_t		iod[NR_AKEY];
	d_sg_list_t		sgl;
	d_sg_list_t		fetch_sgl;
	char			buf[32];
	daos_epoch_t		epoch = start_epoch;
	daos_handle_t		coh;
	daos_epoch_range_t	epr = {epoch, epoch};
	struct vos_ioreq	req[NR_TX] = {0};
	const char		*first = "Hello";
	char			dkey_buf[NR_DKEY][UPDATE_DKEY_SIZE];
	char			akey_buf[NR_AKEY][UPDATE_AKEY_SIZE];
	daos_unit_oid_t		oid[NR_OBJ];
	uint64_t		flags;
	int			count, i, j, k, tx_num, cur_tx, old_tx;
	int			random = 0, op;
	int			total = 0, success = 0, writes = 0;
	int			nr_dkey = NR_DKEY;
	int			nr_akey = NR_AKEY;
	int			nr_obj = NR_OBJ;

	if (DAOS_ON_VALGRIND) {
		nr_dkey /= 5;
		nr_akey /= 5;
		nr_obj /= 5;
	}

	test_args_reset(arg, VPOOL_SIZE);
	coh = arg->ctx.tc_co_hdl;

	memset(&iod, 0, sizeof(iod));

	rc = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);
	rc = d_sgl_init(&fetch_sgl, 1);
	assert_int_equal(rc, 0);

	/* Set up dkey and akey */
	for (i = 0; i < nr_obj; i++)
		oid[i] = gen_oid(arg->ofeat);
	for (i = 0; i < nr_dkey; i++) {
		vts_key_gen(&dkey_buf[i][0], arg->dkey_size, true, arg);
		set_iov(&dkey[i], &dkey_buf[i][0],
			arg->ofeat & DAOS_OF_DKEY_UINT64);
	}
	for (i = 0; i < nr_akey; i++) {
		vts_key_gen(&akey_buf[i][0], arg->akey_size, true, arg);
		set_iov(&akey[i], &akey_buf[i][0],
			arg->ofeat & DAOS_OF_AKEY_UINT64);
		iod[i].iod_type = DAOS_IOD_SINGLE;
		iod[i].iod_size = strlen(first);
		iod[i].iod_name = akey[i];
		iod[i].iod_recxs = NULL;
		iod[i].iod_nr = 1;
	}
	d_iov_set(&sgl.sg_iovs[0], (void *)first, iod[0].iod_size);
	d_iov_set(&fetch_sgl.sg_iovs[0], (void *)buf, sizeof(buf));

	tx_num = 0;
start_over:
	srand(0);
	for (i = 0; i < nr_obj; i++) {
		for (j = 0; j < nr_dkey; j++) {
			for (k = 0; k < nr_akey; k++) {
				for (count = 0; count < 3; count++) {
					total++;
					switch (tx_num & 3) {
					case 0:
						epoch -= 3;
						break;
					case 1:
						epoch += 2;
						break;
					case 2:
						epoch -= 1;
						break;
					case 3:
						epoch += 10;
						break;
					}

					cur_tx = tx_num++ % NR_TX;
					old_tx = (cur_tx + 1) % NR_TX;
					vts_dtx_begin_ex(&oid[i], coh,
							 epoch, 0, 0, 1,
							 &req[cur_tx].dth);
					req[cur_tx].oid = oid[i];
					req[cur_tx].coh = coh;
					req[cur_tx].xid =
						req[cur_tx].dth->dth_xid;

					random = rand();
					flags = 0;
					op = random % TX_NUM_OPS;
					switch (op) {
					case TX_OP_PUNCH_OBJ:
						do_punch(&req[cur_tx]);
						break;
					case TX_OP_COND_PUNCH_DKEY:
						flags = DAOS_COND_PUNCH;
						req[cur_tx].flags = flags;
					case TX_OP_PUNCH_DKEY:
						req[cur_tx].dkey = &dkey[j];
						do_punch(&req[cur_tx]);
						break;
					case TX_OP_COND_PUNCH_AKEY:
						flags = DAOS_COND_PUNCH;
						req[cur_tx].flags = flags;
					case TX_OP_PUNCH_AKEY:
						req[cur_tx].dkey = &dkey[j];
						req[cur_tx].akey = &akey[k];
						req[cur_tx].akey_nr = 1;
						do_punch(&req[cur_tx]);
						break;
					default:
						req[cur_tx].dkey = &dkey[j];
						req[cur_tx].iod = &iod[k];
						req[cur_tx].sgl = &sgl;
						req[cur_tx].fetch_sgl =
							&fetch_sgl;
						req[cur_tx].akey_nr = 1;
						do_io(&req[cur_tx], op);
						break;
					}
					if (req[cur_tx].success)
						success++;
					if (req[cur_tx].commit)
						writes++;
					vts_dtx_end(req[cur_tx].dth);
					if (req[old_tx].commit) {
						rc = vos_dtx_commit(coh,
							    &req[old_tx].xid, 1,
							    NULL);
						assert_int_equal(rc, 1);
					}
					memset(&req[old_tx], 0, sizeof(req[0]));
				}
			}
		}
		if ((epoch - 200) < epr.epr_lo)
			continue;
		epr.epr_hi = epoch - 200;
		rc = vos_aggregate(coh, &epr, NULL, NULL, NULL);
		assert_int_equal(rc, 0);
	}
	for (i = 0; i < NR_TX - 1; i++) {
		old_tx = (tx_num++ + 1) % NR_TX;
		if (!req[old_tx].commit) {
			memset(&req[old_tx], 0, sizeof(req[0]));
			continue;
		}
		rc = vos_dtx_commit(coh, &req[old_tx].xid, 1, NULL);
		assert_int_equal(rc, 1);
		memset(&req[old_tx], 0, sizeof(req[0]));
	}

	for (i = 0; i < nr_obj; i++) {
		rc = vos_obj_delete(coh, oid[i]);
		assert_int_equal(rc, 0);
	}

	if (!done) {
		done = true;
		goto start_over;
	}

	printf("Total transactions %d, success %d, writes %d\n", total, success,
	       writes);

	d_sgl_fini(&sgl, false);
	d_sgl_fini(&fetch_sgl, false);
	start_epoch = epoch + 1;
}

static void
test_multiple_key_conditionals_common(void **state, bool with_dtx)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey;
	daos_key_t		akey1;
	daos_key_t		akey2;
	daos_recx_t		rex[2] = {0};
	daos_iod_t		iod[2] = {0};
	d_sg_list_t		sgl[2] = {0};
	daos_epoch_t		epoch = start_epoch;
	struct dtx_handle	*dth = NULL;
	struct dtx_id		 xid;
	const char		*expected = "xxxxx";
	const char		*first = "hello";
	const char		*second = "world";
	char			buf1[32] = {0};
	char			buf2[32] = {0};
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey1_buf[UPDATE_DKEY_SIZE];
	char			akey2_buf[UPDATE_DKEY_SIZE];
	daos_unit_oid_t		oid;

	test_args_reset(arg, VPOOL_SIZE);

	memset(rex, 0, sizeof(rex));
	memset(iod, 0, sizeof(iod));

	/* set up dkey and akey */
	oid = gen_oid(arg->ofeat);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey1_buf[0], arg->akey_size, false, arg);
	vts_key_gen(&akey2_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey1, &akey1_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);
	set_iov(&akey2, &akey2_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rc = d_sgl_init(&sgl[0], 1);
	assert_int_equal(rc, 0);
	rc = d_sgl_init(&sgl[1], 1);
	assert_int_equal(rc, 0);

	rex[0].rx_idx = 0;
	rex[0].rx_nr = strlen(first);
	rex[1].rx_idx = 0;
	rex[1].rx_nr = strlen(second);

	iod[0].iod_type = DAOS_IOD_ARRAY;
	iod[1].iod_flags = 0;
	iod[0].iod_size = 1;
	iod[0].iod_name = akey1;
	iod[0].iod_recxs = &rex[0];
	iod[0].iod_nr = 1;

	iod[1].iod_type = DAOS_IOD_ARRAY;
	iod[1].iod_flags = DAOS_COND_AKEY_UPDATE;
	iod[1].iod_size = 1;
	iod[1].iod_name = akey2;
	iod[1].iod_recxs = &rex[1];
	iod[1].iod_nr = 1;

	/** First, a committed update to a1*/
	d_iov_set(&sgl[0].sg_iovs[0], (void *)first, rex[0].rx_nr);
	d_iov_set(&sgl[1].sg_iovs[0], (void *)second, rex[1].rx_nr);
	epoch++;
	if (with_dtx)
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);

	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0,
			       DAOS_COND_PER_AKEY, &dkey, 2, iod, NULL, sgl,
			       dth);
	assert_int_equal(rc, -DER_NONEXIST);
	if (with_dtx)
		vts_dtx_end(dth);

	/** Now commit to akey1 only */
	epoch++;
	if (with_dtx) {
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
		xid = dth->dth_xid;
	}
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey, 1,
			       iod, NULL, sgl, dth);
	assert_int_equal(rc, 0);
	if (with_dtx) {
		vts_dtx_end(dth);
		rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}

	/** Try again, condition on akey 2 should fail */
	epoch++;
	if (with_dtx)
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0,
			       DAOS_COND_PER_AKEY, &dkey, 2, iod, NULL, sgl,
			       dth);
	assert_int_equal(rc, -DER_NONEXIST);
	if (with_dtx)
		vts_dtx_end(dth);

	/** Change condition to insert, should pass */
	epoch++;
	if (with_dtx) {
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
		xid = dth->dth_xid;
	}
	iod[1].iod_flags = DAOS_COND_AKEY_INSERT;
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0,
			       DAOS_COND_PER_AKEY, &dkey, 2, iod, NULL, sgl,
			       dth);
	assert_int_equal(rc, 0);
	if (with_dtx) {
		vts_dtx_end(dth);
		rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}

	/** Try update, should pass this time */
	epoch++;
	if (with_dtx) {
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
		xid = dth->dth_xid;
	}
	iod[1].iod_flags = DAOS_COND_AKEY_UPDATE;
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0,
			       DAOS_COND_PER_AKEY, &dkey, 2, iod, NULL, sgl,
			       dth);
	assert_int_equal(rc, 0);
	if (with_dtx) {
		vts_dtx_end(dth);
		rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}

	/** Conditional insert should fail */
	epoch++;
	if (with_dtx)
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
	iod[1].iod_flags = DAOS_COND_AKEY_INSERT;
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0,
			       DAOS_COND_PER_AKEY, &dkey, 2, iod, NULL, sgl,
			       dth);
	assert_int_equal(rc, -DER_EXIST);
	if (with_dtx)
		vts_dtx_end(dth);

	epoch++;
	if (with_dtx)
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
	memset(buf1, 0, sizeof(buf1));
	memset(buf2, 0, sizeof(buf2));
	iod[1].iod_flags = DAOS_COND_AKEY_FETCH;
	d_iov_set(&sgl[0].sg_iovs[0], (void *)buf1, rex[0].rx_nr);
	d_iov_set(&sgl[1].sg_iovs[0], (void *)buf2, rex[1].rx_nr);
	rc = vos_obj_fetch_ex(arg->ctx.tc_co_hdl, oid, epoch,
			      DAOS_COND_PER_AKEY, &dkey, 2, iod, sgl, dth);
	assert_int_equal(rc, 0);
	assert_memory_equal(buf1, first, rex[0].rx_nr);
	assert_memory_equal(buf2, second, rex[1].rx_nr);
	if (with_dtx)
		vts_dtx_end(dth);

	/** Punch akey2 */
	epoch++;
	if (with_dtx) {
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
		xid = dth->dth_xid;
	}
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0, 0, &dkey,
			   1, &akey2, dth);
	assert_int_equal(rc, 0);
	if (with_dtx) {
		vts_dtx_end(dth);
		rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
		assert_int_equal(rc, 1);
	}

	epoch++;
	if (with_dtx)
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
	memset(buf1, 'x', sizeof(buf1));
	memset(buf2, 'x', sizeof(buf2));
	iod[1].iod_flags = DAOS_COND_AKEY_FETCH;
	d_iov_set(&sgl[0].sg_iovs[0], (void *)buf1, rex[0].rx_nr);
	d_iov_set(&sgl[1].sg_iovs[0], (void *)buf2, rex[1].rx_nr);
	rc = vos_obj_fetch_ex(arg->ctx.tc_co_hdl, oid, epoch,
			      DAOS_COND_PER_AKEY, &dkey, 2, iod, sgl, dth);
	assert_int_equal(rc, -DER_NONEXIST);
	assert_memory_equal(buf1, expected, rex[0].rx_nr);
	assert_memory_equal(buf2, expected, rex[1].rx_nr);
	if (with_dtx)
		vts_dtx_end(dth);

	epoch++;
	if (with_dtx)
		vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch, 0, &dth);
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey1_buf[0], arg->akey_size, false, arg);
	vts_key_gen(&akey2_buf[0], arg->akey_size, false, arg);
	iod[0].iod_flags = DAOS_COND_AKEY_INSERT;
	iod[1].iod_flags = DAOS_COND_AKEY_UPDATE;
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oid, epoch, 0,
			       DAOS_COND_DKEY_INSERT | DAOS_COND_PER_AKEY,
			       &dkey, 2, iod, NULL, sgl, dth);
	assert_int_equal(rc, -DER_NONEXIST);
	if (with_dtx)
		vts_dtx_end(dth);

	start_epoch = epoch + 1;
	d_sgl_fini(&sgl[0], false);
	d_sgl_fini(&sgl[1], false);
}

static void
test_multiple_key_conditionals(void **state)
{
	test_multiple_key_conditionals_common(state, false);
}

static void
test_multiple_key_conditionals_tx(void **state)
{
	test_multiple_key_conditionals_common(state, true);
}

static const struct CMUnitTest punch_model_tests_pmdk[] = {
	{ "VOS860: Conditionals test", cond_test, NULL, NULL },
	{ "VOS861: Multiple oid cond test", multiple_oid_cond_test, NULL,
		NULL },
	{ "VOS862: Punch while other akey is inprogress",
		test_inprogress_parent_punch, NULL, NULL },
	{ "VOS863: Multikey conditionals",
		test_multiple_key_conditionals, NULL, NULL },
	{ "VOS864: Multikey conditionals with tx",
		test_multiple_key_conditionals_tx, NULL, NULL },
	{ "VOS865: Many transactions", many_tx, NULL, NULL },
};

static const struct CMUnitTest punch_model_tests_all[] = {
	{ "VOS800: VOS punch model array set/get size",
	  array_set_get_size, pm_setup, pm_teardown },
	{ "VOS801: VOS punch model array read/write/punch int32_t",
	  array_1, pm_setup, pm_teardown },
	{ "VOS802: VOS punch model array read/write/punch char",
	  array_2, pm_setup, pm_teardown },
	{ "VOS803: VOS punch model array read/write/punch int32_t static",
	  array_3, pm_setup, pm_teardown },
	{ "VOS804: VOS punch model array read/write/punch char static",
	  array_4, pm_setup, pm_teardown },
	{ "VOS805: Simple punch model test", punch_model_test, NULL, NULL},
	{ "VOS806: Multi update", simple_multi_update, NULL, NULL},
	{ "VOS807: Array Set/get size, write, punch",
	  array_size_write, pm_setup, pm_teardown },
	{ "VOS808: Object punch and fetch",
	  object_punch_and_fetch, NULL, NULL },
	{ "VOS809: SGL test", sgl_test, NULL, NULL },
	{ "VOS810: Small SGL test", small_sgl, NULL, NULL },
	{ "VOS811: Test vos_obj_array_remove", remove_test, NULL, NULL },
	{ "VOS812: Minor epoch punch sv", minor_epoch_punch_sv, NULL, NULL },
	{ "VOS813: Minor epoch punch array", minor_epoch_punch_array, NULL,
		NULL },
	{ "VOS814: Minor epoch punch rebuild", minor_epoch_punch_rebuild, NULL,
		NULL },
	{ "VOS815: Many keys in one tree", many_keys, NULL, NULL },
};

int
run_pm_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];
	int	rc;

	dts_create_config(test_name, "VOS Universal Punch Model tests %s", cfg);
	if (DAOS_ON_VALGRIND)
		buf_size = 100;

	rc = cmocka_run_group_tests_name(test_name, punch_model_tests_all,
					 setup_io, teardown_io);

	if (getenv("DAOS_IO_BYPASS"))
		return rc;

	dts_create_config(test_name, "VOS PMDK only Punch Model tests %s", cfg);

	rc += cmocka_run_group_tests_name(test_name, punch_model_tests_pmdk,
					  setup_io, teardown_io);

	return rc;
}
