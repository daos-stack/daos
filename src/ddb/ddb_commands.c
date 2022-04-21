/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_cmd_options.h"
#include "ddb_vos.h"
#include "ddb_printer.h"

int
ddb_run_quit(struct ddb_ctx *ctx)
{
	ctx->dc_should_quit = true;
	return 0;
}

struct ls_ctx {
	struct ddb_ctx	*ctx;
	bool		 has_cont;
	bool		 has_obj;
	bool		 has_dkey;
	bool		 has_akey;
};

#define DF_IDX "[%d]"
#define DP_IDX(idx) idx

static int
init_path(daos_handle_t poh, char *path, struct dv_tree_path_builder *vtp)
{
	int rc;

	rc = ddb_vtp_init(poh, path, vtp);
	if (!SUCCESS(rc))
		return rc;

	rc = dv_path_update_from_indexes(vtp);
	if (!SUCCESS(rc))
		return rc;
	return 0;
}

static int
ls_cont_handler(struct ddb_cont *cont, void *args)
{
	struct ls_ctx *ctx = args;

	ctx->has_cont = true;
	ddb_print_cont(ctx->ctx, cont);

	return 0;
}

static int
ls_obj_handler(struct ddb_obj *obj, void *args)
{
	struct ls_ctx		*ctx = args;

	ctx->has_obj = true;

	ddb_print_obj(ctx->ctx, obj, ctx->has_cont);

	return 0;
}

static int
ls_dkey_handler(struct ddb_key *key, void *args)
{
	struct ls_ctx *ctx = args;

	ctx->has_dkey = true;
	ddb_print_key(ctx->ctx, key, ctx->has_cont + ctx->has_obj);

	return 0;
}

static int
ls_akey_handler(struct ddb_key *key, void *args)
{
	struct ls_ctx *ctx = args;

	ctx->has_akey = true;
	ddb_print_key(ctx->ctx, key, ctx->has_cont + ctx->has_obj + ctx->has_dkey);

	return 0;
}

static int
ls_sv_handler(struct ddb_sv *val, void *args)
{
	struct ls_ctx *ctx = args;

	ddb_print_sv(ctx->ctx, val, ctx->has_cont + ctx->has_obj + ctx->has_dkey + ctx->has_akey);
	return 0;
}

static int
ls_array_handler(struct ddb_array *val, void *args)
{
	struct ls_ctx *ctx = args;

	ddb_print_array(ctx->ctx, val,
			ctx->has_cont + ctx->has_obj + ctx->has_dkey + ctx->has_akey);
	return 0;
}

static struct vos_tree_handlers handlers = {
	.ddb_cont_handler = ls_cont_handler,
	.ddb_obj_handler = ls_obj_handler,
	.ddb_dkey_handler = ls_dkey_handler,
	.ddb_akey_handler = ls_akey_handler,
	.ddb_array_handler = ls_array_handler,
	.ddb_sv_handler = ls_sv_handler,
};

int
ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt)
{
	int rc;
	struct dv_tree_path_builder vtp = {0};
	struct ls_ctx lsctx = {0};

	rc = init_path(ctx->dc_poh, opt->path, &vtp);
	if (!SUCCESS(rc))
		return rc;
	if (!SUCCESS(ddb_vtp_verify(ctx->dc_poh, &vtp.vtp_path))) {
		ddb_print(ctx, "Not a valid path\n");
		return -DER_NONEXIST;
	}

	vtp_print(ctx, &vtp.vtp_path, true);
	lsctx.ctx = ctx;
	rc = dv_iterate(ctx->dc_poh, &vtp.vtp_path, opt->recursive, &handlers, &lsctx);

	ddb_vtp_fini(&vtp);

	return rc;
}

static int
print_superblock_cb(void *cb_arg, struct ddb_superblock *sb)
{
	struct ddb_ctx *ctx = cb_arg;

	ddb_print_superblock(ctx, sb);

	return 0;
}

int
ddb_run_dump_superblock(struct ddb_ctx *ctx)
{
	int rc;

	rc = dv_superblock(ctx->dc_poh, print_superblock_cb, ctx);

	if (rc == -DER_DF_INVAL)
		ddb_error(ctx, "Error with pool superblock");

	return rc;
}

struct dump_value_args {
	struct ddb_ctx			*dva_ctx;
	struct dv_tree_path		*dva_vtp;
	char				*dva_dst_path;
};

