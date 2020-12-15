/*
 * (C) Copyright 2020 Intel Corporation.
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
 * \file
 *
 * MVCC Tests
 *
 * These tests verify MVCC rules in src/vos/README.md. The code could use some
 * improvements:
 *
 *   - TODO: Begin and commit TXs explicitly in conflict_rw_exec_one and
 *           uncertainty_check_exec_one.
 *   - TODO: Move epochs from o_func parameter to tx_helpers.
 *   - TODO: Simplify mvcc_arg->i and the i, j parameters.
 *   - TODO: Collect ntotal, nskipped, and nfailed into a struct test_stat.
 */

#define D_LOGFAC DD_FAC(tests)

#include "vts_io.h"
#include "vts_array.h"

struct tx_helper {
	/** Current transaction handle */
	struct dtx_handle	*th_dth;
	/** Save the XID to cleanup related TX. */
	struct dtx_id		 th_saved_xid;
	/** Number of total ops in current tx */
	uint32_t		 th_nr_ops;
	/** Number of write ops in current tx */
	uint32_t		 th_nr_mods;
	/** Current op number */
	uint32_t		 th_op_seq;
	/** Upper bound of epoch uncertainty */
	daos_epoch_t		 th_epoch_bound;
	/** Whether to skip committing this TX */
	bool			 th_skip_commit;
};

struct mvcc_arg {
	/** used to generate different oids, etc. */
	int		i;
	/** Fail on failed test */
	bool		fail_fast;
	/** used to generate different epochs */
	daos_epoch_t	epoch;
};

enum type {
	T_R,	/* read */
	T_RTU,	/* read timestamp update */
	T_RW,	/* readwrite */
	T_W,	/* write */

	T_COUNT /* number of types */
};

enum level {
	L_C,	/* container */
	L_O,	/* object */
	L_D,	/* dkey */
	L_A,	/* akey */

	L_COUNT,	/* number of levels */
	L_NIL		/* not applicable */
};

enum read_type {
	R_R,	/* regular */
	R_E,	/* if empty */
	R_NE,	/* if nonempty */

	R_COUNT,	/* number of read types */
	R_NIL		/* not applicable */
};

enum write_type {
	W_NE,	/* become nonempty */
	W_E,	/* become empty */

	W_COUNT,	/* number of write types */
	W_NIL		/* not applicable */
};

typedef int (*op_func_t)(struct io_test_args *arg, struct tx_helper *txh,
			 char *path, daos_epoch_t epoch);

struct op {
	char		*o_name;
	enum type	 o_type;
	enum level	 o_rlevel;	/* for T_R || T_RW */
	enum level	 o_wlevel;	/* for T_W || T_RW */
	enum read_type	 o_rtype;	/* for T_R || T_RW */
	enum write_type	 o_wtype;	/* for T_W || T_RW */
	op_func_t	 o_func;
};

static bool
is_r(struct op *op)
{
	return op->o_type == T_R;
}

static bool
is_rtu(struct op *op)
{
	return op->o_type == T_RTU;
}

static bool
is_rw(struct op *op)
{
	return op->o_type == T_RW;
}

static bool
is_w(struct op *op)
{
	return op->o_type == T_W;
}

#define for_each_op(op) \
	for (op = &operations[0]; op->o_name != NULL; op++)

static bool
overlap(char *a, char *b)
{
	int	a_len = strlen(a);
	int	b_len = strlen(b);

	return (strncmp(a, b, min(a_len, b_len)) == 0);
}

/* template and path must be at least L_COUNT + 1 bytes. */
static void
set_path(struct op *op, char *template, char *path)
{
	enum level level;

	if (is_r(op) || is_rtu(op))
		level = op->o_rlevel;
	else if (is_rw(op))
		level = max(op->o_rlevel, op->o_wlevel);
	else
		level = op->o_wlevel;

	D_ASSERTF(level < L_COUNT, "%d\n", level);
	memcpy(path, template, level + 1);
	path[level + 1] = '\0';
}

static void
set_oid(int i, char *path, daos_unit_oid_t *oid)
{
	/*
	 * Only use the 64 bits in oid->id_pub.lo:
	 *
	 *   - Higher 56 bits are set to i.
	 *   - Lower 8 bits are set to the numeric value of path[L_O].
	 */
	oid->id_pub.hi = 0;
	D_ASSERT(L_O < strlen(path));
	oid->id_pub.lo = (i << 8) + path[L_O];
	daos_obj_generate_id(&oid->id_pub,
			     DAOS_OF_AKEY_UINT64 | DAOS_OF_DKEY_UINT64, 0, 0);
	oid->id_shard = 0;
	oid->id_pad_32 = 0;
}

static void
set_dkey(uint64_t i, char *path, daos_key_t *dkey)
{
	uint64_t key;

	D_ASSERT(L_D < strlen(path));
	key = (i << 32) + path[L_D];
	D_ASSERT(dkey->iov_buf_len >= sizeof(key));
	*(uint64_t *)dkey->iov_buf = key;
	dkey->iov_len = sizeof(key);
}

static void
set_akey(uint64_t i, char *path, daos_key_t *akey)
{
	uint64_t key = (i << 32) + path[L_A];

	D_ASSERT(L_A < strlen(path));
	key = (i << 32) + path[L_A];
	D_ASSERT(akey->iov_buf_len >= sizeof(key));
	*(uint64_t *)akey->iov_buf = key;
	akey->iov_len = sizeof(key);
}

static void
set_value(int i, char *path, d_iov_t *value)
{
	int rc;

	rc = snprintf(value->iov_buf, value->iov_buf_len, "%d-value-%s", i,
		      path);
	D_ASSERT(rc < value->iov_buf_len);
	value->iov_len = strlen(value->iov_buf) + 1;
}

static struct dtx_handle *
start_tx(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	 struct tx_helper *txh)
{
	struct dtx_handle	*dth;

	if (txh == NULL)
		return NULL;

	dth = txh->th_dth;

	if (dth == NULL) {
		vts_dtx_begin_ex(&oid, coh, epoch, txh->th_epoch_bound, 0,
				 txh->th_nr_mods, &dth);
		txh->th_dth = dth;
	}

	return dth;
}

