//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"os"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	apiMocks "github.com/daos-stack/daos/src/control/lib/daos/client/mocks"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

/*
#include "util.h"
*/
import "C"

type (
	wrappedPoolHandle struct {
		*daosAPI.PoolHandle
	}

	wrappedPoolMock struct {
		*apiMocks.PoolConn
	}

	poolConnection interface {
		UUID() uuid.UUID
		Pointer() unsafe.Pointer
		Disconnect(context.Context) error
		Query(context.Context, daosAPI.PoolQueryReq) (*daos.PoolInfo, error)
		OpenContainer(context.Context, string, daosAPI.ContainerOpenFlag) (contConnection, error)
		CreateContainer(context.Context, daosAPI.ContainerCreateReq) (*daosAPI.ContainerInfo, error)
		DestroyContainer(context.Context, string, bool) error
		ListContainers(context.Context, bool) ([]*daosAPI.ContainerInfo, error)
		ListAttributes(context.Context) ([]string, error)
		GetAttributes(context.Context, ...string) ([]*daos.Attribute, error)
		SetAttributes(context.Context, []*daos.Attribute) error
		DeleteAttribute(context.Context, string) error
	}
)

func (wph *wrappedPoolHandle) UUID() uuid.UUID {
	return wph.PoolHandle.UUID
}

func (wph *wrappedPoolHandle) OpenContainer(ctx context.Context, contID string, flags daosAPI.ContainerOpenFlag) (contConnection, error) {
	ch, err := wph.PoolHandle.OpenContainer(ctx, contID, flags)
	if err != nil {
		return nil, err
	}

	return &wrappedContHandle{ch}, nil
}

func (wpm *wrappedPoolMock) Pointer() unsafe.Pointer {
	return unsafe.Pointer(&C.daos_handle_t{})
}

func (wpm *wrappedPoolMock) OpenContainer(ctx context.Context, contID string, flags daosAPI.ContainerOpenFlag) (contConnection, error) {
	cm, err := wpm.PoolConn.OpenContainer(ctx, contID, flags)
	if err != nil {
		return nil, err
	}

	return &wrappedContMock{cm}, nil
}

var (
	_ poolConnection = (*wrappedPoolHandle)(nil)
)

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

	poolConn    poolConnection
	cPoolHandle C.daos_handle_t

	SysName string `long:"sys-name" short:"G" description:"DAOS system name"`
	Args    struct {
		Pool PoolID `positional-arg-name:"pool label or UUID" description:"required if --path is not used"`
	} `positional-args:"yes"`
}

func (cmd *poolBaseCmd) poolUUIDPtr() *C.uchar {
	poolUUID := cmd.poolUUID()
	return (*C.uchar)(unsafe.Pointer(&poolUUID[0]))
}

func (cmd *poolBaseCmd) poolUUID() uuid.UUID {
	if cmd.poolConn == nil || cmd.poolConn.UUID() == uuid.Nil {
		cmd.Error("poolConn.UUID: nil UUID")
		return uuid.Nil
	}

	return cmd.poolConn.UUID()
}

func (cmd *poolBaseCmd) PoolID() ui.LabelOrUUIDFlag {
	return cmd.Args.Pool.LabelOrUUIDFlag
}

func (cmd *poolBaseCmd) connectPool(flags daosAPI.PoolConnectFlag) error {
	sysName := cmd.SysName
	if sysName == "" {
		sysName = build.DefaultSystemName
	}
	poolID := cmd.PoolID().String()

	if mpc := apiMocks.GetPoolConn(cmd.daosCtx); mpc != nil {
		if err := mpc.Connect(cmd.daosCtx, poolID, sysName, flags); err != nil {
			return err
		}
		cmd.poolConn = &wrappedPoolMock{mpc}
	} else {
		resp, err := daosAPI.PoolConnect(cmd.daosCtx, poolID, sysName, flags)
		if err != nil {
			return err
		}
		cmd.poolConn = &wrappedPoolHandle{resp.PoolConnection}
	}
	cmd.cPoolHandle = *conn2HdlPtr(cmd.poolConn)

	return nil
}

