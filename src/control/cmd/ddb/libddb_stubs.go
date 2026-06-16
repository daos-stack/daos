//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

package main

/*
 #cgo CFLAGS: -I${SRCDIR}/../../../utils/ddb/

 #include <ddb.h>
*/
import "C"

// fromGoErr converts a Go error to a C.int return code.
// Non-nil errors are mapped to -1 (generic failure) so that daosError() will
// propagate a non-nil error back to the caller.
func fromGoErr(err error) C.int {
	if err == nil {
		return 0
	}
	return -1
}

// resetDdbStubs resets all stub variables to their zero values.
// Call this at the start of each test (via newTestContext) to ensure isolation.
func resetDdbStubs() {
	ddb_init_RC = 0

	ddb_pool_is_open_RC = false

	ddb_run_ls_RC, ddb_run_ls_Fn = 0, nil
	ddb_run_open_RC, ddb_run_open_Fn = 0, nil
	ddb_run_version_RC, ddb_run_version_Fn = 0, nil
	ddb_run_close_RC, ddb_run_close_Fn = 0, nil
	ddb_run_superblock_dump_RC, ddb_run_superblock_dump_Fn = 0, nil
	ddb_run_value_dump_RC, ddb_run_value_dump_Fn = 0, nil
	ddb_run_rm_RC, ddb_run_rm_Fn = 0, nil
	ddb_run_value_load_RC, ddb_run_value_load_Fn = 0, nil
	ddb_run_ilog_dump_RC, ddb_run_ilog_dump_Fn = 0, nil
	ddb_run_ilog_commit_RC, ddb_run_ilog_commit_Fn = 0, nil
	ddb_run_ilog_clear_RC, ddb_run_ilog_clear_Fn = 0, nil
	ddb_run_dtx_dump_RC, ddb_run_dtx_dump_Fn = 0, nil
	ddb_run_dtx_cmt_clear_RC, ddb_run_dtx_cmt_clear_Fn = 0, nil
	ddb_run_smd_sync_RC, ddb_run_smd_sync_Fn = 0, nil
	ddb_run_vea_dump_RC, ddb_run_vea_dump_Fn = 0, nil
	ddb_run_vea_update_RC, ddb_run_vea_update_Fn = 0, nil
	ddb_run_dtx_act_commit_RC, ddb_run_dtx_act_commit_Fn = 0, nil
	ddb_run_dtx_act_abort_RC, ddb_run_dtx_act_abort_Fn = 0, nil
	ddb_feature_string2flags_RC, ddb_feature_string2flags_Fn = 0, nil
	ddb_run_feature_RC, ddb_run_feature_Fn = 0, nil
	ddb_run_rm_pool_RC, ddb_run_rm_pool_Fn = 0, nil
	ddb_run_dtx_act_discard_invalid_RC, ddb_run_dtx_act_discard_invalid_Fn = 0, nil
	ddb_run_dev_list_RC, ddb_run_dev_list_Fn = 0, nil
	ddb_run_dev_replace_RC, ddb_run_dev_replace_Fn = 0, nil
	ddb_run_dtx_stat_RC, ddb_run_dtx_stat_Fn = 0, nil
	ddb_run_prov_mem_RC, ddb_run_prov_mem_Fn = 0, nil
	ddb_run_dtx_aggr_RC, ddb_run_dtx_aggr_Fn = 0, nil
}

var ddb_init_RC C.int = 0

func ddb_init() C.int                  { return ddb_init_RC }
func ddb_fini()                        {}
func ddb_ctx_init(_ *C.struct_ddb_ctx) {}

var ddb_pool_is_open_RC = false

func ddb_pool_is_open(_ *C.struct_ddb_ctx) C.bool {
	return C.bool(ddb_pool_is_open_RC)
}

var (
	ddb_run_ls_RC C.int = 0
	ddb_run_ls_Fn func(path string, recursive bool, details bool) error
)

func ddb_run_ls(_ *C.struct_ddb_ctx, opts *C.struct_ls_options) C.int {
	if ddb_run_ls_Fn != nil {
		return fromGoErr(ddb_run_ls_Fn(
			C.GoString(opts.path),
			bool(opts.recursive),
			bool(opts.details),
		))
	}
	return ddb_run_ls_RC
}

var (
	ddb_run_open_RC C.int = 0
	ddb_run_open_Fn func(path string, dbPath string, writeMode bool) error
)