static void
stop_tx(daos_handle_t coh, struct tx_helper *txh, bool success, bool write)
{
	struct dtx_handle	*dth;
	struct dtx_id		 xid;
	int			 err;

	if (txh == NULL)
		return;

	dth = txh->th_dth;

	if (write)
		dth->dth_op_seq++;

	if (txh->th_nr_ops == txh->th_op_seq) {
		xid = dth->dth_xid;
		vts_dtx_end(dth);
		if (txh->th_nr_mods != 0) {
			if (success && !txh->th_skip_commit) {
				err = vos_dtx_commit(coh, &xid, 1, NULL);
				assert(err >= 0);
			} else {
				if (!success)
					txh->th_skip_commit = false;
				daos_dti_copy(&txh->th_saved_xid, &xid);
			}
		}
	}

	txh->th_op_seq++;
}

static int
tx_fetch(daos_handle_t coh, struct tx_helper *txh, daos_unit_oid_t oid,
	 daos_epoch_t epoch, uint64_t flags, daos_key_t *dkey,
	 unsigned int iod_nr, daos_iod_t *iod, d_sg_list_t *sgl)
{
	struct dtx_handle	*dth;
	int			 rc;

	dth = start_tx(coh, oid, epoch, txh);

	rc = vos_obj_fetch_ex(coh, oid, epoch, flags, dkey, iod_nr, iod, sgl,
			      dth);

	stop_tx(coh, txh, rc == 0, false);

	return rc;
}

static int
fetch_with_flags(struct io_test_args *arg, struct tx_helper *txh, char *path,
		 daos_epoch_t epoch, uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;
	char		 dkey_buf[64];
	daos_key_t	 dkey = {dkey_buf, sizeof(dkey_buf), 0};
	char		 akey_buf[64];
	daos_key_t	 akey = {akey_buf, sizeof(akey_buf), 0};
	daos_iod_t	 iod;
	char		 value_buf[64] = {0};
	d_iov_t		 value = {value_buf, sizeof(value_buf), 0};
	d_sg_list_t	 sgl;
	daos_recx_t	 recx;

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);

	memset(&iod, 0, sizeof(iod));
	iod.iod_name = akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	recx.rx_idx = 0;
	recx.rx_nr = sizeof(value_buf);

	memset(&sgl, 0, sizeof(sgl));
	sgl.sg_nr = 1;
	sgl.sg_iovs = &value;

	return tx_fetch(arg->ctx.tc_co_hdl, txh, oid, epoch, flags, &dkey,
			1 /* iod_nr */, &iod, &sgl);
}

static int
fetch_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	daos_epoch_t epoch)
{
	return fetch_with_flags(arg, txh, path, epoch, 0 /* flags */);
}

static int
fetch_dne_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	return fetch_with_flags(arg, txh, path, epoch, DAOS_COND_DKEY_FETCH);
}

static int
fetch_ane_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	return fetch_with_flags(arg, txh, path, epoch, DAOS_COND_AKEY_FETCH);
}

static int
read_ts_o(struct io_test_args *arg, struct tx_helper *txh, char *path,
	  daos_epoch_t epoch, uint64_t flags, daos_key_t *dkey,
	  daos_iod_t *iod, unsigned int iod_nr)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;

	set_oid(mvcc_arg->i, path, &oid);

	return tx_fetch(arg->ctx.tc_co_hdl, txh, oid, epoch, flags, dkey,
			iod_nr, iod, NULL);
}

static int
read_ts_d(struct io_test_args *arg, struct tx_helper *txh, char *path,
	  daos_epoch_t epoch, uint64_t flags, daos_iod_t *iod,
	  unsigned int iod_nr)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	char		 dkey_buf[64];
	daos_key_t	 dkey = {dkey_buf, sizeof(dkey_buf), 0};

	set_dkey(mvcc_arg->i, path, &dkey);

	return read_ts_o(arg, txh, path, epoch, flags, &dkey, iod, iod_nr);
}

static int
read_ts_o_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	return read_ts_o(arg, txh, path, epoch, VOS_OF_FETCH_SET_TS_ONLY,
			 NULL, NULL, 0);
}

static int
read_ts_d_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	return read_ts_d(arg, txh, path, epoch, VOS_OF_FETCH_SET_TS_ONLY,
			 NULL, 0);
}

static int
read_ts_a_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_iod_t	 iod;
	char		 akey_buf[64];
	daos_key_t	 akey = {akey_buf, sizeof(akey_buf), 1};
	char		 value_buf[64] = {0};
	daos_recx_t	 recx;

	set_akey(mvcc_arg->i, path, &akey);
	memset(&iod, 0, sizeof(iod));
	iod.iod_name = akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	recx.rx_idx = 1;
	recx.rx_nr = sizeof(value_buf);

	return read_ts_d(arg, txh, path, epoch, VOS_OF_FETCH_SET_TS_ONLY,
			 &iod, iod.iod_nr);
}

static int
checkexisto_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	      daos_epoch_t epoch)
{
	return read_ts_o(arg, txh, path, epoch, VOS_OF_FETCH_CHECK_EXISTENCE,
			 NULL, NULL, 0);
}


static int
checkexistd_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	      daos_epoch_t epoch)
{
	return read_ts_d(arg, txh, path, epoch, VOS_OF_FETCH_CHECK_EXISTENCE,
			 NULL, 0);
}

static int
checkexista_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	      daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_iod_t	 iod;
	char		 akey_buf[64];
	daos_key_t	 akey = {akey_buf, sizeof(akey_buf), 1};
	char		 value_buf[64] = {0};
	daos_recx_t	 recx;

	set_akey(mvcc_arg->i, path, &akey);
	memset(&iod, 0, sizeof(iod));
	iod.iod_name = akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	recx.rx_idx = 1;
	recx.rx_nr = sizeof(value_buf);

	return read_ts_d(arg, txh, path, epoch, VOS_OF_FETCH_CHECK_EXISTENCE,
			 &iod, iod.iod_nr);
}