static int
write_file_value_cb(void *cb_args, d_iov_t *value)
{
	struct dump_value_args *args = cb_args;
	struct ddb_ctx *ctx = args->dva_ctx;

	D_ASSERT(ctx->dc_io_ft.ddb_write_file);

	if (value->iov_len == 0) {
		ddb_print(ctx, "No value at: ");
		vtp_print(ctx, args->dva_vtp, true);
		return 0;
	}

	ddb_printf(ctx, "Dumping value (size: %lu) to: %s\n",
		   value->iov_len,  args->dva_dst_path);

	return ctx->dc_io_ft.ddb_write_file(args->dva_dst_path, value);
}

int
ddb_run_dump_value(struct ddb_ctx *ctx, struct dump_value_options *opt)
{
	struct dv_tree_path_builder	vtp = {.vtp_poh = ctx->dc_poh};
	struct dump_value_args		dva = {0};
	int				rc;

	if (!opt->path) {
		ddb_error(ctx, "A VOS path to dump is required.\n");
		return -DER_INVAL;
	}
	if (!opt->dst) {
		ddb_error(ctx, "A destination path is required.\n");
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, opt->path, &vtp);
	if (!SUCCESS(rc))
		return rc;

	vtp_print(ctx, &vtp.vtp_path, true);

	if (!dvp_is_complete(&vtp.vtp_path)) {
		ddb_errorf(ctx, "Path [%s] is incomplete.\n", opt->path);
		ddb_vtp_fini(&vtp);
		return -DER_INVAL;
	}

	dva.dva_dst_path = opt->dst;
	dva.dva_ctx = ctx;
	dva.dva_vtp = &vtp.vtp_path;
	rc = dv_dump_value(ctx->dc_poh, &vtp.vtp_path, write_file_value_cb, &dva);
	ddb_vtp_fini(&vtp);

	return rc;
}

static int
dump_ilog_entry_cb(void *cb_arg, struct ddb_ilog_entry *entry)
{
	struct ddb_ctx *ctx = cb_arg;

	ddb_print_ilog_entry(ctx, entry);

	return 0;
}

int
ddb_run_dump_ilog(struct ddb_ctx *ctx, struct dump_ilog_options *opt)
{
	struct dv_tree_path_builder	vtp = {ctx->dc_poh};
	int				rc;
	daos_handle_t			coh;

	if (!opt->path) {
		ddb_error(ctx, "A VOS path to dump the ilog for is required.");
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, opt->path, &vtp);
	if (!SUCCESS(rc))
		return rc;

	/*
	 * ilog for object or dkey ...
	 * Should have a path to at least the object, but not including an akey
	 */
	if (!dv_has_cont(&vtp.vtp_path) || !dv_has_obj(&vtp.vtp_path) ||
	    dv_has_akey(&vtp.vtp_path)) {
		ddb_error(ctx, "Path to object or dkey is required.\n");
		ddb_vtp_fini(&vtp);
		return -DER_INVAL;
	}

	vtp_print(ctx, &vtp.vtp_path, true);
	rc = dv_cont_open(ctx->dc_poh, vtp.vtp_path.vtp_cont, &coh);
	if (!SUCCESS(rc)) {
		ddb_vtp_fini(&vtp);
		return rc;
	}
	rc = dv_get_obj_ilog_entries(coh, vtp.vtp_path.vtp_oid,
				     dump_ilog_entry_cb, ctx);
	dv_cont_close(&coh);
	ddb_vtp_fini(&vtp);

	return rc;
}

static int
active_dtx_cb(struct dv_dtx_active_entry *entry, void *cb_arg)
{
	struct ddb_ctx *ctx = cb_arg;

	ddb_print_dtx_active(ctx, entry);

	return 0;
}

static int
committed_cb(struct dv_dtx_committed_entry *entry, void *cb_arg)
{
	struct ddb_ctx *ctx = cb_arg;

	ddb_print_dtx_committed(ctx, entry);

	return 0;
}