func (cmd *poolBaseCmd) disconnectPool() {
	cmd.Debugf("disconnecting pool %s", cmd.PoolID())

	if err := cmd.poolConn.Disconnect(cmd.daosCtx); err != nil {
		cmd.Errorf("pool disconnect failed: %s", err)
	}
}

func (cmd *poolBaseCmd) resolveAndConnect(flags daosAPI.PoolConnectFlag, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.connectPool(flags); err != nil {
		return nil, errors.Wrapf(err,
			"failed to connect to pool %s", cmd.PoolID())
	}

	if ap != nil {
		if err := copyUUID(&ap.p_uuid, cmd.poolUUID()); err != nil {
			return nil, err
		}
		ap.pool = *conn2HdlPtr(cmd.poolConn)
		switch {
		case cmd.PoolID().HasLabel():
			pLabel := C.CString(cmd.PoolID().Label)
			defer freeString(pLabel)
			C.strncpy(&ap.pool_str[0], pLabel, C.DAOS_PROP_LABEL_MAX_LEN)
		case cmd.PoolID().HasUUID():
			pUUIDstr := C.CString(cmd.poolUUID().String())
			defer freeString(pUUIDstr)
			C.strncpy(&ap.pool_str[0], pUUIDstr, C.DAOS_PROP_LABEL_MAX_LEN)
		}
	}

	return func() {
		cmd.disconnectPool()
	}, nil
}

type poolCmd struct {
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
	ShowEnabledRanks  bool `short:"e" long:"show-enabled" description:"Show engine unique identifiers (ranks) which are enabled"`
	ShowDisabledRanks bool `short:"b" long:"show-disabled" description:"Show engine unique identifiers (ranks) which are disabled"`
}

