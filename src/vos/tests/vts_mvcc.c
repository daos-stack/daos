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
 * These tests verify MVCC conflict detections.
 */

#define D_LOGFAC DD_FAC(tests)

#include "vts_io.h"
#include "vts_array.h"

struct mvcc_arg {
	int		i;	/* used to generate different oids, etc. */
	daos_epoch_t	epoch;	/* used to generate different epochs */
};

enum type {
	T_R,	/* read */
	T_RW,	/* readwrite */
	T_W,	/* write */
	T_COUNT
};

enum level {
	L_C,	/* container */
	L_O,	/* object */
	L_D,	/* dkey */
	L_A,	/* akey */
	L_COUNT
};

typedef int (*op_func_t)(struct io_test_args *arg, char *path,
			 daos_epoch_t epoch);

struct op {
	char		*o_name;
	enum type	 o_type;
	enum level	 o_rlevel;	/* for T_R || T_RW */
	enum level	 o_wlevel;	/* for T_W || T_RW */
	op_func_t	 o_func;
};

static bool
is_r(struct op *op)
{
	return op->o_type == T_R;
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

	if (op->o_type == T_R)
		level = op->o_rlevel;
	else if (op->o_type == T_RW)
		level = max(op->o_rlevel, op->o_wlevel);
	else
		level = op->o_wlevel;

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
	oid->id_pub.lo = (i << 8) + path[L_O];
	daos_obj_generate_id(&oid->id_pub, 0, 0, 0);
	oid->id_shard = 0;
	oid->id_pad_32 = 0;
}

static void
set_dkey(int i, char *path, daos_key_t *dkey)
{
	int rc;

	rc = snprintf(dkey->iov_buf, dkey->iov_buf_len, "%d-dkey-%c", i,
		      path[L_D]);
	D_ASSERT(rc < dkey->iov_buf_len);
	dkey->iov_len = strlen(dkey->iov_buf) + 1;
}

static void
set_akey(int i, char *path, daos_key_t *akey)
{
	int rc;

	rc = snprintf(akey->iov_buf, akey->iov_buf_len, "%d-akey-%c", i,
		      path[L_A]);
	D_ASSERT(rc < akey->iov_buf_len);
	akey->iov_len = strlen(akey->iov_buf) + 1;
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

static int
update_with_flags(struct io_test_args *arg, char *path, daos_epoch_t epoch,
		  uint64_t flags)
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

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);
	set_value(mvcc_arg->i, path, &value);

	memset(&iod, 0, sizeof(iod));
	iod.iod_name = akey;
	iod.iod_type = DAOS_IOD_SINGLE;
	iod.iod_size = value.iov_len;
	iod.iod_nr = 1;

	memset(&sgl, 0, sizeof(sgl));
	sgl.sg_nr = 1;
	sgl.sg_iovs = &value;

	flags |= VOS_OF_USE_TIMESTAMPS;

	return vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch, 0 /* pm_ver */,
			      flags, &dkey, 1 /* iod_nr */, &iod, NULL, &sgl);
}

static int
update_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return update_with_flags(arg, path, epoch, 0 /* flags */);
}

static int
update_de_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return update_with_flags(arg, path, epoch, VOS_OF_COND_DKEY_INSERT);
}

static int
update_dne_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return update_with_flags(arg, path, epoch, VOS_OF_COND_DKEY_UPDATE);
}

static int
update_ae_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return update_with_flags(arg, path, epoch, VOS_OF_COND_AKEY_INSERT);
}

static int
update_ane_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return update_with_flags(arg, path, epoch, VOS_OF_COND_AKEY_UPDATE);
}

static int
puncho_with_flags(struct io_test_args *arg, char *path, daos_epoch_t epoch,
		  uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;

	set_oid(mvcc_arg->i, path, &oid);

	flags |= VOS_OF_USE_TIMESTAMPS;

	return vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0 /* pm_ver */,
			     flags, NULL /* dkey */, 0 /* akey_nr */,
			     NULL /* akeys */, NULL /* dth */);
}

static int
puncho_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return puncho_with_flags(arg, path, epoch, 0 /* flags */);
}

