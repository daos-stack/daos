/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb.h"
#include "ddb_vos.h"
#include "ddb_printer.h"
#include "daos.h"

#define ilog_path_required_error_message "Path to object, dkey, or akey required\n"
#define error_msg_write_mode_only "Can only modify the VOS tree in 'write mode'\n"

int
ddb_run_version(struct ddb_ctx *ctx)
{
	ddb_printf(ctx, "ddb version %d.%d.%d\n",
		   DAOS_VERSION_MAJOR,
		   DAOS_VERSION_MINOR,
		   DAOS_VERSION_FIX);

	return 0;
}

int
ddb_run_help(struct ddb_ctx *ctx)
{
	ddb_commands_help(ctx);

	return 0;
}

int
ddb_run_quit(struct ddb_ctx *ctx)
{
	ctx->dc_should_quit = true;
	return 0;
}

bool
ddb_pool_is_open(struct ddb_ctx *ctx)
{
	return daos_handle_is_valid(ctx->dc_poh);
}

int
ddb_run_open(struct ddb_ctx *ctx, struct open_options *opt)
{
	if (ddb_pool_is_open(ctx)) {
		ddb_error(ctx, "Must close pool before can open another\n");
		return -DER_EXIST;
	}
	ctx->dc_write_mode = opt->write_mode;
	return dv_pool_open(opt->path, &ctx->dc_poh);
}

