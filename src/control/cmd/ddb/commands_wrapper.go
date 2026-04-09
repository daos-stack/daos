//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
// (C) Copyright 2025 Vdura Inc.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"math"
	"runtime"
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
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

type DdbApi interface {
	Init(log *logging.LeveledLogger) (func(), error)
	PoolIsOpen() bool
	Ls(path string, recursive bool, details bool) error
	Open(path string, db_path string, write_mode bool) error
	Version() error
	Close() error
	SuperblockDump() error
	ValueDump(path string, dst string) error
	Rm(path string) error
	ValueLoad(src string, dst string) error
	IlogDump(path string) error
	IlogCommit(path string) error
	IlogClear(path string) error
	DtxDump(path string, active bool, committed bool) error
	DtxCmtClear(path string) error
	SmdSync(nvme_conf string, db_path string) error
	VeaDump() error
	VeaUpdate(offset string, blk_cnt string) error
	DtxActCommit(path string, dtx_id string) error
	DtxActAbort(path string, dtx_id string) error
	Feature(path, db_path, enable, disable string, show bool) error
	RmPool(path string, db_path string) error
	DtxActDiscardInvalid(path string, dtx_id string) error
	DevList(db_path string) error
	DevReplace(db_path string, old_devid string, new_devid string) error
	DtxStat(path string, details bool) error
	ProvMem(db_path string, tmpfs_mount string, tmpfs_mount_size uint) error
	DtxAggr(path string, cmt_time uint64, cmt_date string) error
}

// DdbContext structure for wrapping the C code context structure
type DdbContext struct {
	ctx C.struct_ddb_ctx
	log *logging.LeveledLogger
}

// InitDdb initializes the ddb context and returns a closure to finalize it.
func (ctx *DdbContext) Init(log *logging.LeveledLogger) (func(), error) {
	// Must lock to OS thread because vos init/fini uses ABT init and finalize which must be called on the same thread
	runtime.LockOSThread()

	if err := daosError(C.ddb_init()); err != nil {
		runtime.UnlockOSThread()
		return nil, err
	}

	C.ddb_ctx_init(&ctx.ctx) // Initialize with ctx default values
	ctx.log = log

	return func() {
		C.ddb_fini()
		runtime.UnlockOSThread()
	}, nil
}

func (ctx *DdbContext) PoolIsOpen() bool {
	return bool(C.ddb_pool_is_open(&ctx.ctx))
}

func (ctx *DdbContext) Ls(path string, recursive bool, details bool) error {
	/* Set up the options */
	options := C.struct_ls_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.recursive = C.bool(recursive)
	options.details = C.bool(details)
	/* Run the c code command */
	return daosError(C.ddb_run_ls(&ctx.ctx, &options))
}

func (ctx *DdbContext) Open(path string, db_path string, write_mode bool) error {
	/* Set up the options */
	options := C.struct_open_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	options.write_mode = C.bool(write_mode)
	/* Run the c code command */
	return daosError(C.ddb_run_open(&ctx.ctx, &options))
}

func (ctx *DdbContext) Version() error {
	/* Run the c code command */
	return daosError(C.ddb_run_version(&ctx.ctx))
}

func (ctx *DdbContext) Close() error {
	/* Run the c code command */
	return daosError(C.ddb_run_close(&ctx.ctx))
}

func (ctx *DdbContext) SuperblockDump() error {
	/* Run the c code command */
	return daosError(C.ddb_run_superblock_dump(&ctx.ctx))
}

func (ctx *DdbContext) ValueDump(path string, dst string) error {
	/* Set up the options */
	options := C.struct_value_dump_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dst = C.CString(dst)
	defer freeString(options.dst)
	/* Run the c code command */
	return daosError(C.ddb_run_value_dump(&ctx.ctx, &options))
}

func (ctx *DdbContext) Rm(path string) error {
	/* Set up the options */
	options := C.struct_rm_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_rm(&ctx.ctx, &options))
}

func (ctx *DdbContext) ValueLoad(src string, dst string) error {
	/* Set up the options */
	options := C.struct_value_load_options{}
	options.src = C.CString(src)
	defer freeString(options.src)
	options.dst = C.CString(dst)
	defer freeString(options.dst)
	/* Run the c code command */
	return daosError(C.ddb_run_value_load(&ctx.ctx, &options))
}

func (ctx *DdbContext) IlogDump(path string) error {
	/* Set up the options */
	options := C.struct_ilog_dump_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_ilog_dump(&ctx.ctx, &options))
}

func (ctx *DdbContext) IlogCommit(path string) error {
	/* Set up the options */
	options := C.struct_ilog_commit_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_ilog_commit(&ctx.ctx, &options))
}

func (ctx *DdbContext) IlogClear(path string) error {
	/* Set up the options */
	options := C.struct_ilog_clear_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_ilog_clear(&ctx.ctx, &options))
}