func ddb_run_open(_ *C.struct_ddb_ctx, opts *C.struct_open_options) C.int {
	if ddb_run_open_Fn != nil {
		return fromGoErr(ddb_run_open_Fn(
			C.GoString(opts.path),
			C.GoString(opts.db_path),
			bool(opts.write_mode),
		))
	}
	return ddb_run_open_RC
}

var (
	ddb_run_version_RC C.int = 0
	ddb_run_version_Fn func() error
)

func ddb_run_version(_ *C.struct_ddb_ctx) C.int {
	if ddb_run_version_Fn != nil {
		return fromGoErr(ddb_run_version_Fn())
	}
	return ddb_run_version_RC
}

var (
	ddb_run_close_RC C.int = 0
	ddb_run_close_Fn func() error
)

func ddb_run_close(_ *C.struct_ddb_ctx) C.int {
	if ddb_run_close_Fn != nil {
		return fromGoErr(ddb_run_close_Fn())
	}
	return ddb_run_close_RC
}

var (
	ddb_run_superblock_dump_RC C.int = 0
	ddb_run_superblock_dump_Fn func() error
)

func ddb_run_superblock_dump(_ *C.struct_ddb_ctx) C.int {
	if ddb_run_superblock_dump_Fn != nil {
		return fromGoErr(ddb_run_superblock_dump_Fn())
	}
	return ddb_run_superblock_dump_RC
}

var (
	ddb_run_value_dump_RC C.int = 0
	ddb_run_value_dump_Fn func(path string, dst string) error
)

func ddb_run_value_dump(_ *C.struct_ddb_ctx, opts *C.struct_value_dump_options) C.int {
	if ddb_run_value_dump_Fn != nil {
		return fromGoErr(ddb_run_value_dump_Fn(
			C.GoString(opts.path),
			C.GoString(opts.dst),
		))
	}
	return ddb_run_value_dump_RC
}

var (
	ddb_run_rm_RC C.int = 0
	ddb_run_rm_Fn func(path string) error
)

func ddb_run_rm(_ *C.struct_ddb_ctx, opts *C.struct_rm_options) C.int {
	if ddb_run_rm_Fn != nil {
		return fromGoErr(ddb_run_rm_Fn(C.GoString(opts.path)))
	}
	return ddb_run_rm_RC
}

var (
	ddb_run_value_load_RC C.int = 0
	ddb_run_value_load_Fn func(src string, dst string) error
)

func ddb_run_value_load(_ *C.struct_ddb_ctx, opts *C.struct_value_load_options) C.int {
	if ddb_run_value_load_Fn != nil {
		return fromGoErr(ddb_run_value_load_Fn(
			C.GoString(opts.src),
			C.GoString(opts.dst),
		))
	}
	return ddb_run_value_load_RC
}

var (
	ddb_run_ilog_dump_RC C.int = 0
	ddb_run_ilog_dump_Fn func(path string) error
)

func ddb_run_ilog_dump(_ *C.struct_ddb_ctx, opts *C.struct_ilog_dump_options) C.int {
	if ddb_run_ilog_dump_Fn != nil {
		return fromGoErr(ddb_run_ilog_dump_Fn(C.GoString(opts.path)))
	}
	return ddb_run_ilog_dump_RC
}

var (
	ddb_run_ilog_commit_RC C.int = 0
	ddb_run_ilog_commit_Fn func(path string) error
)

func ddb_run_ilog_commit(_ *C.struct_ddb_ctx, opts *C.struct_ilog_commit_options) C.int {
	if ddb_run_ilog_commit_Fn != nil {
		return fromGoErr(ddb_run_ilog_commit_Fn(C.GoString(opts.path)))
	}
	return ddb_run_ilog_commit_RC
}

var (
	ddb_run_ilog_clear_RC C.int = 0
	ddb_run_ilog_clear_Fn func(path string) error
)

func ddb_run_ilog_clear(_ *C.struct_ddb_ctx, opts *C.struct_ilog_clear_options) C.int {
	if ddb_run_ilog_clear_Fn != nil {
		return fromGoErr(ddb_run_ilog_clear_Fn(C.GoString(opts.path)))
	}
	return ddb_run_ilog_clear_RC
}

var (
	ddb_run_dtx_dump_RC C.int = 0
	ddb_run_dtx_dump_Fn func(path string, active bool, committed bool) error
)

