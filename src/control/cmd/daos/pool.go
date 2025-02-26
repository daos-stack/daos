//
// (C) Copyright 2021-2024 Intel Corporation.
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
		Pool PoolID `positional-arg-name:"pool label or UUID" description:"required if --path is not used"`
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
