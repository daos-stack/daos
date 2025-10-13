/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <cmocka.h>
#include <getopt.h>
#include <sys/stat.h>
#include <argp.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/dtx.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>

#include "../dlck_args.h"
#include "../dlck_bitmap.h"
#include "../dlck_engine.h"
#include "../dlck_pool.h"

#define SRAND_SEED  0x4321
#define UPDATES_NUM 125

extern struct dss_module dtx_module;

struct dlck_helper_args {
	struct dlck_args_files  files;
	struct dlck_args_engine engine;
};

struct bundle {
	struct dlck_args_engine *args_engine;
	struct dlck_args_files  *args_files;
	struct dlck_engine      *engine;
	uuid_t                  *co_uuids;
	unsigned int             seed;
};

struct io {
	daos_unit_oid_t oid;
	uint64_t        dkey_buf;
	daos_key_t      dkey;
	uint64_t        akey_buf;
	daos_key_t      akey;
	daos_iod_t      iod;
	daos_recx_t     rex;
	char            value[UUID_STR_LEN];
	d_sg_list_t     sgl;
};

struct xstream_state {
	/** input */
	struct dlck_args_engine *args_engine;
	struct dlck_args_files  *args_files;
	struct dlck_engine      *engine;
	struct dlck_xstream     *xs;
	uuid_t                   co_uuid;
	unsigned int             seed;
	/** run-time variables */
	daos_handle_t            poh;
	daos_handle_t            coh;
	struct io                io;
	/** output */
	int                      rc;
};

static void
random_uuid_str(char *uuid_str, unsigned int *seedp)
{
	for (int i = 0; i < UUID_STR_LEN - 1; ++i) {
		snprintf(&uuid_str[i], 2, "%x", rand_r(seedp) % 16);
	}

	uuid_str[8] = uuid_str[13] = uuid_str[18] = uuid_str[23] = '-';
}

static void
random_uuid(uuid_t uuid, unsigned int *seedp)
{
	char uuid_str[UUID_STR_LEN];
	int  rc;

	random_uuid_str(uuid_str, seedp);

	rc = uuid_parse(uuid_str, uuid);
	assert_int_equal(rc, 0);
}

static void
cont_setup(struct xstream_state *xst, uuid_t co_uuid)
{
	int rc;

	(void)vos_cont_destroy(xst->poh, co_uuid);

	rc = vos_cont_create(xst->poh, co_uuid);
	assert_int_equal(rc, 0);

	rc = vos_cont_open(xst->poh, co_uuid, &xst->coh);
	assert_int_equal(rc, 0);
}

static void
cont_teardown(struct xstream_state *xst)
{
	int rc;

	rc = vos_cont_close(xst->coh);
	assert_int_equal(rc, 0);
}

static void
dtx_update(daos_handle_t coh, uuid_t dti_uuid, struct io *io, bool is_leader, bool commit)
{
	struct dtx_id             dti        = {0};
	struct dtx_epoch          epoch      = {0};
	daos_unit_oid_t           leader_oid = {0};
	uint32_t                  flags      = 0;
	struct dtx_leader_handle *dlh;
	struct dtx_handle        *dth;
	int                       rc;

	/** create DTI */
	uuid_copy(dti.dti_uuid, dti_uuid);
	dti.dti_hlc = d_hlc_get();

	epoch.oe_value = d_hlc_get();

	if (is_leader) {
		rc = dtx_leader_begin(coh, &dti, &epoch, 1, 0, &leader_oid, NULL, 0, NULL, 0, 0,
				      NULL, NULL, &dlh);
		assert_int_equal(rc, 0);
		dth = &dlh->dlh_handle;
	} else {
		rc = dtx_begin(coh, &dti, &epoch, 1, 0, &leader_oid, NULL, 0, flags, NULL, &dth);
		assert_int_equal(rc, 0);
	}

	rc = dtx_sub_init(dth, &io->oid, 0);
	assert_int_equal(rc, 0);

	rc = vos_obj_update_ex(coh, io->oid, 0, 0, 0, &io->dkey, 1, &io->iod, NULL, &io->sgl, dth);
	assert_int_equal(rc, 0);

	/**
	 * XXX Normally, the leader probably won't use this API to end its transaction.
	 * It also probably messes up a little with the DTX leader handle memory allocation.
	 */
	rc = dtx_end(dth, NULL, DER_SUCCESS);
	assert_int_equal(rc, 0);

	if (commit) {
		rc = vos_dtx_commit(coh, &dti, 1, true, NULL);
		assert_int_equal(rc, 1); /** total number of committed */
	}
}

