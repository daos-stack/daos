/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>
#include <argp.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/dtx.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>

#include "dlck_test_helper_args.h"

#include "../dlck_bitmap.h"
#include "../dlck_engine.h"
#include "../dlck_pool.h"

#define SRAND_SEED 0x4321

extern struct dss_module dtx_module;

struct dlck_helper_paths {
	uuid_t           co_uuid;
	daos_unit_oid_t *oids;
	uint64_t        *keys;
};

struct bundle {
	struct dlck_args_engine *args_engine;
	struct dlck_args_files  *args_files;
	struct dlck_helper_workload_args *args_workload;
	struct dlck_engine               *engine;
	unsigned int             seed;
	struct dlck_helper_paths          paths;
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
	struct dlck_helper_workload_args *args_workload;
	struct dlck_engine      *engine;
	struct dlck_xstream              *xs;
	unsigned int             seed;
	struct dlck_helper_paths         *paths;
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
	assert(rc == 0);
}

static void
cont_setup(struct xstream_state *xst, uuid_t co_uuid)
{
	int rc;

	(void)vos_cont_destroy(xst->poh, co_uuid);

	rc = vos_cont_create(xst->poh, co_uuid);
	assert(rc == 0);

	rc = vos_cont_open(xst->poh, co_uuid, &xst->coh);
	assert(rc == 0);
}

static void
cont_teardown(struct xstream_state *xst)
{
	int rc;

	rc = vos_cont_close(xst->coh);
	assert(rc == 0);
}

static void
dtx_update(daos_handle_t coh, uuid_t dti_uuid, struct io *io, bool is_leader, bool commit,
	   bool keep_act)
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
		assert(rc == 0);
		dth = &dlh->dlh_handle;
	} else {
		rc = dtx_begin(coh, &dti, &epoch, 1, 0, &leader_oid, NULL, 0, flags, NULL, &dth);
		assert(rc == 0);
	}

	rc = dtx_sub_init(dth, &io->oid, 0);
	assert(rc == 0);

	rc = vos_obj_update_ex(coh, io->oid, 0, 0, 0, &io->dkey, 1, &io->iod, NULL, &io->sgl, dth);
	if (rc == -DER_NO_PERM) {
		/** Current implementation does not track already used paths so the same path may be
		 * reused with a different data type which is not allowed. */
		return;
	}
	assert(rc == 0);

	/**
	 * XXX Normally, the leader probably won't use this API to end its transaction.
	 * It also probably messes up a little with the DTX leader handle memory allocation.
	 */
	rc = dtx_end(dth, NULL, DER_SUCCESS);
	assert(rc == 0);

	if (commit) {
		rc = vos_dtx_commit(coh, &dti, 1, keep_act, NULL);
		if (keep_act) {
			assert(rc == 0);
		} else {
			assert(rc == 1); /** total number of committed */
		}
	}
}

static void
io_init_random(struct xstream_state *xst, const char *value, daos_iod_type_t iod_type,
	       struct io *io)
{
	int rc;

	memset(io, 0, sizeof(*io));

	/** random OID */
	io->oid.id_pub = xst->paths->oids[rand_r(&xst->seed) % xst->args_workload->oids_num].id_pub;

	/** random DKEY */
	io->dkey_buf = xst->paths->keys[rand_r(&xst->seed) % xst->args_workload->keys_num];
	d_iov_set(&io->dkey, (void *)&io->dkey_buf, sizeof(io->dkey_buf));

	/** random AKEY */
	io->akey_buf = xst->paths->keys[rand_r(&xst->seed) % xst->args_workload->keys_num];
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
		assert(false);
	}

	/** populate the SG list */
	rc = d_sgl_init(&io->sgl, 1);
	assert(rc == 0);
	d_iov_set(&io->sgl.sg_iovs[0], (void *)value, io->iod.iod_size);
}

static void
io_fini(struct io *io)
{
	d_sgl_fini(&io->sgl, false);
}

static void
update_one(struct xstream_state *xst, daos_iod_type_t iod_type, bool is_leader, bool commit,
	   bool keep_act)
{
	uuid_t     dti_uuid;
	struct io *io = &xst->io;
	char       value[UUID_STR_LEN];

	random_uuid(dti_uuid, &xst->seed);
	random_uuid_str(value, &xst->seed);
	io_init_random(xst, value, iod_type, io);
	printf("Update: " DF_OID "/%" PRIx64 "/%" PRIx64 "\n", DP_OID(io->oid.id_pub), io->dkey_buf,
	       io->akey_buf);
	dtx_update(xst->coh, dti_uuid, io, is_leader, commit, keep_act);
	io_fini(io);
}