static int
tx_update(daos_handle_t coh, struct tx_helper *txh, daos_unit_oid_t oid,
	  daos_epoch_t epoch, uint64_t flags, daos_key_t *dkey,
	  unsigned int iod_nr, daos_iod_t *iod, d_sg_list_t *sgl)
{
	struct dtx_handle	*dth;
	int			 rc;

	dth = start_tx(coh, oid, epoch, txh);

	rc = vos_obj_update_ex(coh, oid, epoch, 0 /* pm_ver */, flags,
			       dkey, iod_nr, iod, NULL /* iods_csums */,
			       sgl, dth);

	stop_tx(coh, txh, rc == 0, true);

	return rc;
}

static int
update_with_flags(struct io_test_args *arg, struct tx_helper *txh, char *path,
		  daos_epoch_t epoch, uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;
	char		 dkey_buf[64];
	daos_key_t	 dkey = {dkey_buf, sizeof(dkey_buf), 0};
	char		 akey_buf[64];
	daos_key_t	 akey = {akey_buf, sizeof(akey_buf), 0};
	daos_iod_t	 iod;
	char		 value_buf[64];
	d_iov_t		 value = {value_buf, sizeof(value_buf), 0};
	d_sg_list_t	 sgl;
	daos_recx_t	 recx;

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);
	set_value(mvcc_arg->i, path, &value);

	memset(&iod, 0, sizeof(iod));
	iod.iod_name = akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	recx.rx_idx = 0;
	recx.rx_nr = value.iov_len;

	memset(&sgl, 0, sizeof(sgl));
	sgl.sg_nr = 1;
	sgl.sg_iovs = &value;

	return tx_update(arg->ctx.tc_co_hdl, txh, oid, epoch, flags, &dkey,
			 1 /* iod_nr */, &iod, &sgl);
}

static int
update_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	 daos_epoch_t epoch)
{
	return update_with_flags(arg, txh, path, epoch, 0 /* flags */);
}

static int
update_de_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	return update_with_flags(arg, txh, path, epoch,
				 VOS_OF_COND_DKEY_INSERT);
}

static int
update_dne_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	return update_with_flags(arg, txh, path, epoch,
				 VOS_OF_COND_DKEY_UPDATE);
}

static int
update_ae_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	return update_with_flags(arg, txh, path, epoch,
				 VOS_OF_COND_AKEY_INSERT);
}

static int
update_ane_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	return update_with_flags(arg, txh, path, epoch,
				 VOS_OF_COND_AKEY_UPDATE);
}

static int
tx_punch(daos_handle_t coh, struct tx_helper *txh, daos_unit_oid_t oid,
	 daos_epoch_t epoch, uint64_t flags, daos_key_t *dkey,
	 unsigned int akey_nr, daos_key_t *akeys)
{
	struct dtx_handle	*dth;
	int			 rc;

	dth = start_tx(coh, oid, epoch, txh);

	rc = vos_obj_punch(coh, oid, epoch, 0 /* pm_ver */, flags, dkey,
			   akey_nr, akeys, dth);

	stop_tx(coh, txh, rc == 0, true);

	return rc;
}

static int
puncho_with_flags(struct io_test_args *arg, struct tx_helper *txh, char *path,
		  daos_epoch_t epoch, uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;

	set_oid(mvcc_arg->i, path, &oid);

	return tx_punch(arg->ctx.tc_co_hdl, txh, oid, epoch, flags,
			NULL /* dkey */, 0 /* akey_nr */, NULL /* akeys */);
}

static int
puncho_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	 daos_epoch_t epoch)
{
	return puncho_with_flags(arg, txh, path, epoch, 0 /* flags */);
}

static int
puncho_one_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	return puncho_with_flags(arg, txh, path, epoch, VOS_OF_COND_PUNCH);
}

static int
punchd_with_flags(struct io_test_args *arg, struct tx_helper *txh, char *path,
		  daos_epoch_t epoch, uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;
	char		 dkey_buf[64];
	daos_key_t	 dkey = {dkey_buf, sizeof(dkey_buf), 0};

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);

	return tx_punch(arg->ctx.tc_co_hdl, txh, oid, epoch, flags, &dkey,
			0 /* akey_nr */, NULL /* akeys */);
}

static int
punchd_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	 daos_epoch_t epoch)
{
	return punchd_with_flags(arg, txh, path, epoch, 0 /* flags */);
}

static int
punchd_dne_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	return punchd_with_flags(arg, txh, path, epoch, VOS_OF_COND_PUNCH);
}

static int
puncha_with_flags(struct io_test_args *arg, struct tx_helper *txh, char *path,
		  daos_epoch_t epoch, uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);

	return tx_punch(arg->ctx.tc_co_hdl, txh, oid, epoch, flags, &dkey,
			1 /* akey_nr */, &akey);
}

static int
puncha_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	 daos_epoch_t epoch)
{
	return puncha_with_flags(arg, txh, path, epoch, 0 /* flags */);
}

static int
puncha_ane_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	return puncha_with_flags(arg, txh, path, epoch, VOS_OF_COND_PUNCH);
}

static int
simple_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	  vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	/** At some point perhaps we want to verify something sane but for
	 *  now this is just a noop
	 */
	return 0;
}

static int
tx_list(vos_iter_param_t *param, vos_iter_type_t type, struct tx_helper *txh)
{
	struct dtx_handle	*dth;
	struct vos_iter_anchors	 anchors = {0};
	int			 rc;

	dth = start_tx(param->ip_hdl, param->ip_oid, param->ip_epr.epr_hi, txh);

	rc = vos_iterate(param, type, false, &anchors, simple_cb, NULL, NULL,
			 dth);

	stop_tx(param->ip_hdl, txh, rc == 0, false);

	return rc;
}