static void
io_init_random(struct io *io, const char *value, daos_iod_type_t iod_type, unsigned int *seedp)
{
	int rc;

	memset(io, 0, sizeof(*io));

	/** random OID */
	io->oid.id_pub.hi = rand_r(seedp);
	io->oid.id_pub.lo = rand_r(seedp);

	/** random DKEY */
	io->dkey_buf = rand_r(seedp);
	d_iov_set(&io->dkey, (void *)&io->dkey_buf, sizeof(io->dkey_buf));

	/** random AKEY */
	io->akey_buf = rand_r(seedp);
	d_iov_set(&io->akey, (void *)&io->akey_buf, sizeof(io->akey_buf));

	/** populate the IO descriptor */
	io->iod.iod_name = io->akey;
	io->iod.iod_nr   = 1;
	io->iod.iod_size = strlen(value);

	if (iod_type == DAOS_IOD_SINGLE) {
		io->iod.iod_type  = DAOS_IOD_SINGLE;
		io->iod.iod_recxs = NULL;
	} else if (iod_type == DAOS_IOD_ARRAY) {
		io->iod.iod_type  = DAOS_IOD_ARRAY;
		io->rex.rx_idx    = 0;
		io->rex.rx_nr     = 1;
		io->iod.iod_recxs = &io->rex;
	} else {
		assert_true(false);
	}

	/** populate the SG list */
	rc = d_sgl_init(&io->sgl, 1);
	assert_int_equal(rc, 0);
	d_iov_set(&io->sgl.sg_iovs[0], (void *)value, io->iod.iod_size);
}

static void
io_fini(struct io *io)
{
	d_sgl_fini(&io->sgl, false);
}

static void
update_one(struct xstream_state *xst, daos_iod_type_t iod_type, bool is_leader, bool commit)
{
	uuid_t     dti_uuid;
	struct io *io = &xst->io;
	char       value[UUID_STR_LEN];

	random_uuid(dti_uuid, &xst->seed);
	random_uuid_str(value, &xst->seed);
	io_init_random(io, value, iod_type, &xst->seed);
	dtx_update(xst->coh, dti_uuid, io, is_leader, commit);
	io_fini(io);
}

static void
cont_process(struct xstream_state *xst, uuid_t co_uuid)
{
	bool is_leader;

	cont_setup(xst, xst->co_uuid);

	/**
	 * 2 (IOD types) * 125 * 4 = 1000 total updates
	 */
	for (daos_iod_type_t iod_type = DAOS_IOD_SINGLE; iod_type <= DAOS_IOD_ARRAY; ++iod_type) {
		for (int i = 0; i < UPDATES_NUM; ++i) {
			is_leader = true;
			update_one(xst, iod_type, is_leader, false /** commit */);
			update_one(xst, iod_type, is_leader, true /** commit */);
			is_leader = false;
			update_one(xst, iod_type, is_leader, false /** commit */);
			update_one(xst, iod_type, is_leader, true /** commit */);
		}
	}

	cont_teardown(xst);
}

static void
exec_one(void *arg)
{
	struct xstream_state *xst = arg;
	struct dlck_file     *file;
	int                   rc;

	rc = dlck_engine_xstream_init(xst->xs);
	if (rc != DER_SUCCESS) {
		xst->rc = rc;
		return;
	}

	d_list_for_each_entry(file, &xst->args_files->list, link) {
		/** do not process the given file if the target is not requested */
		if (dlck_bitmap_isclr32(file->targets_bitmap, xst->xs->tgt_id)) {
			continue;
		}

		rc = dlck_pool_open(xst->args_engine->storage_path, file->po_uuid, xst->xs->tgt_id,
				    &xst->poh);
		if (rc != DER_SUCCESS) {
			xst->rc = rc;
			break;
		}

		cont_process(xst, xst->co_uuid);

		rc = vos_pool_close(xst->poh);
		if (rc != DER_SUCCESS) {
			xst->rc = rc;
			break;
		}
	}

	if (xst->rc != DER_SUCCESS) {
		goto fail_xstream_fini;
	}

	rc = dlck_engine_xstream_fini(xst->xs);
	if (rc != DER_SUCCESS) {
		xst->rc = rc;
	}

	return;

fail_xstream_fini:
	(void)dlck_engine_xstream_fini(xst->xs);
}

