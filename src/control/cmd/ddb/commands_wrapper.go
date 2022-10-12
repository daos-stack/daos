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

func version_wrapper(ctx *C.struct_ddb_ctx) error {
	/* Run the c code command */
	result := C.ddb_run_version(ctx)
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

func superblock_dump_wrapper(ctx *C.struct_ddb_ctx) error {
	/* Run the c code command */
	result := C.ddb_run_superblock_dump(ctx)
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func value_dump_wrapper(ctx *C.struct_ddb_ctx, path string, dst string) error {
	/* Setup the options */
	options := C.struct_value_dump_options{}
	options.path = C.CString(path)
	options.dst = C.CString(dst)
	/* Run the c code command */
	result := C.ddb_run_value_dump(ctx, &options)
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

func value_load_wrapper(ctx *C.struct_ddb_ctx, src string, dst string) error {
	/* Setup the options */
	options := C.struct_value_load_options{}
	options.src = C.CString(src)
	options.dst = C.CString(dst)
	/* Run the c code command */
	result := C.ddb_run_value_load(ctx, &options)
	C.free(unsafe.Pointer(options.src))
	C.free(unsafe.Pointer(options.dst))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func ilog_dump_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_ilog_dump_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_ilog_dump(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func ilog_commit_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_ilog_commit_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_ilog_commit(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func ilog_clear_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_ilog_clear_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_ilog_clear(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dtx_dump_wrapper(ctx *C.struct_ddb_ctx, path string, active bool , committed bool ) error {
	/* Setup the options */
	options := C.struct_dtx_dump_options{}
	options.path = C.CString(path)
	options.active = C.bool(active)
	options.committed = C.bool(committed)
	/* Run the c code command */
	result := C.ddb_run_dtx_dump(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dtx_cmt_clear_wrapper(ctx *C.struct_ddb_ctx, path string) error {
	/* Setup the options */
	options := C.struct_dtx_cmt_clear_options{}
	options.path = C.CString(path)
	/* Run the c code command */
	result := C.ddb_run_dtx_cmt_clear(ctx, &options)
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

func vea_dump_wrapper(ctx *C.struct_ddb_ctx) error {
	/* Run the c code command */
	result := C.ddb_run_vea_dump(ctx)
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func vea_update_wrapper(ctx *C.struct_ddb_ctx, offset string, blk_cnt string) error {
	/* Setup the options */
	options := C.struct_vea_update_options{}
	options.offset = C.CString(offset)
	options.blk_cnt = C.CString(blk_cnt)
	/* Run the c code command */
	result := C.ddb_run_vea_update(ctx, &options)
	C.free(unsafe.Pointer(options.offset))
	C.free(unsafe.Pointer(options.blk_cnt))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dtx_act_commit_wrapper(ctx *C.struct_ddb_ctx, path string, dtx_id string) error {
	/* Setup the options */
	options := C.struct_dtx_act_commit_options{}
	options.path = C.CString(path)
	options.dtx_id = C.CString(dtx_id)
	/* Run the c code command */
	result := C.ddb_run_dtx_act_commit(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dtx_id))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

func dtx_act_abort_wrapper(ctx *C.struct_ddb_ctx, path string, dtx_id string) error {
	/* Setup the options */
	options := C.struct_dtx_act_abort_options{}
	options.path = C.CString(path)
	options.dtx_id = C.CString(dtx_id)
	/* Run the c code command */
	result := C.ddb_run_dtx_act_abort(ctx, &options)
	C.free(unsafe.Pointer(options.path))
	C.free(unsafe.Pointer(options.dtx_id))
	if result != 0 {
		return fmt.Errorf(ErrorString(int(result)))
	}
	return nil
}