static int
puncho_one_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return puncho_with_flags(arg, path, epoch, VOS_OF_COND_PUNCH);
}

static int
punchd_with_flags(struct io_test_args *arg, char *path, daos_epoch_t epoch,
		  uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;
	char		 dkey_buf[64];
	daos_key_t	 dkey = {dkey_buf, sizeof(dkey_buf), 0};

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);

	flags |= VOS_OF_USE_TIMESTAMPS;

	return vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0 /* pm_ver */,
			     flags, &dkey, 0 /* akey_nr */, NULL /* akeys */,
			     NULL /* dth */);
}

static int
punchd_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return punchd_with_flags(arg, path, epoch, 0 /* flags */);
}

static int
punchd_dne_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return punchd_with_flags(arg, path, epoch, VOS_OF_COND_PUNCH);
}

static int
puncha_with_flags(struct io_test_args *arg, char *path, daos_epoch_t epoch,
		  uint64_t flags)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_unit_oid_t	 oid;
	char		 dkey_buf[64];
	daos_key_t	 dkey = {dkey_buf, sizeof(dkey_buf), 0};
	char		 akey_buf[64];
	daos_key_t	 akey = {akey_buf, sizeof(akey_buf), 0};

	set_oid(mvcc_arg->i, path, &oid);
	set_dkey(mvcc_arg->i, path, &dkey);
	set_akey(mvcc_arg->i, path, &akey);

	flags |= VOS_OF_USE_TIMESTAMPS;

	return vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0 /* pm_ver */,
			     flags, &dkey, 1 /* akey_nr */, &akey,
			     NULL /* dth */);
}

static int
puncha_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return puncha_with_flags(arg, path, epoch, 0 /* flags */);
}

static int
puncha_ane_func(struct io_test_args *arg, char *path, daos_epoch_t epoch)
{
	return puncha_with_flags(arg, path, epoch, VOS_OF_COND_PUNCH);
}

static struct op operations[] = {
	/* {name,	type,	rlevel,	wlevel,	func} */

	/* Reads */
	{"fetch",	T_R,	L_A,	0,	NULL /* TODO */},
	{"listc",	T_R,	L_C,	0,	NULL /* TODO */},
	{"listo",	T_R,	L_O,	0,	NULL /* TODO */},
	{"listd",	T_R,	L_D,	0,	NULL /* TODO */},
	{"queryc",	T_R,	L_C,	0,	NULL /* TODO */},
	{"queryo",	T_R,	L_O,	0,	NULL /* TODO */},
	{"queryd",	T_R,	L_D,	0,	NULL /* TODO */},

	/*
	 * Readwrites
	 *
	 *   "_one"	means "if object nonempty"
	 *   "_de"	means "if dkey empty"
	 *   "_dne"	means "if dkey nonempty"
	 *   "_ae"	means "if akey empty"
	 *   "_ane"	means "if akey nonempty"
	 */
	{"update_de",	T_RW,	L_D,	L_A,	update_de_func},
	{"update_dne",	T_RW,	L_D,	L_A,	update_dne_func},
	{"update_ae",	T_RW,	L_A,	L_A,	update_ae_func},
	{"update_ane",	T_RW,	L_A,	L_A,	update_ane_func},
	{"puncho_one",	T_RW,	L_O,	L_O,	puncho_one_func},
	{"punchd_dne",	T_RW,	L_D,	L_D,	punchd_dne_func},
	{"puncha_ane",	T_RW,	L_A,	L_A,	puncha_ane_func},

	/* Writes */
	{"update",	T_W,	0,	L_A,	update_func},
	{"puncho",	T_W,	0,	L_O,	puncho_func},
	{"punchd",	T_W,	0,	L_D,	punchd_func},
	{"puncha",	T_W,	0,	L_A,	puncha_func},

	/* Terminator */
	{NULL,		0,	0,	0,	NULL}
};

static void
write_before_read(struct io_test_args *arg, struct op *r, char *rp,
		  daos_epoch_t epoch)
{
	char	wp[L_COUNT + 1] = "coda";
	int	rc;

	memcpy(wp, rp, strlen(rp));
	print_message("Going to update(%s, "DF_U64") before %s(%s, "DF_U64")\n",
		      rp, epoch, r->o_name, rp, epoch);
	rc = update_func(arg, rp, epoch);
	assert_int_equal(rc, 0);
}

