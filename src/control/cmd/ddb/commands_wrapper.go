//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"unsafe"
)

/*
 #include <ddb.h>
 #include <daos_errno.h>
 */
import "C"

func ErrorString(errno int) string {
	dErrStr := C.GoString(C.d_errstr(C.int(errno)))
	dErrDesc := C.GoString(C.d_errdesc(C.int(errno)))
	return fmt.Sprintf("%s(%d): %s", dErrStr, errno, dErrDesc)
}

func ls_wrapper(ctx *C.struct_ddb_ctx, path string, recursive bool ) error {
	/* Setup the options */
	options := C.struct_ls_options{}
	options.path = C.CString(path)
	options.recursive = C.bool(recursive)
	/* Run the c code command */
	result := C.ddb_run_ls(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func open_wrapper(ctx *C.struct_ddb_ctx, path string, write_mode bool ) error {
	/* Setup the options */
	options := C.struct_open_options{}
	options.path = C.CString(path)
	options.write_mode = C.bool(write_mode)
	/* Run the c code command */
	result := C.ddb_run_open(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func close_wrapper(ctx *C.struct_ddb_ctx) error {
	/* Run the c code command */
	result := C.ddb_run_close(ctx)
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dump_superblock_wrapper(ctx *C.struct_ddb_ctx) error {
	/* Run the c code command */
	result := C.ddb_run_dump_superblock(ctx)
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dump_value_wrapper(ctx *C.struct_ddb_ctx, path string, dst string) error {
	/* Setup the options */
	options := C.struct_dump_value_options{}
	options.path = C.CString(path)
	options.dst = C.CString(dst)
	/* Run the c code command */
	result := C.ddb_run_dump_value(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dst))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func rm_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_rm_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_rm(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func load_wrapper(ctx *C.struct_ddb_ctx, src string, dst string) error {
	/* Setup the options */
	options := C.struct_load_options{}
	options.src = C.CString(src)
	options.dst = C.CString(dst)
	/* Run the c code command */
	result := C.ddb_run_load(ctx, &options)
	C.free(unsafe.Pointer(options.src))
	C.free(unsafe.Pointer(options.dst))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dump_ilog_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_dump_ilog_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_dump_ilog(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func commit_ilog_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_commit_ilog_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_commit_ilog(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func rm_ilog_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_rm_ilog_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_rm_ilog(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dump_dtx_wrapper(ctx *C.struct_ddb_ctx, path string, active bool , committed bool ) error {
	/* Setup the options */
	options := C.struct_dump_dtx_options{}
	options.path = C.CString(path)
	options.active = C.bool(active)
	options.committed = C.bool(committed)
	/* Run the c code command */
	result := C.ddb_run_dump_dtx(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func clear_cmt_dtx_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_clear_cmt_dtx_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_clear_cmt_dtx(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func smd_sync_wrapper(ctx *C.struct_ddb_ctx, nvme_conf string, db_path string) error {
	/* Setup the options */
	options := C.struct_smd_sync_options{}
	options.nvme_conf = C.CString(nvme_conf)
	options.db_path = C.CString(db_path)
	/* Run the c code command */
	result := C.ddb_run_smd_sync(ctx, &options)
	C.free(unsafe.Pointer(options.nvme_conf))
	C.free(unsafe.Pointer(options.db_path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dump_vea_wrapper(ctx *C.struct_ddb_ctx) error {
	/* Run the c code command */
	result := C.ddb_run_dump_vea(ctx)
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func update_vea_wrapper(ctx *C.struct_ddb_ctx, offset string, blk_cnt string) error {
	/* Setup the options */
	options := C.struct_update_vea_options{}
	options.offset = C.CString(offset)
	options.blk_cnt = C.CString(blk_cnt)
	/* Run the c code command */
	result := C.ddb_run_update_vea(ctx, &options)
	C.free(unsafe.Pointer(options.offset))
	C.free(unsafe.Pointer(options.blk_cnt))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dtx_commit_wrapper(ctx *C.struct_ddb_ctx, path string, dtx_id string) error {
	/* Setup the options */
	options := C.struct_dtx_commit_options{}
	options.path = C.CString(path)
	options.dtx_id = C.CString(dtx_id)
	/* Run the c code command */
	result := C.ddb_run_dtx_commit(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dtx_id))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dtx_abort_wrapper(ctx *C.struct_ddb_ctx, path string, dtx_id string) error {
	/* Setup the options */
	options := C.struct_dtx_abort_options{}
	options.path = C.CString(path)
	options.dtx_id = C.CString(dtx_id)
	/* Run the c code command */
	result := C.ddb_run_dtx_abort(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dtx_id))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