static int
listo_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	daos_epoch_t epoch)
{
	struct mvcc_arg		*mvcc_arg = arg->custom;
	vos_iter_param_t	 param = {0};

	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_epr.epr_hi = epoch;
	/** We may need to figure out how to initialize the dtx without an oid
	 *  but for now, just use the one we have
	 */
	set_oid(mvcc_arg->i, ".o", &param.ip_oid);

	return tx_list(&param, VOS_ITER_OBJ, txh);
}

static int
listd_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	daos_epoch_t epoch)
{
	struct mvcc_arg		*mvcc_arg = arg->custom;
	vos_iter_param_t	 param = {0};

	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_epr.epr_hi = epoch;

	set_oid(mvcc_arg->i, path, &param.ip_oid);

	return tx_list(&param, VOS_ITER_DKEY, txh);
}

static int
lista_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	daos_epoch_t epoch)
{
	struct mvcc_arg		*mvcc_arg = arg->custom;
	vos_iter_param_t	 param = {0};
	uint64_t		 dkey_val;
	daos_key_t		 dkey = {&dkey_val, sizeof(dkey_val), 0};

	set_dkey(mvcc_arg->i, path, &dkey);
	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_epr.epr_hi = epoch;
	param.ip_dkey = dkey;
	set_oid(mvcc_arg->i, path, &param.ip_oid);

	return tx_list(&param, VOS_ITER_AKEY, txh);
}

static int
listr_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	daos_epoch_t epoch)
{
	struct mvcc_arg		*mvcc_arg = arg->custom;
	vos_iter_param_t	 param = {0};
	uint64_t		 dkey_val;
	daos_key_t		 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t		 akey_val;
	daos_key_t		 akey = {&akey_val, sizeof(akey_val), 0};

	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);
	param.ip_hdl = arg->ctx.tc_co_hdl;
	param.ip_epr.epr_hi = epoch;
	param.ip_dkey = dkey;
	param.ip_akey = akey;
	set_oid(mvcc_arg->i, path, &param.ip_oid);

	return tx_list(&param, VOS_ITER_RECX, txh);
}

static int
tx_query(daos_handle_t coh, struct tx_helper *txh, daos_epoch_t epoch,
	 daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
	 uint64_t flags, uint64_t i, char *path)
{
	struct dtx_handle	*dth;
	int			 rc;
	daos_unit_oid_t		 oid;

	set_oid(i, path, &oid);

	dth = start_tx(coh, oid, epoch, txh);

	rc = vos_obj_query_key(coh, oid, flags, epoch, dkey, akey, recx,
			       dth);

	stop_tx(coh, txh, rc == 0, false);

	return rc;
}

static int
querymaxd_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val = 0;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, NULL,
			NULL, DAOS_GET_DKEY | DAOS_GET_MAX, mvcc_arg->i, path);
}

static int
querymaxa_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val = 0;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	set_dkey(mvcc_arg->i, path, &dkey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, NULL,
			DAOS_GET_AKEY | DAOS_GET_MAX, mvcc_arg->i, path);
}

static int
querymaxr_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};
	daos_recx_t	 recx = {0};

	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_RECX | DAOS_GET_MAX, mvcc_arg->i, path);
}

static int
querymaxda_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, NULL,
			DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_MAX,
			mvcc_arg->i, path);
}

static int
querymaxdr_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	daos_recx_t	 recx = {0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	set_akey(mvcc_arg->i, "...a", &akey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_DKEY | DAOS_GET_RECX | DAOS_GET_MAX,
			mvcc_arg->i, path);
}

static int
querymaxar_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};
	daos_recx_t	 recx = {0};

	set_dkey(mvcc_arg->i, path, &dkey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX,
			mvcc_arg->i, path);
}

static int
querymaxdar_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	      daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};
	daos_recx_t	 recx = {0};

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_MAX |
			DAOS_GET_RECX, mvcc_arg->i, path);
}

static int
querymind_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val = 0;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, NULL, NULL,
			DAOS_GET_DKEY | DAOS_GET_MIN, mvcc_arg->i, path);
}

static int
querymina_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val = 0;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	set_dkey(mvcc_arg->i, path, &dkey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, NULL,
			DAOS_GET_AKEY | DAOS_GET_MIN, mvcc_arg->i, path);
}

static int
queryminr_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	    daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};
	daos_recx_t	 recx = {0};

	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_RECX | DAOS_GET_MIN, mvcc_arg->i, path);
}

static int
queryminda_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, NULL,
			DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_MIN,
			mvcc_arg->i, path);
}

static int
querymindr_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	daos_recx_t	 recx = {0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};

	set_akey(mvcc_arg->i, "...a", &akey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_DKEY | DAOS_GET_RECX | DAOS_GET_MIN,
			mvcc_arg->i, path);
}

static int
queryminar_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	     daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};
	daos_recx_t	 recx = {0};

	set_dkey(mvcc_arg->i, path, &dkey);

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MIN,
			mvcc_arg->i, path);
}

static int
querymindar_f(struct io_test_args *arg, struct tx_helper *txh, char *path,
	      daos_epoch_t epoch)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	uint64_t	 dkey_val;
	daos_key_t	 dkey = {&dkey_val, sizeof(dkey_val), 0};
	uint64_t	 akey_val;
	daos_key_t	 akey = {&akey_val, sizeof(akey_val), 0};
	daos_recx_t	 recx = {0};

	return tx_query(arg->ctx.tc_co_hdl, txh, epoch, &dkey, &akey, &recx,
			DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_MIN |
			DAOS_GET_RECX, mvcc_arg->i, path);
}

static struct op operations[] = {
	/* {name,	type,	rlevel,	wlevel,	rtype,	wtype,	func} */

