package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"reflect"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

type ddbTestErr string

func (dte ddbTestErr) Error() string {
	return string(dte)
}

const (
	errUnknownCmd = ddbTestErr(grumbleUnknownCmdErr)
)

type DdbContextStub struct {
	init                 func(log *logging.LeveledLogger) (func(), error)
	poolIsOpen           func() bool
	ls                   func(path string, recursive bool, details bool) error
	open                 func(path string, db_path string, write_mode bool) error
	version              func() error
	close                func() error
	superblockDump       func() error
	valueDump            func(path string, dst string) error
	rm                   func(path string) error
	valueLoad            func(src string, dst string) error
	ilogDump             func(path string) error
	ilogCommit           func(path string) error
	ilogClear            func(path string) error
	dtxDump              func(path string, active bool, committed bool) error
	dtxCmtClear          func(path string) error
	smdSync              func(nvme_conf string, db_path string) error
	veaDump              func() error
	veaUpdate            func(offset string, blk_cnt string) error
	dtxActCommit         func(path string, dtx_id string) error
	dtxActAbort          func(path string, dtx_id string) error
	feature              func(path, db_path, enable, disable string, show bool) error
	rmPool               func(path string, db_path string) error
	dtxActDiscardInvalid func(path string, dtx_id string) error
	devList              func(db_path string) error
	devReplace           func(db_path string, old_devid string, new_devid string) error
	dtxStat              func(path string, details bool) error
	provMem              func(db_path string, tmpfs_mount string, tmpfs_mount_size uint) error
	dtxAggr              func(path string, cmt_time uint64, cmt_date string) error
}

func (ctx *DdbContextStub) Init(log *logging.LeveledLogger) (func(), error) {
	if ctx.init == nil {
		return func() {}, nil
	}
	return ctx.init(log)
}

func (ctx *DdbContextStub) PoolIsOpen() bool {
	if ctx.poolIsOpen == nil {
		return false
	}
	return ctx.poolIsOpen()
}

func (ctx *DdbContextStub) Ls(path string, recursive bool, details bool) error {
	if ctx.ls == nil {
		return nil
	}
	return ctx.ls(path, recursive, details)
}

func (ctx *DdbContextStub) Open(path string, db_path string, write_mode bool) error {
	if ctx.open == nil {
		return nil
	}
	return ctx.open(path, db_path, write_mode)
}

func (ctx *DdbContextStub) Version() error {
	if ctx.version == nil {
		return nil
	}
	return ctx.version()
}

func (ctx *DdbContextStub) Close() error {
	if ctx.close == nil {
		return nil
	}
	return ctx.close()
}

func (ctx *DdbContextStub) SuperblockDump() error {
	if ctx.superblockDump == nil {
		return nil
	}
	return ctx.superblockDump()
}

func (ctx *DdbContextStub) ValueDump(path string, dst string) error {
	if ctx.valueDump == nil {
		return nil
	}
	return ctx.valueDump(path, dst)
}

func (ctx *DdbContextStub) Rm(path string) error {
	if ctx.rm == nil {
		return nil
	}
	return ctx.rm(path)
}

func (ctx *DdbContextStub) ValueLoad(src string, dst string) error {
	if ctx.valueLoad == nil {
		return nil
	}
	return ctx.valueLoad(src, dst)
}

func (ctx *DdbContextStub) IlogDump(path string) error {
	if ctx.ilogDump == nil {
		return nil
	}
	return ctx.ilogDump(path)
}

func (ctx *DdbContextStub) IlogCommit(path string) error {
	if ctx.ilogCommit == nil {
		return nil
	}
	return ctx.ilogCommit(path)
}

func (ctx *DdbContextStub) IlogClear(path string) error {
	if ctx.ilogClear == nil {
		return nil
	}
	return ctx.ilogClear(path)
}

func (ctx *DdbContextStub) DtxDump(path string, active bool, committed bool) error {
	if ctx.dtxDump == nil {
		return nil
	}
	return ctx.dtxDump(path, active, committed)
}