int
ddb_run_dump_dtx(struct ddb_ctx *ctx, struct dump_dtx_options *opt)
{
	struct dv_tree_path_builder	vtp;
	int				rc;
	daos_handle_t			coh;
	bool				both = !(opt->committed ^ opt->active);

	rc = init_path(ctx->dc_poh, opt->path, &vtp);
	if (!SUCCESS(rc))
		return rc;

	if (!dv_has_cont(&vtp.vtp_path)) {
		ddb_error(ctx, "Path to object is required.\n");
		ddb_vtp_fini(&vtp);
		return -DER_INVAL;
	}

	rc = dv_cont_open(ctx->dc_poh, vtp.vtp_path.vtp_cont, &coh);
	if (!SUCCESS(rc)) {
		ddb_vtp_fini(&vtp);
		return rc;
	}

	if (both || opt->active) {
		ddb_print(ctx, "Active Transactions:\n");
		rc = dv_active_dtx(coh, active_dtx_cb, ctx);
		if (!SUCCESS(rc)) {
			ddb_vtp_fini(&vtp);
			return rc;
		}
	}
	if (both || opt->active) {
		ddb_print(ctx, "Committed Transactions:\n");
		rc = dv_committed_dtx(coh, committed_cb, ctx);
		if (!SUCCESS(rc)) {
			ddb_vtp_fini(&vtp);
			return rc;
		}
	}

	dv_cont_close(&coh);
	ddb_vtp_fini(&vtp);

	return 0;
}

int
ddb_run_rm(struct ddb_ctx *ctx, struct rm_options *opt)
{
	struct dv_tree_path_builder	vtpb;
	int				rc;

	rc = init_path(ctx->dc_poh, opt->path, &vtpb);

	if (!SUCCESS(rc))
		return rc;

	rc = dv_delete(ctx->dc_poh, &vtpb.vtp_path);

	if (!SUCCESS(rc)) {
		ddb_errorf(ctx, "Error: "DF_RC"\n", DP_RC(rc));
		ddb_vtp_fini(&vtpb);

		return rc;
	}

	vtp_print(ctx, &vtpb.vtp_path, false);
	ddb_print(ctx, " deleted\n");

	ddb_vtp_fini(&vtpb);

	return 0;
}

int
ddb_run_load(struct ddb_ctx *ctx, struct load_options *opt)
{
	struct dv_tree_path_builder	vtpb;
	d_iov_t				iov = {0};
	size_t				file_size;
	uint32_t			epoch;
	int				rc;
	char				*end;

	if (opt->epoch == NULL) {
		ddb_error(ctx, "Epoch is not set\n");
		return -DER_INVAL;
	}
	epoch = strtol(opt->epoch, &end, 10);
	if (epoch == 0 || *end != '\0') {
		ddb_errorf(ctx, "Epoch '%s' is not valid\n", opt->epoch);
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, opt->dst, &vtpb);

	if (!SUCCESS(rc)) {
		ddb_error(ctx, "Invalid VOS path\n");
		return rc;
	}


	if (!dvp_is_complete(&vtpb.vtp_path)) {
		ddb_error(ctx, "Invalid path");
		D_GOTO(done, rc = -DER_INVAL);
	}

	vtp_print(ctx, &vtpb.vtp_path, true);

	if (!ctx->dc_io_ft.ddb_get_file_exists(opt->src)) {
		ddb_errorf(ctx, "Unable to access '%s'\n", opt->src);
		return -DER_INVAL;
	}

	file_size = ctx->dc_io_ft.ddb_get_file_size(opt->src);
	if (file_size == 0)
		D_GOTO(done, rc = -DER_INVAL);
	rc = daos_iov_alloc(&iov, file_size, false);
	if (!SUCCESS(rc)) {
		ddb_errorf(ctx, "System error: "DF_RC"\n", DP_RC(rc));
		D_GOTO(done, rc);
	}

	rc = ctx->dc_io_ft.ddb_read_file(opt->src, &iov);
	if (rc < 0) {
		ddb_errorf(ctx, "System error: "DF_RC"\n", DP_RC(rc));
		D_GOTO(done, rc);
	}
	D_ASSERT(rc == iov.iov_buf_len && rc == iov.iov_len);

	rc = dv_update(ctx->dc_poh, &vtpb.vtp_path, &iov, epoch);
	if (!SUCCESS(rc)) {
		ddb_errorf(ctx, "Unable to update path: "DF_RC"\n", DP_RC(rc));
		D_GOTO(done, rc);
	}

done:
	daos_iov_free(&iov);
	ddb_vtp_fini(&vtpb);

	if (SUCCESS(rc))
		ddb_printf(ctx, "Successfully loaded file '%s'\n", opt->src);

	return rc;
}
