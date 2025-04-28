//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"runtime"
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
 #cgo CFLAGS: -I${SRCDIR}/../../../utils/ddb/
 #cgo LDFLAGS: -lddb -lgurt

 #include <ddb.h>
 #include <daos_errno.h>
*/
import "C"

func daosError(rc C.int) error {
	if rc != 0 {
		return daos.Status(rc)
	}
	return nil
}

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}

// InitDdb initializes the ddb context and returns a closure to finalize it.
func InitDdb() (*DdbContext, func(), error) {
	// Must lock to OS thread because vos init/fini uses ABT init and finalize which must be called on the same thread
	runtime.LockOSThread()

	if err := daosError(C.ddb_init()); err != nil {
		runtime.UnlockOSThread()
		return nil, nil, err
	}

	ctx := &DdbContext{}
	C.ddb_ctx_init(&ctx.ctx) // Initialize with ctx default values

	return ctx, func() {
		C.ddb_fini()
		runtime.UnlockOSThread()
	}, nil
}

// DdbContext structure for wrapping the C code context structure
type DdbContext struct {
	ctx C.struct_ddb_ctx
}

func ddbPoolIsOpen(ctx *DdbContext) bool {
	return bool(C.ddb_pool_is_open(&ctx.ctx))
}

func ddbLs(ctx *DdbContext, path string, recursive bool, details bool) error {
	/* Set up the options */
	options := C.struct_ls_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.recursive = C.bool(recursive)
	options.details = C.bool(details)
	/* Run the c code command */
	return daosError(C.ddb_run_ls(&ctx.ctx, &options))
}

func ddbOpen(ctx *DdbContext, path string, write_mode bool) error {
	/* Set up the options */
	options := C.struct_open_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.write_mode = C.bool(write_mode)
	/* Run the c code command */
	return daosError(C.ddb_run_open(&ctx.ctx, &options))
}

func ddbVersion(ctx *DdbContext) error {
	/* Run the c code command */
	return daosError(C.ddb_run_version(&ctx.ctx))
}

func ddbClose(ctx *DdbContext) error {
	/* Run the c code command */
	return daosError(C.ddb_run_close(&ctx.ctx))
}

func ddbSuperblockDump(ctx *DdbContext) error {
	/* Run the c code command */
	return daosError(C.ddb_run_superblock_dump(&ctx.ctx))
}

func ddbValueDump(ctx *DdbContext, path string, dst string) error {
	/* Set up the options */
	options := C.struct_value_dump_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dst = C.CString(dst)
	defer freeString(options.dst)
	/* Run the c code command */
	return daosError(C.ddb_run_value_dump(&ctx.ctx, &options))
}

func ddbRm(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_rm_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_rm(&ctx.ctx, &options))
}

func ddbValueLoad(ctx *DdbContext, src string, dst string) error {
	/* Set up the options */
	options := C.struct_value_load_options{}
	options.src = C.CString(src)
	defer freeString(options.src)
	options.dst = C.CString(dst)
	defer freeString(options.dst)
	/* Run the c code command */
	return daosError(C.ddb_run_value_load(&ctx.ctx, &options))
}

func ddbIlogDump(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_ilog_dump_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_ilog_dump(&ctx.ctx, &options))
}

func ddbIlogCommit(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_ilog_commit_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_ilog_commit(&ctx.ctx, &options))
}

func ddbIlogClear(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_ilog_clear_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_ilog_clear(&ctx.ctx, &options))
}

func ddbDtxDump(ctx *DdbContext, path string, active bool, committed bool) error {
	/* Set up the options */
	options := C.struct_dtx_dump_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.active = C.bool(active)
	options.committed = C.bool(committed)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_dump(&ctx.ctx, &options))
}

func ddbDtxCmtClear(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_dtx_cmt_clear_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_cmt_clear(&ctx.ctx, &options))
}

func ddbSmdSync(ctx *DdbContext, nvme_conf string, db_path string) error {
	/* Set up the options */
	options := C.struct_smd_sync_options{}
	options.nvme_conf = C.CString(nvme_conf)
	defer freeString(options.nvme_conf)
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	/* Run the c code command */
	return daosError(C.ddb_run_smd_sync(&ctx.ctx, &options))
}

func ddbVeaDump(ctx *DdbContext) error {
	/* Run the c code command */
	return daosError(C.ddb_run_vea_dump(&ctx.ctx))
}

func ddbVeaUpdate(ctx *DdbContext, offset string, blk_cnt string) error {
	/* Set up the options */
	options := C.struct_vea_update_options{}
	options.offset = C.CString(offset)
	defer freeString(options.offset)
	options.blk_cnt = C.CString(blk_cnt)
	defer freeString(options.blk_cnt)
	/* Run the c code command */
	return daosError(C.ddb_run_vea_update(&ctx.ctx, &options))
}

func ddbDtxActCommit(ctx *DdbContext, path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dtx_id = C.CString(dtx_id)
	defer freeString(options.dtx_id)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_act_commit(&ctx.ctx, &options))
}

func ddbDtxActAbort(ctx *DdbContext, path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dtx_id = C.CString(dtx_id)
	defer freeString(options.dtx_id)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_act_abort(&ctx.ctx, &options))
}

func ddbFeature(ctx *DdbContext, path, enable, disable string, show bool) error {
	/* Set up the options */
	options := C.struct_feature_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	if enable != "" {
		err := daosError(C.ddb_feature_string2flags(&ctx.ctx, C.CString(enable),
			&options.set_compat_flags, &options.set_incompat_flags))
		if err != nil {
			return err
		}
	}
	if disable != "" {
		err := daosError(C.ddb_feature_string2flags(&ctx.ctx, C.CString(disable),
			&options.clear_compat_flags, &options.clear_incompat_flags))
		if err != nil {
			return err
		}
	}
	options.show_features = C.bool(show)
	/* Run the c code command */
	return daosError(C.ddb_run_feature(&ctx.ctx, &options))
}

func ddbRmPool(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_rm_pool_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_rm_pool(&ctx.ctx, &options))
}

func ddbDtxActDiscardInvalid(ctx *DdbContext, path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dtx_id = C.CString(dtx_id)
	defer freeString(options.dtx_id)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_act_discard_invalid(&ctx.ctx, &options))
}

func ddbDevList(ctx *DdbContext, db_path string) error {
	/* Set up the options */
	options := C.struct_dev_list_options{}
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	/* Run the c code command */
	return daosError(C.ddb_run_dev_list(&ctx.ctx, &options))
}

func ddbDevReplace(ctx *DdbContext, db_path string, old_devid string, new_devid string) error {
	/* Set up the options */
	options := C.struct_dev_replace_options{}
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	options.old_devid = C.CString(old_devid)
	defer freeString(options.old_devid)
	options.new_devid = C.CString(new_devid)
	defer freeString(options.new_devid)
	/* Run the c code command */
	return daosError(C.ddb_run_dev_replace(&ctx.ctx, &options))
}