func (ctx *DdbContextStub) DtxCmtClear(path string) error {
	if ctx.dtxCmtClear == nil {
		return nil
	}
	return ctx.dtxCmtClear(path)
}

func (ctx *DdbContextStub) SmdSync(nvme_conf string, db_path string) error {
	if ctx.smdSync == nil {
		return nil
	}
	return ctx.smdSync(nvme_conf, db_path)
}

func (ctx *DdbContextStub) VeaDump() error {
	if ctx.veaDump == nil {
		return nil
	}
	return ctx.veaDump()
}

func (ctx *DdbContextStub) VeaUpdate(offset string, blk_cnt string) error {
	if ctx.veaUpdate == nil {
		return nil
	}
	return ctx.veaUpdate(offset, blk_cnt)
}

func (ctx *DdbContextStub) DtxActCommit(path string, dtx_id string) error {
	if ctx.dtxActCommit == nil {
		return nil
	}
	return ctx.dtxActCommit(path, dtx_id)
}

func (ctx *DdbContextStub) DtxActAbort(path string, dtx_id string) error {
	if ctx.dtxActAbort == nil {
		return nil
	}
	return ctx.dtxActAbort(path, dtx_id)
}

func (ctx *DdbContextStub) Feature(path, db_path, enable, disable string, show bool) error {
	if ctx.feature == nil {
		return nil
	}
	return ctx.feature(path, db_path, enable, disable, show)
}

func (ctx *DdbContextStub) RmPool(path string, db_path string) error {
	if ctx.rmPool == nil {
		return nil
	}
	return ctx.rmPool(path, db_path)
}

func (ctx *DdbContextStub) DtxActDiscardInvalid(path string, dtx_id string) error {
	if ctx.dtxActDiscardInvalid == nil {
		return nil
	}
	return ctx.dtxActDiscardInvalid(path, dtx_id)
}

func (ctx *DdbContextStub) DevList(db_path string) error {
	if ctx.devList == nil {
		return nil
	}
	return ctx.devList(db_path)
}

func (ctx *DdbContextStub) DevReplace(db_path string, old_devid string, new_devid string) error {
	if ctx.devReplace == nil {
		return nil
	}
	return ctx.devReplace(db_path, old_devid, new_devid)
}

func (ctx *DdbContextStub) DtxStat(path string, details bool) error {
	if ctx.dtxStat == nil {
		return nil
	}
	return ctx.dtxStat(path, details)
}

func (ctx *DdbContextStub) ProvMem(db_path string, tmpfs_mount string, tmpfs_mount_size uint) error {
	if ctx.provMem == nil {
		return nil
	}
	return ctx.provMem(db_path, tmpfs_mount, tmpfs_mount_size)
}

func (ctx *DdbContextStub) DtxAggr(path string, cmt_time uint64, cmt_date string) error {
	if ctx.dtxAggr == nil {
		return nil
	}
	return ctx.dtxAggr(path, cmt_time, cmt_date)
}

func runCmdToStdout(log *logging.LeveledLogger, ctx *DdbContextStub, opts *cliOptions, args []string) (string, error) {
	// replace os.Stdout so that we can verify the generated output
	var result bytes.Buffer
	r, w, _ := os.Pipe()
	done := make(chan struct{})
	go func() {
		_, _ = io.Copy(&result, r)
		close(done)
	}()
	stdout := os.Stdout
	defer func() { os.Stdout = stdout }()
	os.Stdout = w

	// Run the help command
	err := parseOpts(args, opts, ctx, log)
	// Restore stdout and read output
	w.Close()
	<-done

	if err != nil {
		return "", err
	}
	return string(result.Bytes()), nil
}

func isArgEqual(want interface{}, got interface{}, wantName string) error {
	if reflect.DeepEqual(want, got) {
		return nil
	}

	return errors.New(fmt.Sprintf("Unexpected %s argument: wanted '%+v', got '%+v'", wantName, want, got))
}
