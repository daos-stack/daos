//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/daos/api"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

/*
#include "util.h"

// Note: intentionally not "#include <stdlib.h>" -- SCons' Go dependency scanner
// (site_scons/site_tools/go_builder.py) treats every "#include <...>" line in a
// CGO preamble as a DAOS public header located under src/include/, which breaks
// the build for genuine system headers.  Declare getenv() directly instead.
extern char *getenv(const char *name);

// Compiled-in default LSAN options, used whenever ASAN_OPTIONS/LSAN_OPTIONS is not
// set in the environment.  Without this hook, LSAN falls back to its own default of
// detect_leaks=1 and performs a stop-the-world ptrace-based scan whenever the
// process exits through any path that runs libc atexit handlers.  That scan aborts
// with "LeakSanitizer has encountered a fatal error" on some CI hosts (likely a
// ptrace restriction).  Values explicitly set via ASAN_OPTIONS/LSAN_OPTIONS in the
// environment still take precedence over this compiled-in default.
const char *__lsan_default_options(void) {
	return "detect_leaks=0";
}

// Compiled-in default TSAN options for the daos CLI, used whenever
// TSAN_OPTIONS is not set in the environment.  This binary is the reproduction
// target for DAOS-18859 (the suspected use-after-free is a race between a
// background CaRT/Mercury TLS thread and the main thread, both inside this
// process, during pool connect) so TSan reporting is enabled here, unlike the
// other DAOS Go binaries where it is silenced by default (report_bugs=0) to
// avoid noise unrelated to this investigation.  Values explicitly set via
// TSAN_OPTIONS in the environment still take precedence over this default.
//
// Guarded by __SANITIZE_THREAD__ (defined by GCC/Clang only when this
// translation unit is actually compiled with -fsanitize=thread): when
// SANITIZERS is unset, Go falls back to its own native -race flag, which
// links Go's own bundled TSan-derived runtime.  That runtime already defines
// __tsan_default_options() itself, so defining it unconditionally here causes
// "multiple definition of `__tsan_default_options'" at link time for any
// ordinary -race build (observed across EL8/EL9/Leap15 in build_023).
#ifdef __SANITIZE_THREAD__
const char *__tsan_default_options(void) {
	return "halt_on_error=0:report_signal_unsafe=0:history_size=7:second_deadlock_stack=1";
}
#endif

// Weak reference — resolves to the real LSAN function in ASAN builds, NULL otherwise.
extern void __attribute__((weak)) __lsan_do_leak_check(void);

// Go's runtime calls exit_group() directly when it exits, bypassing libc's exit()
// and therefore ASAN's registered atexit() handlers.  Call the leak-checker explicitly
// so that an ASAN report is written to log_path when leaks are detected.
//
// __lsan_do_leak_check() is LSAN's on-demand API: unlike the implicit atexit-based
// check, it ignores ASAN_OPTIONS=detect_leaks=0 and always performs a stop-the-world
// scan.  On some CI hosts this scan aborts with "LeakSanitizer has encountered a
// fatal error", failing the command even though the underlying operation succeeded.
// Make the call opt-in via DAOS_ASAN_LEAK_CHECK=1 so routine CLI invocations are
// unaffected.
// Note: use-after-free crashes are reported immediately by ASAN's signal handler and
// do not depend on this call.
static void run_asan_fini(void) {
	if (__lsan_do_leak_check && getenv("DAOS_ASAN_LEAK_CHECK"))
		__lsan_do_leak_check();
}
*/
import "C"

// argOrID is used to handle a positional argument that can be a label or UUID,
// or a non-ID positional argument to be consumed by the command handler if the
// --path flag is used.
type argOrID struct {
	ui.LabelOrUUIDFlag
	unparsedArg string
}

func (opt *argOrID) Clear() {
	opt.LabelOrUUIDFlag.Clear()
	opt.unparsedArg = ""
}

func (opt *argOrID) UnmarshalFlag(val string) error {
	if err := opt.LabelOrUUIDFlag.UnmarshalFlag(val); err != nil {
		if opt.unparsedArg != "" {
			return err
		}
		opt.unparsedArg = val
		return nil
	}
	return nil
}

