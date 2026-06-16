//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build !test_stubs

package main

/*
 #cgo CFLAGS: -I${SRCDIR}/../../../utils/ddb/
 #cgo LDFLAGS: -lddb -lgurt

 #include <ddb.h>
*/
import "C"

func ddb_init() C.int                    { return C.ddb_init() }
func ddb_fini()                          { C.ddb_fini() }
func ddb_ctx_init(ctx *C.struct_ddb_ctx) { C.ddb_ctx_init(ctx) }

func ddb_pool_is_open(ctx *C.struct_ddb_ctx) C.bool {
	return C.ddb_pool_is_open(ctx)
}

func ddb_run_ls(ctx *C.struct_ddb_ctx, opts *C.struct_ls_options) C.int {
	return C.ddb_run_ls(ctx, opts)
}

func ddb_run_open(ctx *C.struct_ddb_ctx, opts *C.struct_open_options) C.int {
	return C.ddb_run_open(ctx, opts)
}

func ddb_run_version(ctx *C.struct_ddb_ctx) C.int {
	return C.ddb_run_version(ctx)
}

func ddb_run_close(ctx *C.struct_ddb_ctx) C.int {
	return C.ddb_run_close(ctx)
}

func ddb_run_superblock_dump(ctx *C.struct_ddb_ctx) C.int {
	return C.ddb_run_superblock_dump(ctx)
}

func ddb_run_value_dump(ctx *C.struct_ddb_ctx, opts *C.struct_value_dump_options) C.int {
	return C.ddb_run_value_dump(ctx, opts)
}

func ddb_run_rm(ctx *C.struct_ddb_ctx, opts *C.struct_rm_options) C.int {
	return C.ddb_run_rm(ctx, opts)
}

func ddb_run_value_load(ctx *C.struct_ddb_ctx, opts *C.struct_value_load_options) C.int {
	return C.ddb_run_value_load(ctx, opts)
}

func ddb_run_ilog_dump(ctx *C.struct_ddb_ctx, opts *C.struct_ilog_dump_options) C.int {
	return C.ddb_run_ilog_dump(ctx, opts)
}

func ddb_run_ilog_commit(ctx *C.struct_ddb_ctx, opts *C.struct_ilog_commit_options) C.int {
	return C.ddb_run_ilog_commit(ctx, opts)
}

func ddb_run_ilog_clear(ctx *C.struct_ddb_ctx, opts *C.struct_ilog_clear_options) C.int {
	return C.ddb_run_ilog_clear(ctx, opts)
}

func ddb_run_dtx_dump(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_dump_options) C.int {
	return C.ddb_run_dtx_dump(ctx, opts)
}

func ddb_run_dtx_cmt_clear(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_cmt_clear_options) C.int {
	return C.ddb_run_dtx_cmt_clear(ctx, opts)
}

func ddb_run_smd_sync(ctx *C.struct_ddb_ctx, opts *C.struct_smd_sync_options) C.int {
	return C.ddb_run_smd_sync(ctx, opts)
}

func ddb_run_vea_dump(ctx *C.struct_ddb_ctx) C.int {
	return C.ddb_run_vea_dump(ctx)
}

func ddb_run_vea_update(ctx *C.struct_ddb_ctx, opts *C.struct_vea_update_options) C.int {
	return C.ddb_run_vea_update(ctx, opts)
}

func ddb_run_dtx_act_commit(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_act_options) C.int {
	return C.ddb_run_dtx_act_commit(ctx, opts)
}

func ddb_run_dtx_act_abort(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_act_options) C.int {
	return C.ddb_run_dtx_act_abort(ctx, opts)
}

func ddb_feature_string2flags(ctx *C.struct_ddb_ctx, s *C.char, compat *C.uint64_t, incompat *C.uint64_t) C.int {
	return C.ddb_feature_string2flags(ctx, s, compat, incompat)
}

func ddb_run_feature(ctx *C.struct_ddb_ctx, opts *C.struct_feature_options) C.int {
	return C.ddb_run_feature(ctx, opts)
}

func ddb_run_rm_pool(ctx *C.struct_ddb_ctx, opts *C.struct_rm_pool_options) C.int {
	return C.ddb_run_rm_pool(ctx, opts)
}

func ddb_run_dtx_act_discard_invalid(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_act_options) C.int {
	return C.ddb_run_dtx_act_discard_invalid(ctx, opts)
}

func ddb_run_dev_list(ctx *C.struct_ddb_ctx, opts *C.struct_dev_list_options) C.int {
	return C.ddb_run_dev_list(ctx, opts)
}

func ddb_run_dev_replace(ctx *C.struct_ddb_ctx, opts *C.struct_dev_replace_options) C.int {
	return C.ddb_run_dev_replace(ctx, opts)
}

func ddb_run_dtx_stat(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_stat_options) C.int {
	return C.ddb_run_dtx_stat(ctx, opts)
}

func ddb_run_prov_mem(ctx *C.struct_ddb_ctx, opts *C.struct_prov_mem_options) C.int {
	return C.ddb_run_prov_mem(ctx, opts)
}

func ddb_run_dtx_aggr(ctx *C.struct_ddb_ctx, opts *C.struct_dtx_aggr_options) C.int {
	return C.ddb_run_dtx_aggr(ctx, opts)
}