/**
 * Allocate and populate arguments for an execution stream.
 */
static int
arg_alloc(struct dlck_engine *engine, int idx, void *input_arg, void **output_arg)
{
	struct bundle        *bundle = input_arg;
	struct xstream_state *xst;

	D_ALLOC_PTR(xst);
	if (xst == NULL) {
		return ENOMEM;
	}

	xst->args_engine = bundle->args_engine;
	xst->args_files  = bundle->args_files;
	xst->engine      = engine;
	xst->xs          = &engine->xss[idx];
	uuid_copy(xst->co_uuid, bundle->co_uuids[idx]);
	xst->seed = rand_r(&bundle->seed);

	*output_arg = xst;

	return 0;
}

/**
 * Free an execution stream's arguments.
 */
static int
arg_free(void *unused, void **arg)
{
	D_FREE(*arg);
	*arg = NULL;

	return 0;
}

/** command-line argument parsing */

extern struct argp        argp_file;
extern struct argp        argp_engine;

static struct argp_option _automagic[] = {OPT_HEADER("Other options:", GROUP_AUTOMAGIC), {0}};

static struct argp        automagic = {_automagic, NULL};

static struct argp_child  children[] = {{&argp_file}, {&argp_engine}, {&automagic}, {0}};

/**
 * \brief Main parser.
 *
 * Just provides inputs for the child parsers.
 */
error_t
parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_helper_args *args = state->input;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		state->child_inputs[0] = &args->files;
		state->child_inputs[1] = &args->engine;
		return 0;
	}

	return ARGP_ERR_UNKNOWN;
}

static struct argp argp = {NULL, parser, NULL /** usage */, NULL, children};

static int
setup(struct dlck_helper_args *args, struct bundle *bundle)
{
	struct dlck_engine *engine;
	unsigned int        seed = SRAND_SEED;
	int                 rc;
	int                 rc_abt;

	/** prepare pool storage directories */
	rc = dlck_pool_mkdir_all(args->engine.storage_path, &args->files.list, NULL);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc_abt = ABT_init(0, NULL);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		return rc;
	}

	/** start an engine */
	rc = dlck_engine_start(&args->engine, &engine);
	if (rc != DER_SUCCESS) {
		(void)ABT_finalize();
		return rc;
	}

	D_ALLOC_ARRAY(bundle->co_uuids, args->engine.targets);
	if (bundle->co_uuids == NULL) {
		rc = -DER_NOMEM;
		goto fail_engine_stop;
	}

	for (int i = 0; i < args->engine.targets; ++i) {
		random_uuid(bundle->co_uuids[i], &seed);
	}

	/** register DTX module key */
	dss_register_key(dtx_module.sm_key);

	bundle->args_engine = &args->engine;
	bundle->args_files  = &args->files;
	bundle->engine      = engine;
	bundle->seed        = seed;

	return DER_SUCCESS;

fail_engine_stop:
	(void)dlck_engine_stop(engine);
	(void)ABT_finalize();
	return rc;
}

static int
teardown(struct bundle *bundle)
{
	int rc_abt;
	int rc;

	dss_unregister_key(dtx_module.sm_key);

	D_FREE(bundle->co_uuids);

	rc = dlck_engine_stop(bundle->engine);
	if (rc != DER_SUCCESS) {
		(void)ABT_finalize();
		return rc;
	}

	rc_abt = ABT_finalize();
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
	}

	return rc;
}

int
main(int argc, char **argv)
{
	struct dlck_helper_args args   = {0};
	struct bundle           bundle = {0};

	int                     rc;

	argp_parse(&argp, argc, argv, 0, 0, &args);

	rc = setup(&args, &bundle);
	if (rc != DER_SUCCESS) {
		goto fail_args_free;
	}

	rc = dlck_engine_exec_all_sync(bundle.engine, exec_one, arg_alloc, &bundle, arg_free);
	if (rc != DER_SUCCESS) {
		goto fail_teardown;
	}

	rc = teardown(&bundle);
	if (rc != DER_SUCCESS) {
		goto fail_args_free;
	}

	/** XXX args free */

	return 0;

fail_teardown:
	(void)teardown(&bundle);
fail_args_free:
	/** XXX args free */
	return rc;
}