func (opt *argOrID) String() string {
	if opt.unparsedArg != "" {
		return opt.unparsedArg
	}
	if !opt.LabelOrUUIDFlag.Empty() {
		return opt.LabelOrUUIDFlag.String()
	}
	return ""
}

type PoolID struct {
	argOrID
}

type poolBaseCmd struct {
	daosCmd
	pool *api.PoolHandle

	// deprecated params -- gradually remove in favor of PoolHandle
	poolUUID    uuid.UUID
	cPoolHandle C.daos_handle_t

	Args struct {
		Pool PoolID `positional-arg-name:"<pool label or UUID>" description:"required"`
	} `positional-args:"yes"`
}

func (cmd *poolBaseCmd) PoolID() ui.LabelOrUUIDFlag {
	return cmd.Args.Pool.LabelOrUUIDFlag
}

func (cmd *poolBaseCmd) connectPool(flags daos.PoolConnectFlag) error {
	if cmd.PoolID().Empty() {
		return errors.New("no pool UUID or label supplied")
	}

	req := api.PoolConnectReq{
		SysName: cmd.SysName,
		ID:      cmd.PoolID().String(),
		Flags:   flags,
	}

	resp, err := PoolConnect(cmd.MustLogCtx(), req)
	if err != nil {
		return err
	}
	cmd.pool = resp.Connection

	// Needed for backward compatibility with code that calls libdaos directly.
	// Can be removed when everything is behind the API.
	if err := cmd.pool.FillHandle(unsafe.Pointer(&cmd.cPoolHandle)); err != nil {
		cmd.disconnectPool()
		return err
	}

	return nil
}

func (cmd *poolBaseCmd) disconnectPool() {
	if err := cmd.pool.Disconnect(cmd.MustLogCtx()); err != nil {
		cmd.Errorf("pool disconnect failed: %v", err)
	}
}

func (cmd *poolBaseCmd) resolveAndConnect(flags daos.PoolConnectFlag, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.connectPool(flags); err != nil {
		return nil, errors.Wrapf(err,
			"failed to connect to pool %s", cmd.PoolID())
	}

	if ap != nil {
		if err := copyUUID(&ap.p_uuid, cmd.pool.UUID()); err != nil {
			return nil, err
		}
		ap.pool = cmd.cPoolHandle

		pLabel := C.CString(cmd.pool.Label)
		defer freeString(pLabel)
		C.strncpy(&ap.pool_str[0], pLabel, C.DAOS_PROP_LABEL_MAX_LEN)
	}

	return cmd.disconnectPool, nil
}

type poolCmd struct {
	List         poolListCmd         `command:"list" description:"list pools to which this user has access"`
	Query        poolQueryCmd        `command:"query" description:"query pool info"`
	QueryTargets poolQueryTargetsCmd `command:"query-targets" description:"query pool target info"`
	ListConts    containerListCmd    `command:"list-containers" alias:"list-cont" description:"list all containers in pool"`
	ListAttrs    poolListAttrsCmd    `command:"list-attr" alias:"list-attrs" alias:"lsattr" description:"list pool user-defined attributes"`
	GetAttr      poolGetAttrCmd      `command:"get-attr" alias:"getattr" description:"get pool user-defined attribute"`
	SetAttr      poolSetAttrCmd      `command:"set-attr" alias:"setattr" description:"set pool user-defined attribute"`
	DelAttr      poolDelAttrCmd      `command:"del-attr" alias:"delattr" description:"delete pool user-defined attribute"`
	AutoTest     poolAutoTestCmd     `command:"autotest" description:"verify setup with smoke tests"`
}

type poolQueryCmd struct {
	poolBaseCmd
	ShowEnabledRanks bool `short:"e" long:"show-enabled" description:"Show engine unique identifiers (ranks) which are enabled"`
	HealthOnly       bool `short:"t" long:"health-only" description:"Only perform pool health related queries"`
}