	/* Reads */
	{"fetch",	T_R,	L_A,	L_NIL,	R_R,	W_NIL,	fetch_f},
	{"fetch_dne",	T_R,	L_A,	L_NIL,	R_NE,	W_NIL,	fetch_dne_f},
	{"fetch_ane",	T_R,	L_A,	L_NIL,	R_NE,	W_NIL,	fetch_ane_f},
	{"listo",	T_R,	L_C,	L_NIL,	R_R,	W_NIL,	listo_f},
	{"listd",	T_R,	L_O,	L_NIL,	R_R,	W_NIL,	listd_f},
	{"lista",	T_R,	L_D,	L_NIL,	R_R,	W_NIL,	lista_f},
	{"listr",	T_R,	L_A,	L_NIL,	R_R,	W_NIL,	listr_f},
	{"checkexisto",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	checkexisto_f},
	{"checkexistd",	T_R,	L_D,	L_NIL,	R_NE,	W_NIL,	checkexistd_f},
	{"checkexista",	T_R,	L_A,	L_NIL,	R_NE,	W_NIL,	checkexista_f},
	{"querymaxd",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymaxd_f},
	{"querymaxa",	T_R,	L_D,	L_NIL,	R_NE,	W_NIL,	querymaxa_f},
	{"querymaxr",	T_R,	L_A,	L_NIL,	R_NE,	W_NIL,	querymaxr_f},
	{"querymaxda",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymaxda_f},
	{"querymaxdr",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymaxdr_f},
	{"querymaxar",	T_R,	L_D,	L_NIL,	R_NE,	W_NIL,	querymaxar_f},
	{"querymaxdar",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymaxdar_f},
	{"querymind",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymind_f},
	{"querymina",	T_R,	L_D,	L_NIL,	R_NE,	W_NIL,	querymina_f},
	{"queryminr",	T_R,	L_A,	L_NIL,	R_NE,	W_NIL,	queryminr_f},
	{"queryminda",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	queryminda_f},
	{"querymindr",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymindr_f},
	{"queryminar",	T_R,	L_D,	L_NIL,	R_NE,	W_NIL,	queryminar_f},
	{"querymindar",	T_R,	L_O,	L_NIL,	R_NE,	W_NIL,	querymindar_f},
	/* Read timestamp updates */
	{"read_ts_o",	T_RTU,	L_O,	L_NIL,	R_R,	W_NIL,	read_ts_o_f},
	{"read_ts_d",	T_RTU,	L_D,	L_NIL,	R_R,	W_NIL,	read_ts_d_f},
	{"read_ts_a",	T_RTU,	L_A,	L_NIL,	R_R,	W_NIL,	read_ts_a_f},

	/*
	 * Readwrites
	 *
	 *   "_de"	means "if dkey empty"
	 *   "_dne"	means "if dkey nonempty"
	 *   "_ae"	means "if akey empty"
	 *   "_ane"	means "if akey nonempty"
	 *   "_one"	means "if object nonempty"
	 *   "_ane"	means "if akey nonempty"
	 */
	{"update_de",	T_RW,	L_D,	L_A,	R_E,	W_NE,	update_de_f},
	{"update_dne",	T_RW,	L_D,	L_A,	R_NE,	W_NE,	update_dne_f},
	{"update_ae",	T_RW,	L_A,	L_A,	R_E,	W_NE,	update_ae_f},
	{"update_ane",	T_RW,	L_A,	L_A,	R_NE,	W_NE,	update_ane_f},
	{"puncho_one",	T_RW,	L_O,	L_O,	R_NE,	W_E,	puncho_one_f},
	{"punchd_dne",	T_RW,	L_D,	L_D,	R_NE,	W_E,	punchd_dne_f},
	{"puncha_ane",	T_RW,	L_A,	L_A,	R_NE,	W_E,	puncha_ane_f},

	/*
	 * Writes
	 *
	 * Note that due to punch propagation, regular punches actually involve
	 * one or more reads. These reads can only be determined at run time.
	 * We are not verifying their side effects right now---failures caused
	 * by them are simply ignored.
	 */
	{"update",	T_W,	L_NIL,	L_A,	R_NIL,	W_NE,	update_f},
	{"puncho",	T_W,	L_NIL,	L_O,	R_NIL,	W_E,	puncho_f},
	{"punchd",	T_W,	L_NIL,	L_D,	R_NIL,	W_E,	punchd_f},
	{"puncha",	T_W,	L_NIL,	L_A,	R_NIL,	W_E,	puncha_f},

	/* Terminator */
	{NULL,		0,	0,	0,	0,	0,	NULL}
};

static bool
is_punch(struct op *op)
{
	char prefix[] = "punch";

	return (strncmp(op->o_name, prefix, strlen(prefix)) == 0);
}

/*
 * An excluded conflicting_rw case
 *
 * ec_we_minus_re:
 *
 *   we > re:	 1
 *   we = re:	 0
 *   we < re:	-1
 */
struct conflicting_rw_excluded_case {
	bool		 ec_empty;
	char		*ec_r;
	char		*ec_rp;
	char		*ec_w;
	char		*ec_wp;
	int64_t		 ec_we_minus_re;
	bool		 ec_same_tx;
};

static struct conflicting_rw_excluded_case conflicting_rw_excluded_cases[] = {
	/** Used to disable specific tests as necessary */
	/** These specific tests can be enabled when DAOS-4698 is fixed
	 *  and the line in vos_obj.c that references this ticket is
	 *  uncommented.
	 */
	{false,	"punchd_dne",	"cod",	"puncho_one",	"co",	0, false},
	{false,	"punchd_dne",	"cod",	"puncho_one",	"co",	1, false},
	{false,	"puncha_ane",	"coda",	"puncho_one",	"co",	0, false},
	{false,	"puncha_ane",	"coda",	"puncho_one",	"co",	1, false},
	{false, "puncha_ane",   "coda", "puncho_one",   "co",   0, true},
	{false, "punchd_dne",   "cod",  "puncho_one",   "co",   0, true},
};

static int64_t
we_minus_re(daos_epoch_t we, daos_epoch_t re)
{
	if (we > re)
		return 1;
	else if (we == re)
		return 0;
	else
		return -1;
}