func (cmd *poolQueryCmd) Execute(_ []string) error {
	if cmd.ShowEnabledRanks && cmd.ShowDisabledRanks {
		return errors.New("show-enabled and show-disabled can't be used at the same time.")
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	req := daosAPI.PoolQueryReq{
		IncludeEnabled:  cmd.ShowEnabledRanks,
		IncludeDisabled: cmd.ShowDisabledRanks,
	}
	poolInfo, err := cmd.poolConn.Query(cmd.daosCtx, req)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(poolInfo, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolInfo(poolInfo, &bld); err != nil {
		return err
	}

	cmd.Info(bld.String())

	return nil
}

type poolQueryTargetsCmd struct {
	poolBaseCmd

	Rank    uint32 `long:"rank" required:"1" description:"Engine rank of the targets to be queried"`
	Targets string `long:"target-idx" description:"Comma-separated list of target idx(s) to be queried"`
}

// For using the pretty printer that dmg uses for this target info.
func convertPoolTargetInfo(ptinfo *C.daos_target_info_t) (*daos.PoolQueryTargetInfo, error) {
	pqti := new(daos.PoolQueryTargetInfo)
	pqti.Type = daos.PoolQueryTargetType(ptinfo.ta_type)
	pqti.State = daos.PoolQueryTargetState(ptinfo.ta_state)
	pqti.Space = []*daos.StorageTargetUsage{
		{
			Total:     uint64(ptinfo.ta_space.s_total[C.DAOS_MEDIA_SCM]),
			Free:      uint64(ptinfo.ta_space.s_free[C.DAOS_MEDIA_SCM]),
			MediaType: C.DAOS_MEDIA_SCM,
		},
		{
			Total:     uint64(ptinfo.ta_space.s_total[C.DAOS_MEDIA_NVME]),
			Free:      uint64(ptinfo.ta_space.s_free[C.DAOS_MEDIA_NVME]),
			MediaType: C.DAOS_MEDIA_NVME,
		},
	}

	return pqti, nil
}

func (cmd *poolQueryTargetsCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	var idxList []uint32
	if err = common.ParseNumberList(cmd.Targets, &idxList); err != nil {
		return errors.WithMessage(err, "parsing target list")
	}

	infoResp := new(control.PoolQueryTargetResp)
	ptInfo := new(C.daos_target_info_t)
	var rc C.int

	for tgt := 0; tgt < len(idxList); tgt++ {
		rc = C.daos_pool_query_target(cmd.cPoolHandle, C.uint32_t(idxList[tgt]), C.uint32_t(cmd.Rank), ptInfo, nil)
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err,
				"failed to query pool %s rank:target %d:%d", cmd.poolUUID(), cmd.Rank, idxList[tgt])
		}

		tgtInfo, err := convertPoolTargetInfo(ptInfo)
		infoResp.Infos = append(infoResp.Infos, tgtInfo)
		if err != nil {
			return err
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(infoResp, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryTargetResponse(infoResp, &bld); err != nil {
		return err
	}

	cmd.Info(bld.String())

	return nil
}

type poolGetOrListAttributesCmd struct {
	poolBaseCmd
}

func (cmd *poolGetOrListAttributesCmd) listAttributes() error {
	names, err := cmd.poolConn.ListAttributes(cmd.daosCtx)
	if err != nil {
		return errors.Wrapf(err, "failed to list attributes for pool %s", cmd.poolUUID())
	}

	attrList := make([]*daos.Attribute, len(names))
	for i, name := range names {
		attrList[i] = &daos.Attribute{Name: name}
	}
	var bld strings.Builder
	title := fmt.Sprintf("Attributes for pool %s:", cmd.PoolID())
	printAttributes(&bld, title, attrList...)

	cmd.Info(bld.String())

	return nil
}

func (cmd *poolGetOrListAttributesCmd) getAttributes(names ...string) error {
	attrs, err := cmd.poolConn.GetAttributes(cmd.daosCtx, names...)
	if err != nil {
		return errors.Wrapf(err,
			"failed to get attributes for pool %s",
			cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with older behavior.
		if len(names) == 1 && len(attrs) == 1 {
			return cmd.OutputJSON(attrs[0], nil)
		}
		return cmd.OutputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for pool %s:", cmd.PoolID())
	printAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

type poolListAttrsCmd struct {
	poolGetOrListAttributesCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *poolListAttrsCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.Verbose {
		return cmd.getAttributes()
	}

	return cmd.listAttributes()
}

type poolGetAttrCmd struct {
	poolGetOrListAttributesCmd

	Args struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *poolGetAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		return cmd.listAttributes()
	} else {
		return cmd.getAttributes(cmd.Args.Attrs.ParsedProps.ToSlice()...)
	}
}

type poolSetAttrCmd struct {
	poolBaseCmd

	Args struct {
		Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *poolSetAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		return errors.New("attribute name and value are required")
	}

	attrs := make([]*daos.Attribute, 0, len(cmd.Args.Attrs.ParsedProps))
	for key, val := range cmd.Args.Attrs.ParsedProps {
		attrs = append(attrs, &daos.Attribute{
			Name:  key,
			Value: []byte(val),
		})
	}

	if err := cmd.poolConn.SetAttributes(cmd.daosCtx, attrs); err != nil {
		return errors.Wrapf(err, "failed to set attributes on pool %s", cmd.PoolID())
	}

	return nil
}

type poolDelAttrCmd struct {
	poolBaseCmd

	Args struct {
		Name string `positional-arg-name:"<attribute name>" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *poolDelAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := cmd.poolConn.DeleteAttribute(cmd.daosCtx, cmd.Args.Name); err != nil {
		return errors.Wrapf(err,
			"failed to delete attribute %q on pool %s",
			cmd.Args.Name, cmd.poolUUID())
	}

	return nil
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	ap.pool = cmd.cPoolHandle
	if err := copyUUID(&ap.p_uuid, cmd.poolUUID()); err != nil {
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
		return errors.Wrapf(err, "failed to run autotest for pool %s",
			cmd.poolUUID())
	}

	return nil
}