func ddb_run_dtx_dump(_ *C.struct_ddb_ctx, opts *C.struct_dtx_dump_options) C.int {
	if ddb_run_dtx_dump_Fn != nil {
		return fromGoErr(ddb_run_dtx_dump_Fn(
			C.GoString(opts.path),
			bool(opts.active),
			bool(opts.committed),
		))
	}
	return ddb_run_dtx_dump_RC
}

var (
	ddb_run_dtx_cmt_clear_RC C.int = 0
	ddb_run_dtx_cmt_clear_Fn func(path string) error
)

func ddb_run_dtx_cmt_clear(_ *C.struct_ddb_ctx, opts *C.struct_dtx_cmt_clear_options) C.int {
	if ddb_run_dtx_cmt_clear_Fn != nil {
		return fromGoErr(ddb_run_dtx_cmt_clear_Fn(C.GoString(opts.path)))
	}
	return ddb_run_dtx_cmt_clear_RC
}

var (
	ddb_run_smd_sync_RC C.int = 0
	ddb_run_smd_sync_Fn func(nvmeConf string, dbPath string) error
)

func ddb_run_smd_sync(_ *C.struct_ddb_ctx, opts *C.struct_smd_sync_options) C.int {
	if ddb_run_smd_sync_Fn != nil {
		return fromGoErr(ddb_run_smd_sync_Fn(
			C.GoString(opts.nvme_conf),
			C.GoString(opts.db_path),
		))
	}
	return ddb_run_smd_sync_RC
}

var (
	ddb_run_vea_dump_RC C.int = 0
	ddb_run_vea_dump_Fn func() error
)

func ddb_run_vea_dump(_ *C.struct_ddb_ctx) C.int {
	if ddb_run_vea_dump_Fn != nil {
		return fromGoErr(ddb_run_vea_dump_Fn())
	}
	return ddb_run_vea_dump_RC
}

var (
	ddb_run_vea_update_RC C.int = 0
	ddb_run_vea_update_Fn func(offset string, blkCnt string) error
)

func ddb_run_vea_update(_ *C.struct_ddb_ctx, opts *C.struct_vea_update_options) C.int {
	if ddb_run_vea_update_Fn != nil {
		return fromGoErr(ddb_run_vea_update_Fn(
			C.GoString(opts.offset),
			C.GoString(opts.blk_cnt),
		))
	}
	return ddb_run_vea_update_RC
}

var (
	ddb_run_dtx_act_commit_RC C.int = 0
	ddb_run_dtx_act_commit_Fn func(path string, dtxID string) error
)

func ddb_run_dtx_act_commit(_ *C.struct_ddb_ctx, opts *C.struct_dtx_act_options) C.int {
	if ddb_run_dtx_act_commit_Fn != nil {
		return fromGoErr(ddb_run_dtx_act_commit_Fn(
			C.GoString(opts.path),
			C.GoString(opts.dtx_id),
		))
	}
	return ddb_run_dtx_act_commit_RC
}

var (
	ddb_run_dtx_act_abort_RC C.int = 0
	ddb_run_dtx_act_abort_Fn func(path string, dtxID string) error
)

func ddb_run_dtx_act_abort(_ *C.struct_ddb_ctx, opts *C.struct_dtx_act_options) C.int {
	if ddb_run_dtx_act_abort_Fn != nil {
		return fromGoErr(ddb_run_dtx_act_abort_Fn(
			C.GoString(opts.path),
			C.GoString(opts.dtx_id),
		))
	}
	return ddb_run_dtx_act_abort_RC
}

var (
	ddb_feature_string2flags_RC C.int = 0
	ddb_feature_string2flags_Fn func(s string) (compat uint64, incompat uint64, err error)
)

func ddb_feature_string2flags(_ *C.struct_ddb_ctx, s *C.char, compat *C.uint64_t, incompat *C.uint64_t) C.int {
	if ddb_feature_string2flags_Fn != nil {
		c, ic, err := ddb_feature_string2flags_Fn(C.GoString(s))
		if err != nil {
			return fromGoErr(err)
		}
		*compat = C.uint64_t(c)
		*incompat = C.uint64_t(ic)
		return 0
	}
	return ddb_feature_string2flags_RC
}

var (
	ddb_run_feature_RC C.int = 0
	ddb_run_feature_Fn func(path, dbPath, enable, disable string, show bool) error
)