static bool
conflicting_rw_is_excluded(bool empty, struct op *r, char *rp, daos_epoch_t re,
			   struct op *w, char *wp, daos_epoch_t we,
			   bool same_tx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(conflicting_rw_excluded_cases); i++) {
		struct conflicting_rw_excluded_case *c;

		c = &conflicting_rw_excluded_cases[i];
		if (c->ec_empty == empty && strcmp(c->ec_r, r->o_name) == 0 &&
		    strcmp(c->ec_rp, rp) == 0 &&
		    strcmp(c->ec_w, w->o_name) == 0 &&
		    strcmp(c->ec_wp, wp) == 0 &&
		    c->ec_we_minus_re == we_minus_re(we, re) &&
		    c->ec_same_tx == same_tx)
			return true;
	}

	return false;
}

/* Return the number of failures observed. */
static int
conflicting_rw_exec_one(struct io_test_args *arg, int i, int j, bool empty,
			struct op *r, char *rp, daos_epoch_t re,
			struct op *w, char *wp, daos_epoch_t we, bool same_tx,
			int *skipped)
{
	struct mvcc_arg		*mvcc_arg = arg->custom;
	struct tx_helper	*rtx;
	struct tx_helper	*wtx;
	struct tx_helper	 txh1 = {0};
	struct tx_helper	 txh2 = {0};
	int			 expected_rrc = 0;
	int			 expected_wrc = 0;
	int			 nfailed = 0;
	int			 rc;

	if (conflicting_rw_is_excluded(empty, r, rp, re, w, wp, we, same_tx)) {
		(*skipped)++;
		goto out;
	}

	/*
	 * Figure out the expected read result, perform read, and verify the
	 * result.
	 */
	if (r->o_rtype == R_E && !empty)
		expected_rrc = -DER_EXIST;
	else if (r->o_rtype == R_NE && empty)
		expected_rrc = -DER_NONEXIST;

	if (same_tx && expected_rrc != 0) {
		/** Not a valid use case as conditional updates are split in the
		 *  context of distributed transactions.  The conditional fetch
		 *  would mean either the update doesn't execute or should abort
		 *  the transaction if it returns -DER_EXIST
		 */
		goto out;
	}

	print_message("CASE %d.%d: %s, %s(%s, "DF_X64"), %s(%s, "DF_X64"), "
		      "%s TX [%d]\n", i, j, empty ? "empty" : "nonempty",
		      r->o_name, rp, re, w->o_name, wp, we,
		      same_tx ? "same" : "diff", mvcc_arg->i);

	if (same_tx) {
		rtx = wtx = &txh1;
		txh1.th_nr_ops = 2;
		txh1.th_op_seq = 1;
		if (is_rw(r))
			txh1.th_nr_mods = 2;
		else
			txh1.th_nr_mods = 1;
	} else {
		rtx = &txh1;
		wtx = &txh2;
		txh1.th_nr_ops = txh2.th_nr_ops = 1;
		txh1.th_op_seq = txh2.th_op_seq = 1;
		txh2.th_nr_mods = 1;
		if (is_rw(r))
			txh1.th_nr_mods = 1;
	}

	/* If requested, prepare the data that will be read. */
	if (!empty) {
		char	pp[L_COUNT + 1] = "coda";

		memcpy(pp, rp, strlen(rp));
		print_message("  update(%s, "DF_X64") before %s(%s, "
			      DF_X64"): ", pp, re - 1, r->o_name, rp, re);
		rc = update_f(arg, NULL /* txh */, pp, re - 1);
		print_message("%s\n", d_errstr(rc));
		if (rc != 0) {
			nfailed++;
			goto out;
		}
	}

	print_message("  %s(%s, "DF_X64") (expect %s): ",
		      r->o_name, rp, re, d_errstr(expected_rrc));
	rc = r->o_func(arg, rtx, rp, re);
	print_message("%s\n", d_errstr(rc));
	if (rc != expected_rrc) {
		nfailed++;
		goto out;
	}

	/*
	 * Figure out the expected readwrite or write result, perform the
	 * readwrite or write, and verify the result.
	 */
	if (re > we || (re == we && !same_tx))
		expected_wrc = -DER_TX_RESTART;
	if (is_rw(w)) {
		bool e;

		/*
		 * What shall be w's read result? This shall override its write
		 * result.
		 */
		if (re > we) {
			e = true;
		} else {
			e = empty;
			if (expected_rrc == 0 && is_rw(r))
				e = (r->o_wtype == W_E);
		}
		if (w->o_rtype == R_E && !e)
			expected_wrc = -DER_EXIST;
		else if (w->o_rtype == R_NE && e)
			expected_wrc = -DER_NONEXIST;
	}
	print_message("  %s(%s, "DF_X64") (expect %s): ",
		      w->o_name, wp, we, d_errstr(expected_wrc));
	rc = w->o_func(arg, wtx, wp, we);
	print_message("%s\n", d_errstr(rc));
	if (rc != expected_wrc)
		nfailed++;

out:
	if (nfailed > 0)
		print_message("FAILED: CASE %d.%d: %s, %s(%s, "DF_X64
			      "), %s(%s, "DF_X64"), %s TX [%d]\n", i, j,
			      empty ? "empty" : "nonempty", r->o_name, rp, re,
			      w->o_name, wp, we, same_tx ? "same" : "diff",
			      mvcc_arg->i);
	return nfailed;
}