int
ddb_run_close(struct ddb_ctx *ctx)
{
	int rc;

	if (!ddb_pool_is_open(ctx)) {
		ddb_error(ctx, "No pool open to close\n");
		return 0;
	}

	rc = dv_pool_close(ctx->dc_poh);
	ctx->dc_poh = DAOS_HDL_INVAL;
	ctx->dc_write_mode = false;

	return rc;
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

	rc = dv_path_verify(vtp);
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

	if (daos_handle_is_inval(ctx->dc_poh)) {
		ddb_error(ctx, "Not connected to a pool. Use 'open' to connect to a pool.\n");
		return -DER_NONEXIST;
	}
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
ddb_run_superblock_dump(struct ddb_ctx *ctx)
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
print_value_cb(void *cb_args, d_iov_t *value)
{
	struct dump_value_args *args = cb_args;
	struct ddb_ctx *ctx = args->dva_ctx;
	char buf[256];

	if (value->iov_len == 0) {
		ddb_print(ctx, "No value at: ");
		vtp_print(ctx, args->dva_vtp, true);
		return 0;
	}

	ddb_iov_to_printable_buf(value, buf, ARRAY_SIZE(buf));
	ddb_printf(ctx, "Value (size: %lu):\n", value->iov_len);
	ddb_printf(ctx, "%s\n", buf);
	return 0;
}

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
ddb_run_value_dump(struct ddb_ctx *ctx, struct value_dump_options *opt)
{
	struct dv_tree_path_builder	vtp = {.vtp_poh = ctx->dc_poh};
	struct dump_value_args		dva = {0};
	dv_dump_value_cb		cb = NULL;
	int				rc;

	if (!opt->path) {
		ddb_error(ctx, "A VOS path to dump is required.\n");
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

	if (opt->dst && strlen(opt->dst) > 0)
		cb = write_file_value_cb;
	else
		cb = print_value_cb;

	dva.dva_dst_path = opt->dst;
	dva.dva_ctx = ctx;
	dva.dva_vtp = &vtp.vtp_path;
	rc = dv_dump_value(ctx->dc_poh, &vtp.vtp_path, cb, &dva);
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
ddb_run_ilog_dump(struct ddb_ctx *ctx, struct ilog_dump_options *opt)
{
	struct dv_tree_path_builder	 vtpb = {0};
	struct dv_tree_path		*vtp = &vtpb.vtp_path;
	daos_handle_t			 coh;
	int				 rc;

	if (!opt->path) {
		ddb_error(ctx, ilog_path_required_error_message);
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, opt->path, &vtpb);
	if (!SUCCESS(rc))
		return rc;
	vtp_print(ctx, vtp, true);

	if (!dv_has_cont(vtp)) {
		ddb_error(ctx, ilog_path_required_error_message);
		return -DER_INVAL;
	}

	rc = dv_cont_open(ctx->dc_poh, vtp->vtp_cont, &coh);
	if (!SUCCESS(rc)) {
		ddb_vtp_fini(&vtpb);
		return rc;
	}

	if (dv_has_akey(vtp)) {
		rc = dv_get_key_ilog_entries(coh, vtp->vtp_oid, &vtp->vtp_dkey, &vtp->vtp_akey,
					     dump_ilog_entry_cb, ctx);
	} else if (dv_has_dkey(vtp)) {
		rc = dv_get_key_ilog_entries(coh, vtp->vtp_oid, &vtp->vtp_dkey, NULL,
					     dump_ilog_entry_cb, ctx);
	} else if (dv_has_obj(vtp)) {
		rc = dv_get_obj_ilog_entries(coh, vtpb.vtp_path.vtp_oid, dump_ilog_entry_cb, ctx);
	} else {
		ddb_error(ctx, ilog_path_required_error_message);
		rc = -DER_INVAL;
	}

	dv_cont_close(&coh);
	ddb_vtp_fini(&vtpb);

	return rc;
}

struct dtx_cb_args {
	struct ddb_ctx *ctx;
	uint32_t entry_count;
};

static int
active_dtx_cb(struct dv_dtx_active_entry *entry, void *cb_arg)
{
	struct dtx_cb_args *args = cb_arg;

	ddb_print_dtx_active(args->ctx, entry);
	args->entry_count++;

	return 0;
}

static int
committed_cb(struct dv_dtx_committed_entry *entry, void *cb_arg)
{
	struct dtx_cb_args *args = cb_arg;

	ddb_print_dtx_committed(args->ctx, entry);
	args->entry_count++;

	return 0;
}

int
ddb_run_dtx_dump(struct ddb_ctx *ctx, struct dtx_dump_options *opt)
{
	struct dv_tree_path_builder	vtp;
	int				rc;
	daos_handle_t			coh;
	bool				both = !(opt->committed ^ opt->active);
	struct dtx_cb_args	args = {.ctx = ctx, .entry_count = 0};

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

	vtp_print(ctx, &vtp.vtp_path, true);

	if (both || opt->active) {
		ddb_print(ctx, "Active Transactions:\n");
		rc = dv_dtx_get_act_table(coh, active_dtx_cb, &args);
		if (!SUCCESS(rc)) {
			ddb_vtp_fini(&vtp);
			return rc;
		}
		ddb_printf(ctx, "%d Active Entries\n", args.entry_count);
	}
	if (both || opt->committed) {
		args.entry_count = 0;
		ddb_print(ctx, "Committed Transactions:\n");
		rc = dv_dtx_get_cmt_table(coh, committed_cb, &args);
		if (!SUCCESS(rc)) {
			ddb_vtp_fini(&vtp);
			return rc;
		}
		ddb_printf(ctx, "%d Committed Entries\n", args.entry_count);
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

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

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
ddb_run_value_load(struct ddb_ctx *ctx, struct value_load_options *opt)
{
	struct dv_tree_path_builder	pb;
	d_iov_t				iov = {0};
	size_t				file_size;
	int				rc;

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, opt->dst, &pb);
	if (rc == -DER_NONEXIST) {
		/* It's okay that the path doesn't exist as long as the container does */
		if (pb.vtp_cont_verified)
			rc = 0;
	}

	if (!SUCCESS(rc)) {
		ddb_error(ctx, "Invalid VOS path\n");
		D_GOTO(done, rc);
	}

	if (!dvp_is_complete(&pb.vtp_path)) {
		ddb_error(ctx, "Invalid path");
		D_GOTO(done, rc = -DER_INVAL);
	}

	vtp_print(ctx, &pb.vtp_path, true);

	if (!ctx->dc_io_ft.ddb_get_file_exists(opt->src)) {
		ddb_errorf(ctx, "Unable to access '%s'\n", opt->src);
		D_GOTO(done, rc = -DER_INVAL);
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
	} else if (!(rc == iov.iov_buf_len && rc == iov.iov_len)) {
		D_ERROR("Bytes read from file does not match results from get file size\n");
		D_GOTO(done, rc = -DER_UNKNOWN);
	}

	rc = dv_update(ctx->dc_poh, &pb.vtp_path, &iov);
	if (!SUCCESS(rc)) {
		ddb_errorf(ctx, "Unable to update path: "DF_RC"\n", DP_RC(rc));
		D_GOTO(done, rc);
	}

done:
	daos_iov_free(&iov);
	ddb_vtp_fini(&pb);

	if (SUCCESS(rc))
		ddb_printf(ctx, "Successfully loaded file '%s'\n", opt->src);

	return rc;
}

static int
process_ilog_op(struct ddb_ctx *ctx, char *path, enum ddb_ilog_op op)
{
	struct dv_tree_path_builder	 vtpb = {0};
	daos_handle_t			 coh = {0};
	int				 rc;
	struct dv_tree_path		*vtp = &vtpb.vtp_path;

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

	if (path == NULL) {
		ddb_error(ctx, ilog_path_required_error_message);
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, path, &vtpb);
	if (!SUCCESS(rc))
		return rc;
	vtp_print(ctx, vtp, true);

	if (!dv_has_cont(vtp)) {
		ddb_error(ctx, ilog_path_required_error_message);
		return -DER_INVAL;
	}

	rc = dv_cont_open(ctx->dc_poh, vtp->vtp_cont, &coh);
	if (!SUCCESS(rc)) {
		ddb_vtp_fini(&vtpb);
		return rc;
	}

	if (dv_has_akey(vtp)) {
		rc = dv_process_key_ilog_entries(coh, vtp->vtp_oid, &vtp->vtp_dkey,
						 &vtp->vtp_akey, op);

	} else if (dv_has_dkey(vtp)) {
		rc = dv_process_key_ilog_entries(coh, vtp->vtp_oid, &vtp->vtp_dkey, NULL, op);
	} else if (dv_has_obj(vtp)) {
		rc = dv_process_obj_ilog_entries(coh, vtp->vtp_oid, op);
	} else {
		ddb_error(ctx, ilog_path_required_error_message);
		rc = -DER_INVAL;
	}

	dv_cont_close(&coh);
	ddb_vtp_fini(&vtpb);

	if (SUCCESS(rc))
		ddb_print(ctx, "Done\n");
	else
		ddb_errorf(ctx, "Failed to %s ilogs: "DF_RC"\n",
			   op == DDB_ILOG_OP_ABORT ? "abort" : "persist", DP_RC(rc));
	return rc;
}

int
ddb_run_ilog_clear(struct ddb_ctx *ctx, struct ilog_clear_options *opt)
{
	return process_ilog_op(ctx, opt->path, DDB_ILOG_OP_ABORT);
}

int
ddb_run_ilog_commit(struct ddb_ctx *ctx, struct ilog_commit_options *opt)
{
	return process_ilog_op(ctx, opt->path, DDB_ILOG_OP_PERSIST);
}

int
ddb_run_dtx_cmt_clear(struct ddb_ctx *ctx, struct dtx_cmt_clear_options *opt)
{
	struct dv_tree_path_builder	 vtpb = {0};
	struct dv_tree_path		*vtp = &vtpb.vtp_path;
	daos_handle_t			 coh = {0};
	int				 rc;

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

	if (opt->path == NULL) {
		ddb_error(ctx, "path is required\n");
		return -DER_INVAL;
	}

	rc = init_path(ctx->dc_poh, opt->path, &vtpb);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);
	vtp_print(ctx, &vtpb.vtp_path, true);

	if (!dv_has_cont(vtp))
		D_GOTO(done, rc = -DER_INVAL);

	rc = dv_cont_open(ctx->dc_poh, vtp->vtp_cont, &coh);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);

	rc = dv_dtx_clear_cmt_table(coh);
	if (rc < 0)
		D_GOTO(done, rc);

	ddb_printf(ctx, "Cleared %d dtx committed entries\n", rc);
	rc = 0;

done:
	ddb_vtp_fini(&vtpb);
	dv_cont_close(&coh);
	return rc;
}

static int
sync_smd_cb(void *cb_args, uuid_t pool_id, uint32_t vos_id, uint64_t blob_id,
	    daos_size_t blob_size, uuid_t dev_id)
{
	struct ddb_ctx *ctx = cb_args;

	ddb_printf(ctx, "> Sync Info - pool: "DF_UUIDF", target id: %d, blob id: %lu, "
		   "blob_size: %lu\n", DP_UUID(pool_id),
		   vos_id, blob_id, blob_size);
	ddb_printf(ctx, "> Sync Info - dev: "DF_UUIDF", target id: %d\n", DP_UUID(dev_id), vos_id);

	return 0;
}

int
ddb_run_smd_sync(struct ddb_ctx *ctx, struct smd_sync_options *opt)
{
	/* Some defaults */
	char	nvme_conf[256] = "/mnt/daos/daos_nvme.conf";
	char	db_path[256] = "/mnt/daos";
	int	rc;

	if (daos_handle_is_valid(ctx->dc_poh)) {
		ddb_print(ctx, "Close pool connection before attempting to sync smd\n");
		return -DER_INVAL;
	}

	if (opt->nvme_conf != NULL && strlen(opt->nvme_conf) > 0)
		strncpy(nvme_conf, opt->nvme_conf, ARRAY_SIZE(nvme_conf) - 1);
	if (opt->db_path != NULL && strlen(opt->db_path) > 0)
		strncpy(db_path, opt->db_path, ARRAY_SIZE(db_path) - 1);

	ddb_printf(ctx, "Using nvme config file: '%s' and smd db path: '%s'\n", nvme_conf, db_path);
	rc = dv_sync_smd(nvme_conf, db_path, sync_smd_cb, ctx);
	ddb_printf(ctx, "Done: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct dump_vea_cb_args {
	struct ddb_ctx *dva_ctx;
	uint32_t	dva_count;
};

static int
dump_vea_cb(void *cb_arg, struct dv_vea_free_extent *dvfe)
{
	struct dump_vea_cb_args *args = cb_arg;

	ddb_printf(args->dva_ctx, "[Region %d] offset: %lu, block count: %d, age: %d\n",
		   args->dva_count, dvfe->dvfe_block_offset, dvfe->dvfe_block_count,
		   dvfe->dvfe_age);

	args->dva_count++;
	return 0;
}

int
ddb_run_vea_dump(struct ddb_ctx *ctx)
{
	struct dump_vea_cb_args args = {.dva_ctx = ctx, .dva_count = 0};
	int			rc;

	rc = dv_enumerate_vea(ctx->dc_poh, dump_vea_cb, &args);

	ddb_printf(ctx, "Total Free Regions: %d\n", args.dva_count);

	return rc;
}

static int
parse_uint32_t(char *str)
{
	uint32_t result = atoi(str);
	char	 verify_str[32];

	snprintf(verify_str, ARRAY_SIZE(verify_str), "%d", result);

	if (strcmp(str, verify_str) == 0)
		return result;

	return -DER_INVAL;

}

struct update_vea_verify_region_cb_args {
	struct ddb_ctx           *ctx;
	struct dv_vea_free_extent potential_extent;
};

/**
 *
 * @param n - new extent to insert or update
 * @param e - existing extent
 * @return
 */
static bool
vfe_overlap(struct dv_vea_free_extent *n, struct dv_vea_free_extent *e)
{
	uint64_t a_lo = n->dvfe_block_offset;
	uint64_t a_hi = n->dvfe_block_offset + n->dvfe_block_count - 1;
	uint64_t b_lo = e->dvfe_block_offset;
	uint64_t b_hi = e->dvfe_block_offset + e->dvfe_block_count - 1;

	return !(a_hi < b_lo || a_lo > b_hi);
}

static int
update_vea_verify_region_cb(void *cb_arg, struct dv_vea_free_extent *vfe)
{
	struct update_vea_verify_region_cb_args *args = cb_arg;

	if (vfe_overlap(vfe, &args->potential_extent)) {
		ddb_errorf(args->ctx, "New free region {%lu, %d} overlaps with {%lu, %d}\n",
			   args->potential_extent.dvfe_block_offset,
			   args->potential_extent.dvfe_block_count, vfe->dvfe_block_offset,
			   vfe->dvfe_block_count);
		return -DER_INVAL;
	}

	return 0;
}

static int
verify_free(struct ddb_ctx *ctx, uint64_t offset, uint32_t blk_cnt)
{
	struct update_vea_verify_region_cb_args args = {0};

	args.potential_extent.dvfe_block_offset = offset;
	args.potential_extent.dvfe_block_count  = blk_cnt;
	args.ctx = ctx;
	return dv_enumerate_vea(ctx->dc_poh, update_vea_verify_region_cb, &args);
}

int
ddb_run_vea_update(struct ddb_ctx *ctx, struct vea_update_options *opt)
{
	uint64_t				offset;
	uint32_t				blk_cnt;
	int					rc;

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

	offset = parse_uint32_t(opt->offset);
	if (offset <= 0) {
		ddb_errorf(ctx, "'%s' is not a valid offset\n", opt->offset);
		return -DER_INVAL;
	}
	blk_cnt = parse_uint32_t(opt->blk_cnt);
	if (blk_cnt <= 0) {
		ddb_errorf(ctx, "'%s' is not a valid block size\n", opt->blk_cnt);
		return -DER_INVAL;
	}

	rc = verify_free(ctx, offset, blk_cnt);
	if (!SUCCESS(rc))
		return rc;

	ddb_printf(ctx, "Adding free region to vea {%lu, %d}\n", offset, blk_cnt);
	rc = dv_vea_free_region(ctx->dc_poh, offset, blk_cnt);
	if (!SUCCESS(rc))
		ddb_errorf(ctx, "Unable to add new free region: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/* Information used while modifying a dtx active entry */
struct dtx_modify_args {
	struct dv_tree_path_builder	 vtpb;
	struct dtx_id			 dti;
	daos_handle_t			 coh;
};

/* setup information needed for calling commit or abort active dtx entry */
static int
dtx_modify_init(struct ddb_ctx *ctx, char *path, char *dtx_id_str, struct dtx_modify_args *args)
{
	struct dv_tree_path	*vtp;
	int			 rc;

	rc = init_path(ctx->dc_poh, path, &args->vtpb);
	if (!SUCCESS(rc))
		D_GOTO(error, rc);
	vtp = &args->vtpb.vtp_path;

	vtp_print(ctx, vtp, true);

	if (!dv_has_cont(vtp)) {
		ddb_error(ctx, "Path to container is required\n");
		D_GOTO(error, rc = -DER_INVAL);
	}

	rc = dv_cont_open(ctx->dc_poh, vtp->vtp_cont, &args->coh);
	if (!SUCCESS(rc)) {
		ddb_errorf(ctx, "Unable to open container: "DF_RC"\n", DP_RC(rc));
		D_GOTO(error, rc);
	}

	rc = ddb_parse_dtx_id(dtx_id_str, &args->dti);
	if (!SUCCESS(rc)) {
		ddb_errorf(ctx, "Invalid dtx_id: %s\n", dtx_id_str);
		D_GOTO(error, rc);
	}
	return 0;

error:
	dv_cont_close(&args->coh);
	ddb_vtp_fini(&args->vtpb);
	return rc;
}

static void
dtx_modify_fini(struct dtx_modify_args *args)
{
	dv_cont_close(&args->coh);
	ddb_vtp_fini(&args->vtpb);
}

int
ddb_run_dtx_act_commit(struct ddb_ctx *ctx, struct dtx_act_commit_options *opt)
{
	struct dtx_modify_args	args = {0};
	int			rc;

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

	rc = dtx_modify_init(ctx, opt->path, opt->dtx_id, &args);
	if (!SUCCESS(rc))
		return rc;
	/* Marking entries as committed returns the number of entries committed */
	rc = dv_dtx_commit_active_entry(args.coh, &args.dti);
	if (rc < 0) {
		ddb_errorf(ctx, "Error marking entry as committed: "DF_RC"\n", DP_RC(rc));
	} else if (rc > 0) {
		ddb_print(ctx, "Entry marked as committed\n");
		rc = 0;
	} else {
		ddb_print(ctx, "No entry found to mark as committed\n");
	}

	dtx_modify_fini(&args);

	return rc;
}

int ddb_run_dtx_act_abort(struct ddb_ctx *ctx, struct dtx_act_abort_options *opt)
{
	struct dtx_modify_args	args = {0};
	int			rc;

	if (!ctx->dc_write_mode) {
		ddb_error(ctx, error_msg_write_mode_only);
		return -DER_INVAL;
	}

	rc = dtx_modify_init(ctx, opt->path, opt->dtx_id, &args);
	if (!SUCCESS(rc))
		return rc;

	rc = dv_dtx_abort_active_entry(args.coh, &args.dti);
	if (SUCCESS(rc)) {
		ddb_print(ctx, "Entry marked as aborted\n");
	} else if (rc == -DER_NONEXIST) {
		ddb_print(ctx, "No entry found to mark as aborted\n");
		rc = 0;
	} else {
		ddb_errorf(ctx, "Error marking entry as aborted: "DF_RC"\n", DP_RC(rc));
	}

	dtx_modify_fini(&args);
	return rc;
}