func ddb_run_feature(_ *C.struct_ddb_ctx, opts *C.struct_feature_options) C.int {
	if ddb_run_feature_Fn != nil {
		return fromGoErr(ddb_run_feature_Fn(
			C.GoString(opts.path),
			C.GoString(opts.db_path),
			"", "",
			bool(opts.show_features),
		))
	}
	return ddb_run_feature_RC
}

var (
	ddb_run_rm_pool_RC C.int = 0
	ddb_run_rm_pool_Fn func(path string, dbPath string) error
)

func ddb_run_rm_pool(_ *C.struct_ddb_ctx, opts *C.struct_rm_pool_options) C.int {
	if ddb_run_rm_pool_Fn != nil {
		return fromGoErr(ddb_run_rm_pool_Fn(
			C.GoString(opts.path),
			C.GoString(opts.db_path),
		))
	}
	return ddb_run_rm_pool_RC
}

var (
	ddb_run_dtx_act_discard_invalid_RC C.int = 0
	ddb_run_dtx_act_discard_invalid_Fn func(path string, dtxID string) error
)

func ddb_run_dtx_act_discard_invalid(_ *C.struct_ddb_ctx, opts *C.struct_dtx_act_options) C.int {
	if ddb_run_dtx_act_discard_invalid_Fn != nil {
		return fromGoErr(ddb_run_dtx_act_discard_invalid_Fn(
			C.GoString(opts.path),
			C.GoString(opts.dtx_id),
		))
	}
	return ddb_run_dtx_act_discard_invalid_RC
}

var (
	ddb_run_dev_list_RC C.int = 0
	ddb_run_dev_list_Fn func(dbPath string) error
)

func ddb_run_dev_list(_ *C.struct_ddb_ctx, opts *C.struct_dev_list_options) C.int {
	if ddb_run_dev_list_Fn != nil {
		return fromGoErr(ddb_run_dev_list_Fn(C.GoString(opts.db_path)))
	}
	return ddb_run_dev_list_RC
}

var (
	ddb_run_dev_replace_RC C.int = 0
	ddb_run_dev_replace_Fn func(dbPath string, oldDevid string, newDevid string) error
)

func ddb_run_dev_replace(_ *C.struct_ddb_ctx, opts *C.struct_dev_replace_options) C.int {
	if ddb_run_dev_replace_Fn != nil {
		return fromGoErr(ddb_run_dev_replace_Fn(
			C.GoString(opts.db_path),
			C.GoString(opts.old_devid),
			C.GoString(opts.new_devid),
		))
	}
	return ddb_run_dev_replace_RC
}

var (
	ddb_run_dtx_stat_RC C.int = 0
	ddb_run_dtx_stat_Fn func(path string, details bool) error
)

func ddb_run_dtx_stat(_ *C.struct_ddb_ctx, opts *C.struct_dtx_stat_options) C.int {
	if ddb_run_dtx_stat_Fn != nil {
		return fromGoErr(ddb_run_dtx_stat_Fn(
			C.GoString(opts.path),
			bool(opts.details),
		))
	}
	return ddb_run_dtx_stat_RC
}

var (
	ddb_run_prov_mem_RC C.int = 0
	ddb_run_prov_mem_Fn func(dbPath string, tmpfsMount string, tmpfsMountSize uint) error
)

func ddb_run_prov_mem(_ *C.struct_ddb_ctx, opts *C.struct_prov_mem_options) C.int {
	if ddb_run_prov_mem_Fn != nil {
		return fromGoErr(ddb_run_prov_mem_Fn(
			C.GoString(opts.db_path),
			C.GoString(opts.tmpfs_mount),
			uint(opts.tmpfs_mount_size),
		))
	}
	return ddb_run_prov_mem_RC
}

var (
	ddb_run_dtx_aggr_RC C.int = 0
	ddb_run_dtx_aggr_Fn func(path string, cmtTime uint64, cmtDate string) error
)

func ddb_run_dtx_aggr(_ *C.struct_ddb_ctx, opts *C.struct_dtx_aggr_options) C.int {
	if ddb_run_dtx_aggr_Fn != nil {
		return fromGoErr(ddb_run_dtx_aggr_Fn(
			C.GoString(opts.path),
			uint64(opts.cmt_time),
			C.GoString(opts.cmt_date),
		))
	}
	return ddb_run_dtx_aggr_RC
}