static void
conflicting_rw_exec_one(struct io_test_args *arg, int i, int j, bool nonempty,
			struct op *r, char *rp, daos_epoch_t re,
			struct op *w, char *wp, daos_epoch_t we,
			int expected_wrc)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	int		 rc;

	print_message("CASE %d.%d: %s, %s(%s, "DF_U64"), %s(%s, "DF_U64") "
		      "[%d]\n", i, j, nonempty ? "nonempty" : "emtpy",
		      r->o_name, rp, re, w->o_name, wp, we, mvcc_arg->i);
	if (nonempty)
		write_before_read(arg, r, rp, re);
	print_message("Going to %s(%s, "DF_U64") (expect 0)\n",
		      r->o_name, rp, re);
	rc = r->o_func(arg, rp, re);
	assert_int_equal(rc, 0);
	print_message("Going to %s(%s, "DF_U64") (expect %d)\n",
		      w->o_name, wp, we, expected_wrc);
	rc = w->o_func(arg, wp, we);
	assert_int_equal(rc, expected_wrc);
}

static void
conflicting_rw_exec(struct io_test_args *arg, int i, struct op *r, struct op *w)
{
	struct mvcc_arg	*mvcc_arg = arg->custom;
	daos_epoch_t	 re;			/* r epoch */
	daos_epoch_t	 we;			/* w epoch */
	char		 rp[L_COUNT + 1];	/* r path */
	char		 wp[L_COUNT + 1];	/* w path */
	char		*path_template = "coda";
	bool		 nonemptyness[] = {false, true};
	int		 j = 0;
	int		 k;

	/* T_R operations do not leave read epoch records at the moment. */
	if (is_r(r))
		return;

	/* Set overlapping paths. */
	set_path(r, path_template, rp);
	set_path(w, path_template, wp);
	D_ASSERTF(overlap(rp, wp), "overlap(\"%s\", \"%s\")", rp, wp);

	for (k = 0; k < ARRAY_SIZE(nonemptyness); k++) {
		bool nonempty = nonemptyness[k];

		/* Read, then write. re > we. */
		re = mvcc_arg->epoch + 10;
		we = mvcc_arg->epoch;
		conflicting_rw_exec_one(arg, i, j, nonempty, r, rp, re, w, wp,
					we, -DER_AGAIN /* expected_wrc */);
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/* Read, then write. re == we. */
		re = mvcc_arg->epoch;
		we = mvcc_arg->epoch;
		conflicting_rw_exec_one(arg, i, j, nonempty, r, rp, re, w, wp,
					we, -DER_AGAIN /* expected_wrc */);
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;

		/* Read, then write. re < we. Shall write OK. */
		re = mvcc_arg->epoch;
		we = mvcc_arg->epoch + 10;
		conflicting_rw_exec_one(arg, i, j, nonempty, r, rp, re, w, wp,
					we, 0 /* expected_wrc */);
		mvcc_arg->i++;
		mvcc_arg->epoch += 100;
	}
}

/* Verify that a read causes a conflicting write to be rejected. */
static void
conflicting_rw(void **state)
{
	struct io_test_args	*arg = *state;
	struct op		*r;
	struct op		*w;
	int			 n = 0;

	/* For each readwrite... */
	for_each_op(r) {
		if (!(is_r(r) || is_rw(r)))
			continue;

		/* For each readwrite or write... */
		for_each_op(w) {
			if (!(is_rw(w) || is_w(w)))
				continue;

			conflicting_rw_exec(arg, n, r, w);
			n++;
		}
	}
}

static const struct CMUnitTest mvcc_tests[] = {
	{ "VOS900: Conflicting read and write",
	  conflicting_rw, NULL, NULL },
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
	mvcc_arg->epoch = 10;
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
run_mvcc_tests(void)
{
	return cmocka_run_group_tests_name("VOS MVCC Tests", mvcc_tests,
					   setup_mvcc, teardown_mvcc);
}