func (cmd *poolQueryCmd) Execute(_ []string) error {
	queryMask := daos.DefaultPoolQueryMask
	if cmd.HealthOnly {
		queryMask = daos.HealthOnlyPoolQueryMask
	}
	if cmd.ShowEnabledRanks {
		queryMask.SetOptions(daos.PoolQueryOptionEnabledEngines)
	}

	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadOnly, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	poolInfo, err := cmd.pool.Query(cmd.MustLogCtx(), queryMask)
	if err != nil {
		return errors.Wrapf(err, "failed to query pool %q", cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(poolInfo, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolInfo(poolInfo, &bld); err != nil {
		return err
	}

	cmd.Debugf("Pool query options: %s", poolInfo.QueryMask)
	cmd.Info(bld.String())

	return nil
}

type poolQueryTargetsCmd struct {
	poolBaseCmd

	Rank    uint32         `long:"rank" required:"1" description:"Engine rank of the target(s) to be queried"`
	Targets ui.RankSetFlag `long:"target-idx" description:"Comma-separated list of target index(es) to be queried (default: all)"`
}

func (cmd *poolQueryTargetsCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadOnly, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	infos, err := cmd.pool.QueryTargets(cmd.MustLogCtx(), ranklist.Rank(cmd.Rank), &cmd.Targets.RankSet)
	if err != nil {
		return errors.Wrapf(err, "failed to query targets for pool %s", cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(infos, nil)
	}

	var bld strings.Builder
	for _, info := range infos {
		if err := pretty.PrintPoolQueryTargetInfo(info, &bld); err != nil {
			return err
		}
	}

	cmd.Info(bld.String())

	return nil
}

type poolListAttrsCmd struct {
	poolBaseCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *poolListAttrsCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadOnly, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	return listAttributes(cmd, cmd.pool, poolAttr, cmd.pool.ID(), cmd.Verbose)
}

type poolGetAttrCmd struct {
	poolBaseCmd

	Args struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *poolGetAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadOnly, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	return getAttributes(cmd, cmd.pool, poolAttr, cmd.pool.ID(), cmd.Args.Attrs.ParsedProps.ToSlice()...)
}

type poolSetAttrCmd struct {
	poolBaseCmd

	Args struct {
		Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *poolSetAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadWrite, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	return setAttributes(cmd, cmd.pool, poolAttr, cmd.pool.ID(), cmd.Args.Attrs.ParsedProps)
}

type poolDelAttrCmd struct {
	poolBaseCmd

	Args struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *poolDelAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadWrite, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	return delAttributes(cmd, cmd.pool, poolAttr, cmd.pool.ID(), cmd.Args.Attrs.ParsedProps.ToSlice()...)
}

type poolAutoTestCmd struct {
	poolBaseCmd

	SkipBig       C.bool `long:"skip-big" short:"S" description:"skip big tests"`
	DeadlineLimit C.int  `long:"deadline-limit" short:"D" description:"deadline limit for test (seconds)"`
}

func (cmd *poolAutoTestCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadWrite, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	ap.pool = cmd.cPoolHandle
	if err := copyUUID(&ap.p_uuid, cmd.pool.UUID()); err != nil {
		return err
	}
	ap.p_op = C.POOL_AUTOTEST

	// Set outstream to stdout; don't try to redirect it.
	ap.outstream, err = fd2FILE(os.Stdout.Fd(), "w")
	if err != nil {
		return err
	}

	ap.skip_big = C.bool(cmd.SkipBig)

	ap.deadline_limit = C.int(cmd.DeadlineLimit)

	rc := C.pool_autotest_hdlr(ap)
	// Explicit ASAN/LSAN finalization: Go exits via exit_group syscall which bypasses
	// libc's exit() and therefore ASAN's atexit() handlers.  Force the report here so
	// that the log_path file is written before the process terminates.
	C.run_asan_fini()
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to run autotest for pool %s", cmd.PoolID())
	}

	return nil
}

type poolListCmd struct {
	daosCmd
	Verbose bool `short:"v" long:"verbose" description:"Add pool UUIDs and service replica lists to display"`
	NoQuery bool `short:"n" long:"no-query" description:"Disable query of listed pools"`
}

func (cmd *poolListCmd) Execute(_ []string) error {
	pools, err := GetPoolList(cmd.MustLogCtx(), api.GetPoolListReq{
		Query: !cmd.NoQuery,
	})
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(struct {
			Pools []*daos.PoolInfo `json:"pools"` // compatibility with dmg
		}{
			Pools: pools,
		}, nil)
	}

	var buf strings.Builder
	if err := pretty.PrintPoolList(pools, &buf, cmd.Verbose); err != nil {
		return err
	}
	cmd.Info(buf.String())

	return nil
}