func (ctx *DdbContext) DtxDump(path string, active bool, committed bool) error {
	/* Set up the options */
	options := C.struct_dtx_dump_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.active = C.bool(active)
	options.committed = C.bool(committed)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_dump(&ctx.ctx, &options))
}

func (ctx *DdbContext) DtxCmtClear(path string) error {
	/* Set up the options */
	options := C.struct_dtx_cmt_clear_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_cmt_clear(&ctx.ctx, &options))
}

func (ctx *DdbContext) SmdSync(nvme_conf string, db_path string) error {
	/* Set up the options */
	options := C.struct_smd_sync_options{}
	options.nvme_conf = C.CString(nvme_conf)
	defer freeString(options.nvme_conf)
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	/* Run the c code command */
	return daosError(C.ddb_run_smd_sync(&ctx.ctx, &options))
}

func (ctx *DdbContext) VeaDump() error {
	/* Run the c code command */
	return daosError(C.ddb_run_vea_dump(&ctx.ctx))
}

func (ctx *DdbContext) VeaUpdate(offset string, blk_cnt string) error {
	/* Set up the options */
	options := C.struct_vea_update_options{}
	options.offset = C.CString(offset)
	defer freeString(options.offset)
	options.blk_cnt = C.CString(blk_cnt)
	defer freeString(options.blk_cnt)
	/* Run the c code command */
	return daosError(C.ddb_run_vea_update(&ctx.ctx, &options))
}

func (ctx *DdbContext) DtxActCommit(path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dtx_id = C.CString(dtx_id)
	defer freeString(options.dtx_id)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_act_commit(&ctx.ctx, &options))
}

func (ctx *DdbContext) DtxActAbort(path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dtx_id = C.CString(dtx_id)
	defer freeString(options.dtx_id)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_act_abort(&ctx.ctx, &options))
}

func (ctx *DdbContext) Feature(path, db_path, enable, disable string, show bool) error {
	/* Set up the options */
	options := C.struct_feature_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
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

func (ctx *DdbContext) RmPool(path string, db_path string) error {
	/* Set up the options */
	options := C.struct_rm_pool_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	/* Run the c code command */
	return daosError(C.ddb_run_rm_pool(&ctx.ctx, &options))
}

func (ctx *DdbContext) DtxActDiscardInvalid(path string, dtx_id string) error {
	/* Set up the options */
	options := C.struct_dtx_act_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	options.dtx_id = C.CString(dtx_id)
	defer freeString(options.dtx_id)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_act_discard_invalid(&ctx.ctx, &options))
}

func (ctx *DdbContext) DevList(db_path string) error {
	/* Set up the options */
	options := C.struct_dev_list_options{}
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	/* Run the c code command */
	return daosError(C.ddb_run_dev_list(&ctx.ctx, &options))
}

func (ctx *DdbContext) DevReplace(db_path string, old_devid string, new_devid string) error {
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

func (ctx *DdbContext) DtxStat(path string, details bool) error {
	/* Set up the options */
	options := C.struct_dtx_stat_options{}
	options.path = C.CString(path)
	options.details = C.bool(details)
	defer freeString(options.path)
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_stat(&ctx.ctx, &options))
}

func (ctx *DdbContext) ProvMem(db_path string, tmpfs_mount string, tmpfs_mount_size uint) error {
	/* Set up the options */
	options := C.struct_prov_mem_options{}
	options.db_path = C.CString(db_path)
	defer freeString(options.db_path)
	options.tmpfs_mount = C.CString(tmpfs_mount)
	defer freeString(options.tmpfs_mount)

	options.tmpfs_mount_size = C.uint(tmpfs_mount_size)
	/* Run the c code command */
	return daosError(C.ddb_run_prov_mem(&ctx.ctx, &options))
}

func (ctx *DdbContext) DtxAggr(path string, cmt_time uint64, cmt_date string) error {
	if cmt_time != math.MaxUint64 && cmt_date != "" {
		ctx.log.Error("'--cmt_time' and '--cmt_date' options are mutually exclusive")
		return daosError(-C.DER_INVAL)
	}
	if cmt_time == math.MaxUint64 && cmt_date == "" {
		ctx.log.Error("'--cmt_time' or '--cmt_date' option has to be defined")
		return daosError(-C.DER_INVAL)
	}

	/* Set up the options */
	options := C.struct_dtx_aggr_options{}
	options.path = C.CString(path)
	defer freeString(options.path)
	if cmt_time != math.MaxUint64 {
		options.format = C.DDB_DTX_AGGR_CMT_TIME
		options.cmt_time = C.uint64_t(cmt_time)
	}
	if cmt_date != "" {
		options.format = C.DDB_DTX_AGGR_CMT_DATE
		options.cmt_date = C.CString(cmt_date)
		defer freeString(options.cmt_date)
	}
	/* Run the c code command */
	return daosError(C.ddb_run_dtx_aggr(&ctx.ctx, &options))
}
