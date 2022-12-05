//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"runtime"
	"unsafe"
)

/*
 #include <ddb.h>
 #include <daos_errno.h>
*/
import "C"

// DdbContext structure for wrapping the C code context structure
type DdbContext struct {
	ctx C.struct_ddb_ctx
}

// Init initialize DAOS and the context for doing ddb operations
func (ctx *DdbContext) Init() error {
	// Must lock to OS thread because vos init/fini uses ABT init and finalize which must be called on the same thread
	runtime.LockOSThread()

	result := C.ddb_init()

	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	C.ddb_ctx_init(&ctx.ctx) // Initialize with ctx default values

	return nil
}

// Fini clean up from Init
func (ctx *DdbContext) Fini() {
	C.ddb_fini()
	runtime.UnlockOSThread()
}

func errorString(errno int) string {
	dErrStr := C.GoString(C.d_errstr(C.int(errno)))
	dErrDesc := C.GoString(C.d_errdesc(C.int(errno)))
	return fmt.Sprintf("%s(%d): %s", dErrStr, errno, dErrDesc)
}

func ddbLs(ctx *DdbContext, path string, recursive bool) error {
	/* Set up the options */
	options := C.struct_ls_options{}
	options.path = C.CString(path)
	options.recursive = C.bool(recursive)
	/* Run the c code command */
	result := C.ddb_run_ls(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbOpen(ctx *DdbContext, path string, write_mode bool) error {
	/* Set up the options */
	options := C.struct_open_options{}
	options.path = C.CString(path)
	options.write_mode = C.bool(write_mode)
	/* Run the c code command */
	result := C.ddb_run_open(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbVersion(ctx *DdbContext) error {
	/* Run the c code command */
	result := C.ddb_run_version(&ctx.ctx)
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbClose(ctx *DdbContext) error {
	/* Run the c code command */
	result := C.ddb_run_close(&ctx.ctx)
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbSuperblockDump(ctx *DdbContext) error {
	/* Run the c code command */
	result := C.ddb_run_superblock_dump(&ctx.ctx)
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbValueDump(ctx *DdbContext, path string, dst string) error {
	/* Set up the options */
	options := C.struct_value_dump_options{}
	options.path = C.CString(path)
	options.dst = C.CString(dst)
	/* Run the c code command */
	result := C.ddb_run_value_dump(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dst))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbRm(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_rm_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_rm(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbValueLoad(ctx *DdbContext, src string, dst string) error {
	/* Set up the options */
	options := C.struct_value_load_options{}
	options.src = C.CString(src)
	options.dst = C.CString(dst)
	/* Run the c code command */
	result := C.ddb_run_value_load(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.src))
	C.free(unsafe.Pointer(options.dst))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbIlogDump(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_ilog_dump_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_ilog_dump(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbIlogCommit(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_ilog_commit_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_ilog_commit(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbIlogClear(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_ilog_clear_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_ilog_clear(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbDtxDump(ctx *DdbContext, path string, active bool, committed bool) error {
	/* Set up the options */
	options := C.struct_dtx_dump_options{}
	options.path = C.CString(path)
	options.active = C.bool(active)
	options.committed = C.bool(committed)
	/* Run the c code command */
	result := C.ddb_run_dtx_dump(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbDtxCmtClear(ctx *DdbContext, path string) error {
	/* Set up the options */
	options := C.struct_dtx_cmt_clear_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_dtx_cmt_clear(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbSmdSync(ctx *DdbContext, nvme_conf string, db_path string) error {
	/* Set up the options */
	options := C.struct_smd_sync_options{}
	options.nvme_conf = C.CString(nvme_conf)
	options.db_path = C.CString(db_path)
	/* Run the c code command */
	result := C.ddb_run_smd_sync(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.nvme_conf))
	C.free(unsafe.Pointer(options.db_path))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbVeaDump(ctx *DdbContext) error {
	/* Run the c code command */
	result := C.ddb_run_vea_dump(&ctx.ctx)
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbVeaUpdate(ctx *DdbContext, offset string, blk_cnt string) error {
	/* Set up the options */
	options := C.struct_vea_update_options{}
	options.offset = C.CString(offset)
	options.blk_cnt = C.CString(blk_cnt)
	/* Run the c code command */
	result := C.ddb_run_vea_update(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.offset))
	C.free(unsafe.Pointer(options.blk_cnt))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbDtxActCommit(ctx *DdbContext, path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_commit_options{}
	options.path = C.CString(path)
	options.dtx_id = C.CString(dtx_id)
	/* Run the c code command */
	result := C.ddb_run_dtx_act_commit(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dtx_id))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

func ddbDtxActAbort(ctx *DdbContext, path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_abort_options{}
	options.path = C.CString(path)
	options.dtx_id = C.CString(dtx_id)
	/* Run the c code command */
	result := C.ddb_run_dtx_act_abort(&ctx.ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dtx_id))
	if result != 0 {
		return fmt.Errorf(errorString(int(result)))
	}
	return nil
}