static void
cont_process(struct xstream_state *xst, uuid_t co_uuid)
{
	daos_iod_type_t iod_types[] = {DAOS_IOD_SINGLE, DAOS_IOD_ARRAY};
	daos_iod_type_t iod_type;
	bool            is_leader;
	bool            commit;
	bool            keep_act;

	cont_setup(xst, co_uuid);

	for (int i = 0; i < xst->args_workload->updates_num; ++i) {
		iod_type  = iod_types[rand_r(&xst->seed) % ARRAY_SIZE(iod_types)];
		is_leader = rand_r(&xst->seed) % 2;
		commit    = rand_r(&xst->seed) % 2;
		keep_act  = commit ? (rand_r(&xst->seed) % 2) : false;

		update_one(xst, iod_type, is_leader, commit, keep_act);
	}

	cont_teardown(xst);
}

static void
exec_one(void *arg)
{
	struct xstream_state *xst = arg;
	struct dlck_file     *file;
	int                   rc;

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

		cont_process(xst, xst->paths->co_uuid);

		rc = vos_pool_close(xst->poh);
		if (rc != DER_SUCCESS) {
			xst->rc = rc;
			break;
		}
	}

	return;
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
	xst->args_workload = bundle->args_workload;
	xst->engine      = engine;
	xst->xs          = &engine->xss[idx];
	xst->paths         = &bundle->paths;
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

static int
setup(struct dlck_helper_args *args, struct bundle *bundle)
{
	struct dlck_helper_paths *paths = &bundle->paths;
	struct dlck_engine *engine;
	unsigned int        seed = SRAND_SEED;
	int                 rc;
	int                 rc_abt;

	rc_abt = ABT_init(0, NULL);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		return rc;
	}

	/** register DTX module key */
	dss_register_key(dtx_module.sm_key);

	/** start an engine */
	rc = dlck_engine_start(&args->engine, &engine);
	if (rc != DER_SUCCESS) {
		goto fail_unregister;
	}

	if (d_list_empty(&args->files.list)) {
		/** no files specified means all files are requested */
		rc = dlck_pool_list(&args->files.list);
		if (rc != DER_SUCCESS) {
			goto fail_engine_stop;
		}
	}

	/** prepare pool storage directories */
	rc = dlck_pool_mkdir_all(args->engine.storage_path, &args->files.list, NULL);
	if (rc != DER_SUCCESS) {
		goto fail_engine_stop;
	}

	/** generate random elements of VOS path */
	random_uuid(paths->co_uuid, &seed);

	D_ALLOC_ARRAY(paths->oids, args->workload.oids_num);
	assert(paths->oids != NULL);
	for (int i = 0; i < args->workload.oids_num; ++i) {
		paths->oids[i].id_pub.hi = rand_r(&seed);
		paths->oids[i].id_pub.lo = rand_r(&seed);
	}

	D_ALLOC_ARRAY(paths->keys, args->workload.keys_num);
	assert(paths->keys != NULL);
	for (int i = 0; i < args->workload.keys_num; ++i) {
		paths->keys[i] = rand_r(&seed);
	}

	/** write relevant bits into the bundle */
	bundle->args_engine = &args->engine;
	bundle->args_files  = &args->files;
	bundle->args_workload = &args->workload;
	bundle->engine      = engine;
	bundle->seed        = seed;

	return DER_SUCCESS;

fail_engine_stop:
	(void)dlck_engine_stop(engine);
fail_unregister:
	dss_unregister_key(dtx_module.sm_key);
	(void)ABT_finalize();
	return rc;
}

static int
teardown(struct bundle *bundle)
{
	int rc_abt;
	int rc;

	D_FREE(bundle->paths.keys);
	D_FREE(bundle->paths.oids);

	rc = dlck_engine_stop(bundle->engine);
	if (rc != DER_SUCCESS) {
		goto fail_unregister;
	}

	dss_unregister_key(dtx_module.sm_key);

	rc_abt = ABT_finalize();
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
	}

	return rc;

fail_unregister:
	dss_unregister_key(dtx_module.sm_key);
	(void)ABT_finalize();
	return rc;
}

int
main(int argc, char **argv)
{
	struct dlck_helper_args args   = {0};
	struct bundle           bundle = {0};

	int                     rc;

	dlck_helper_args_parse(argc, argv, &args);

	rc = setup(&args, &bundle);
	if (rc != DER_SUCCESS) {
		goto fail_args_free;
	}

	rc = dlck_engine_exec_all(bundle.engine, exec_one, arg_alloc, &bundle, arg_free, NULL);
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