/* Return the number of failures observed. */
static int
conflicting_rw_exec(struct io_test_args *arg, int i, struct op *r, struct op *w,
		    int *cases, int *skipped)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_epoch_t	 re;			/* r epoch */
	daos_epoch_t	 we;			/* w epoch */
	char		 rp[L_COUNT + 1] = {0};	/* r path */
	char		 wp[L_COUNT + 1] = {0};	/* w path */
	char		*path_template = "coda";
	bool		 emptiness[] = {true, false};
	int		 j = 0;
	int		 k;
	int		 nfailed = 0;

	/* Set overlapping paths. */
	set_path(r, path_template, rp);
	set_path(w, path_template, wp);
	D_ASSERTF(overlap(rp, wp), "overlap(\"%s\", \"%s\")", rp, wp);

	for (k = 0; k < ARRAY_SIZE(emptiness); k++) {
		bool empty = emptiness[k];

		/* Read, then write. re > we. */
		re = mvcc_arg->epoch + 10;
		we = mvcc_arg->epoch;
		nfailed += conflicting_rw_exec_one(arg, i, j, empty, r, rp,
						   re, w, wp, we,
						   false /* same_tx */,
						   skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/* Read, then write. re == we. Diff TX. */
		re = mvcc_arg->epoch;
		we = mvcc_arg->epoch;
		nfailed += conflicting_rw_exec_one(arg, i, j, empty, r, rp,
						   re, w, wp, we,
						   false /* same_tx */,
						   skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/* Read, then write. re == we. Same TX. */
		re = mvcc_arg->epoch;
		we = mvcc_arg->epoch;
		nfailed += conflicting_rw_exec_one(arg, i, j, empty, r, rp,
						   re, w, wp, we,
						   true /* same_tx */,
						   skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/* Read, then write. re < we. Shall write OK. */
		re = mvcc_arg->epoch;
		we = mvcc_arg->epoch + 10;
		nfailed += conflicting_rw_exec_one(arg, i, j, empty, r, rp,
						   re, w, wp, we,
						   false /* same_tx */,
						   skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;
	}

	return nfailed;
}

/* Verify that a read causes a conflicting write to be rejected. */
static void
conflicting_rw(void **state)
{
	struct io_test_args	*arg = *state;
	struct mvcc_arg		*mvcc_arg = arg->custom;
	struct op		*r;
	struct op		*w;
	int			 i = 0;
	int			 nfailed = 0;
	int			 nskipped = 0;
	int			 ntot = 0;

	/* For each read, read timestamp update, or readwrite... */
	for_each_op(r) {
		if (!(is_r(r) || is_rtu(r) || is_rw(r)))
			continue;

		/* For each readwrite or write... */
		for_each_op(w) {
			if (!(is_rw(w) || is_w(w)))
				continue;

			nfailed += conflicting_rw_exec(arg, i, r, w, &ntot,
						       &nskipped);
			assert_true(!mvcc_arg->fail_fast || nfailed == 0);
			i++;
		}
	}

	print_message("total tests: %d, skipped %d\n", ntot, nskipped);

	if (nfailed > 0)
		fail_msg("%d failed cases", nfailed);
}

/* Return the number of failures observed. */
static int
uncertainty_check_exec_one(struct io_test_args *arg, int i, int j, bool empty,
			   struct op *w, char *wp, daos_epoch_t we,
			   struct op *a, char *ap, daos_epoch_t ae,
			   daos_epoch_t bound, bool commit, int *skipped)
{
	struct mvcc_arg		*mvcc_arg = arg->custom;
	struct tx_helper	 txh1 = {0};
	struct tx_helper	 txh2 = {0};
	struct tx_helper	*wtx = &txh1;
	struct tx_helper	*atx = &txh2;
	int			 expected_arc = 0;
	int			 nfailed = 0;
	int			 rc;

#define DF_CASE								\
	"CASE %d.%d: %s, %s(%s, "DF_X64"), %s, %s(%s, "DF_X64		\
	") with bound "DF_X64" [%d]"
#define DP_CASE(i, j, empty, w, wp, we, commit, a, ap, ae, bound, argi)	\
	i, j, empty ? "empty" : "nonempty", w->o_name, wp, we,		\
	commit ? "commit" : "do not commit", a->o_name, ap, ae, bound,	\
	argi

	print_message(DF_CASE"\n",
		      DP_CASE(i, j, empty, w, wp, we, commit, a, ap, ae, bound,
			      mvcc_arg->i));

	/* If requested, prepare the data that will be overwritten by w. */
	if (!empty) {
		char		pp[L_COUNT + 1] = "coda";
		daos_epoch_t	pe = ae - 1;

		D_ASSERT(strlen(wp) <= sizeof(pp) - 1);
		memcpy(pp, wp, strlen(wp));
		print_message("  update(%s, "DF_U64") (expect DER_SUCCESS): ",
			      pp, pe);
		rc = update_f(arg, NULL /* txh */, pp, pe);
		print_message("%s\n", d_errstr(rc));
		if (rc != 0) {
			nfailed++;
			goto out;
		}
	}

	wtx->th_nr_ops = 1;
	wtx->th_op_seq = 1;
	wtx->th_nr_mods = 1;
	if (!commit)
		wtx->th_skip_commit = true;
	atx->th_nr_ops = 1;
	atx->th_op_seq = 1;
	if (is_rw(a) || is_w(a))
		atx->th_nr_mods = 1;
	atx->th_epoch_bound = bound;

	/* Perform w. */
	print_message("  %s(%s, "DF_X64") (expect DER_SUCCESS): ", w->o_name,
		      wp, we);
	rc = w->o_func(arg, wtx, wp, we);
	print_message("%s\n", d_errstr(rc));
	if (rc != 0) {
		nfailed++;
		goto out;
	}

	/*
	 * Perform a. If is_punch(w), a may be rejected due to w's read
	 * timestamp record.
	 */
	if (we <= bound) {
		expected_arc = -DER_TX_RESTART;
	} else {
		if ((is_r(a) || is_rw(a))) {
			if (a->o_rtype == R_NE && empty)
				expected_arc = -DER_NONEXIST;
			else if (a->o_rtype == R_E && !empty)
				expected_arc = -DER_EXIST;
		}
	}
	if (is_punch(w) && we > bound)
		print_message("  %s(%s, "DF_X64
			      ") (expect %s or DER_TX_RESTART): ", a->o_name,
			      ap, ae, d_errstr(expected_arc));
	else
		print_message("  %s(%s, "DF_X64") (expect %s): ",
			      a->o_name, ap, ae, d_errstr(expected_arc));
	rc = a->o_func(arg, atx, ap, ae);
	print_message("%s\n", d_errstr(rc));
	if (rc != expected_arc) {
		if (is_punch(w) && we > bound && rc == -DER_TX_RESTART)
			goto out;
		nfailed++;
	}

out:
	if (nfailed > 0)
		print_message("FAILED: "DF_CASE"\n",
			      DP_CASE(i, j, empty, w, wp, we, commit, a, ap, ae,
				      bound, mvcc_arg->i));

	if (!daos_is_zero_dti(&wtx->th_saved_xid)) {
		if (wtx->th_skip_commit)
			vos_dtx_commit(arg->ctx.tc_co_hdl, &wtx->th_saved_xid,
				       1, NULL);
		else
			vos_dtx_abort(arg->ctx.tc_co_hdl, DAOS_EPOCH_MAX,
				      &wtx->th_saved_xid, 1);
	}

	if (!daos_is_zero_dti(&atx->th_saved_xid)) {
		if (atx->th_skip_commit)
			vos_dtx_commit(arg->ctx.tc_co_hdl, &atx->th_saved_xid,
				       1, NULL);
		else
			vos_dtx_abort(arg->ctx.tc_co_hdl, DAOS_EPOCH_MAX,
				      &atx->th_saved_xid, 1);
	}

#undef DP_CASE
#undef DF_CASE
	return nfailed;
}

/* Return the number of failures observed. */
static int
uncertainty_check_exec(struct io_test_args *arg, int i, struct op *w,
		       struct op *a, int *cases, int *skipped)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_epoch_t	 we;			/* w epoch */
	daos_epoch_t	 ae;			/* a epoch */
	daos_epoch_t	 bound;			/* upper bound */
	char		 wp[L_COUNT + 1];	/* w path */
	char		 ap[L_COUNT + 1];	/* a path */
	char		*path_template = "coda";
	bool		 emptiness[] = {true, false};
	int		 j = 0;
	int		 k;
	int		 nfailed = 0;

	/* Set overlapping paths. */
	set_path(w, path_template, wp);
	set_path(a, path_template, ap);
	D_ASSERTF(overlap(wp, ap), "overlap(\"%s\", \"%s\")", wp, ap);

	for (k = 0; k < ARRAY_SIZE(emptiness); k++) {
		bool empty = emptiness[k];

		/*
		 * Write at the uncertainty upper bound, commit, then do
		 * operation a.
		 */
		bound = mvcc_arg->epoch + 10;
		we = bound;
		ae = mvcc_arg->epoch;
		nfailed += uncertainty_check_exec_one(arg, i, j, empty, w, wp,
						      we, a, ap, ae, bound,
						      true /* commit */,
						      skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/*
		 * Write at the uncertainty upper bound, do not commit, then do
		 * operation a.
		 */
		bound = mvcc_arg->epoch + 10;
		we = bound;
		ae = mvcc_arg->epoch;
		nfailed += uncertainty_check_exec_one(arg, i, j, empty, w, wp,
						      we, a, ap, ae, bound,
						      false /* commit */,
						      skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/*
		 * Write above the uncertainty upper bound, commit, then do
		 * operation a.
		 */
		bound = mvcc_arg->epoch + 10;
		we = bound + 1;
		ae = mvcc_arg->epoch;
		nfailed += uncertainty_check_exec_one(arg, i, j, empty, w, wp,
						      we, a, ap, ae, bound,
						      true /* commit */,
						      skipped);
		(*cases)++;
		j++;
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;
	}

	return nfailed;
}

static void
uncertainty_check(void **state)
{
	struct io_test_args	*arg = *state;
	struct mvcc_arg		*mvcc_arg = arg->custom;
	struct op		*w;
	struct op		*a;
	int			 i = 0;
	int			 nfailed = 0;
	int			 nskipped = 0;
	int			 ntot = 0;

	/* For each write... */
	for_each_op(w) {
		if (!is_w(w))
			continue;

		/*
		 * For any operation that isn't a read timestamp update...
		 * (Read timestamp updates shall not perform epoch uncertainty
		 * checks.)
		 */
		for_each_op(a) {
			if (is_rtu(a))
				continue;

			nfailed += uncertainty_check_exec(arg, i, w, a, &ntot,
							  &nskipped);
			assert_true(!mvcc_arg->fail_fast || nfailed == 0);
			i++;
		}
	}

	print_message("total tests: %d, failed %d, skipped %d\n", ntot,
		      nfailed, nskipped);

	if (nfailed > 0)
		fail_msg("%d failed cases", nfailed);
}

static const struct CMUnitTest mvcc_tests[] = {
	{ "VOS900: Conflicting read and write",
	  conflicting_rw, NULL, NULL },
	{ "VOS901: Epoch uncertainty checks",
	  uncertainty_check, NULL, NULL },
};

static int
setup_mvcc(void **state)
{
	struct io_test_args	*arg;
	struct mvcc_arg		*mvcc_arg;
	int			 rc;

	rc = setup_io(state);
	if (rc != 0)
		return rc;

	arg = *state;
	D_ASSERT(arg->custom == NULL);
	D_ALLOC_PTR(mvcc_arg);
	D_ASSERT(mvcc_arg != NULL);
	mvcc_arg->epoch = 500;
	d_getenv_bool("CMOCKA_TEST_ABORT", &mvcc_arg->fail_fast);
	arg->custom = mvcc_arg;
	return 0;
}

static int
teardown_mvcc(void **state)
{
	struct io_test_args	*arg = *state;

	D_ASSERT(arg->custom != NULL);
	D_FREE(arg->custom);
	arg->custom = NULL;

	return teardown_io(state);
}

int
run_mvcc_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "VOS MVCC Tests %s", cfg);

	if (getenv("DAOS_IO_BYPASS")) {
		print_message("Skipping MVCC tests: DAOS_IO_BYPASS is set\n");
		return 0;
	}

	return cmocka_run_group_tests_name(test_name, mvcc_tests,
					   setup_mvcc, teardown_mvcc);
}
