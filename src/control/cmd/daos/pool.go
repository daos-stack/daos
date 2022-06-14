//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/system"
)

/*
#include "util.h"
*/
import "C"

type PoolID struct {
	ui.LabelOrUUIDFlag
}

type poolBaseCmd struct {
	daosCmd
	poolUUID  uuid.UUID
	poolLabel *C.char

	cPoolHandle C.daos_handle_t

	SysName  string `long:"sys-name" short:"G" description:"DAOS system name"`
	PoolFlag PoolID `long:"pool" short:"p" description:"pool UUID (deprecated; use positional arg)"`
	Args     struct {
		Pool PoolID `positional-arg-name:"<pool name or UUID>"`
	} `positional-args:"yes"`
}

func (cmd *poolBaseCmd) processArguments() error {
	// Handle deprecated --pool flag
	if !cmd.PoolFlag.Empty() {
		cmd.Args.Pool = cmd.PoolFlag
	}

	return nil
}

func (cmd *poolBaseCmd) poolUUIDPtr() *C.uchar {
	if cmd.poolUUID == uuid.Nil {
		cmd.Errorf("poolUUIDPtr(): nil UUID")
		return nil
	}
	return (*C.uchar)(unsafe.Pointer(&cmd.poolUUID[0]))
}

func (cmd *poolBaseCmd) PoolID() PoolID {
	return cmd.Args.Pool
}

func (cmd *poolBaseCmd) connectPool(flags C.uint) error {
	sysName := cmd.SysName
	if sysName == "" {
		sysName = build.DefaultSystemName
	}
	cSysName := C.CString(sysName)
	defer freeString(cSysName)

	var rc C.int
	switch {
	case cmd.Args.Pool.HasLabel():
		var poolInfo C.daos_pool_info_t
		cLabel := C.CString(cmd.Args.Pool.Label)
		defer freeString(cLabel)

		cmd.Debugf("connecting to pool: %s", cmd.Args.Pool.Label)
		rc = C.daos_pool_connect(cLabel, cSysName, flags,
			&cmd.cPoolHandle, &poolInfo, nil)
		if rc == 0 {
			var err error
			cmd.poolUUID, err = uuidFromC(poolInfo.pi_uuid)
			if err != nil {
				cmd.disconnectPool()
				return err
			}
		}
	case cmd.Args.Pool.HasUUID():
		cmd.poolUUID = cmd.Args.Pool.UUID
		cmd.Debugf("connecting to pool: %s", cmd.poolUUID)
		cUUIDstr := C.CString(cmd.poolUUID.String())
		defer freeString(cUUIDstr)
		rc = C.daos_pool_connect(cUUIDstr, cSysName, flags,
			&cmd.cPoolHandle, nil, nil)
	default:
		return errors.New("no pool UUID or label supplied")
	}

	return daosError(rc)
}

func (cmd *poolBaseCmd) disconnectPool() {
	cmd.Debugf("disconnecting pool %s", cmd.Args.Pool)
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_pool_disconnect(cmd.cPoolHandle, nil)
	if rc == -C.DER_NOMEM {
		rc = C.daos_pool_disconnect(cmd.cPoolHandle, nil)
		// DAOS-8866, daos_pool_disconnect() might have failed, but worked anyway.
		if rc == -C.DER_NO_HDL {
			rc = -C.DER_SUCCESS
		}
	}

	if err := daosError(rc); err != nil {
		cmd.Errorf("pool disconnect failed: %s", err)
	}
}

func (cmd *poolBaseCmd) resolveAndConnect(flags C.uint, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.connectPool(flags); err != nil {
		return nil, errors.Wrapf(err,
			"failed to connect to pool %s", cmd.Args.Pool)
	}

	if ap != nil {
		if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
			return nil, err
		}
		ap.pool = cmd.cPoolHandle
		switch {
		case cmd.Args.Pool.HasLabel():
			pLabel := C.CString(cmd.Args.Pool.Label)
			defer freeString(pLabel)
			C.strncpy(&ap.pool_str[0], pLabel, C.DAOS_PROP_LABEL_MAX_LEN)
		case cmd.Args.Pool.HasUUID():
			pUUIDstr := C.CString(cmd.poolUUID.String())
			defer freeString(pUUIDstr)
			C.strncpy(&ap.pool_str[0], pUUIDstr, C.DAOS_PROP_LABEL_MAX_LEN)
		}
	}

	return func() {
		cmd.disconnectPool()
	}, nil
}

func (cmd *poolBaseCmd) getAttr(name string) (*attribute, error) {
	return getDaosAttribute(cmd.cPoolHandle, poolAttr, name)
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

func convertPoolSpaceInfo(in *C.struct_daos_pool_space, mt C.uint) *mgmtpb.StorageUsageStats {
	if in == nil {
		return nil
	}

	return &mgmtpb.StorageUsageStats{
		Total:     uint64(in.ps_space.s_total[mt]),
		Free:      uint64(in.ps_space.s_free[mt]),
		Min:       uint64(in.ps_free_min[mt]),
		Max:       uint64(in.ps_free_max[mt]),
		Mean:      uint64(in.ps_free_mean[mt]),
		MediaType: mgmtpb.StorageMediaType(mt),
	}
}

func convertPoolRebuildStatus(in *C.struct_daos_rebuild_status) *mgmtpb.PoolRebuildStatus {
	if in == nil {
		return nil
	}

	out := &mgmtpb.PoolRebuildStatus{
		Status: int32(in.rs_errno),
	}
	if out.Status == 0 {
		out.Objects = uint64(in.rs_obj_nr)
		out.Records = uint64(in.rs_rec_nr)
		switch {
		case in.rs_version == 0:
			out.State = mgmtpb.PoolRebuildStatus_IDLE
		case C.get_rebuild_state(in) == C.DRS_COMPLETED:
			out.State = mgmtpb.PoolRebuildStatus_DONE
		default:
			out.State = mgmtpb.PoolRebuildStatus_BUSY
		}
	}

	return out
}

// This is not great... But it allows us to leverage the existing
// pretty printer that dmg uses for this info. Better to find some
// way to unify all of this and remove redundancy/manual conversion.
//
// We're basically doing the same thing as ds_mgmt_drpc_pool_query()
// to stuff the info into a protobuf message and then using the
// automatic conversion from proto to control. Kind of ugly but
// gets the job done. We could potentially create some function
// that's shared between this code and the drpc handlers to deal
// with stuffing the protobuf message but it's probably overkill.
func convertPoolInfo(pinfo *C.daos_pool_info_t) (*control.PoolQueryResp, error) {
	pqp := new(mgmtpb.PoolQueryResp)

	pqp.Uuid = uuid.Must(uuidFromC(pinfo.pi_uuid)).String()
	pqp.TotalTargets = uint32(pinfo.pi_ntargets)
	pqp.DisabledTargets = uint32(pinfo.pi_ndisabled)
	pqp.ActiveTargets = uint32(pinfo.pi_space.ps_ntargets)
	pqp.TotalEngines = uint32(pinfo.pi_nnodes)
	pqp.Leader = uint32(pinfo.pi_leader)
	pqp.Version = uint32(pinfo.pi_map_ver)

	pqp.TierStats = []*mgmtpb.StorageUsageStats{
		convertPoolSpaceInfo(&pinfo.pi_space, C.DAOS_MEDIA_SCM),
		convertPoolSpaceInfo(&pinfo.pi_space, C.DAOS_MEDIA_NVME),
	}
	pqp.Rebuild = convertPoolRebuildStatus(&pinfo.pi_rebuild_st)

	pqr := new(control.PoolQueryResp)
	return pqr, convert.Types(pqp, pqr)
}

const (
	dpiQuerySpace   = C.DPI_SPACE
	dpiQueryRebuild = C.DPI_REBUILD_STATUS
	dpiQueryAll     = C.uint64_t(^uint64(0)) // DPI_ALL is -1
)

func generateRankSet(ranklist *C.d_rank_list_t) string {
	if ranklist.rl_nr == 0 {
		return ""
	}
	ranks := uintptr(unsafe.Pointer(ranklist.rl_ranks))
	const size = unsafe.Sizeof(uint32(0))
	rankset := "["
	for i := 0; i < int(ranklist.rl_nr); i++ {
		if i > 0 {
			rankset += ","
		}
		rankset += fmt.Sprint(*(*uint32)(unsafe.Pointer(ranks + uintptr(i)*size)))
	}
	rankset += "]"
	return rankset
}

func (cmd *poolQueryCmd) processArguments() error {
	if err := cmd.poolBaseCmd.processArguments(); err != nil {
		return err
	}

	if cmd.Args.Pool.Empty() {
		return errors.New("pool ID is required")
	}

	return nil
}

func (cmd *poolQueryCmd) Execute(_ []string) error {
	if err := cmd.processArguments(); err != nil {
		return err
	}

	if cmd.ShowEnabledRanks && cmd.ShowDisabledRanks {
		return errors.New("show-enabled and show-disabled can't be used at the same time.")
	}
	var ranklistPtr **C.d_rank_list_t = nil
	var ranklist *C.d_rank_list_t = nil

	if cmd.ShowEnabledRanks || cmd.ShowDisabledRanks {
		ranklistPtr = &ranklist
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	pinfo := C.daos_pool_info_t{
		pi_bits: dpiQueryAll,
	}
	if cmd.ShowDisabledRanks {
		pinfo.pi_bits &= C.uint64_t(^(uint64(C.DPI_ENGINES_ENABLED)))
	}

	rc := C.daos_pool_query(cmd.cPoolHandle, ranklistPtr, &pinfo, nil, nil)
	defer C.d_rank_list_free(ranklist)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to query pool %s", cmd.poolUUID)
	}

	pqr, err := convertPoolInfo(&pinfo)
	if err != nil {
		return err
	}

	if ranklistPtr != nil {
		if cmd.ShowEnabledRanks {
			pqr.EnabledRanks = system.MustCreateRankSet(generateRankSet(ranklist))
		}
		if cmd.ShowDisabledRanks {
			pqr.DisabledRanks = system.MustCreateRankSet(generateRankSet(ranklist))
		}
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(pqr, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryResponse(pqr, &bld); err != nil {
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
func convertPoolTargetInfo(ptinfo *C.daos_target_info_t) (*control.PoolQueryTargetInfo, error) {
	pqti := new(control.PoolQueryTargetInfo)
	pqti.Type = control.PoolQueryTargetType(ptinfo.ta_type)
	pqti.State = control.PoolQueryTargetState(ptinfo.ta_state)
	pqti.Space = []*control.StorageTargetUsage{
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
	if err := cmd.processArguments(); err != nil {
		return err
	}

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
				"failed to query pool %s rank:target %d:%d", cmd.poolUUID, cmd.Rank, idxList[tgt])
		}

		tgtInfo, err := convertPoolTargetInfo(ptInfo)
		infoResp.Infos = append(infoResp.Infos, tgtInfo)
		if err != nil {
			return err
		}
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(infoResp, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryTargetResponse(infoResp, &bld); err != nil {
		return err
	}

	cmd.Info(bld.String())

	return nil
}

type poolListAttrsCmd struct {
	poolBaseCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *poolListAttrsCmd) processArguments() error {
	if err := cmd.poolBaseCmd.processArguments(); err != nil {
		return err
	}

	if cmd.poolBaseCmd.Args.Pool.Empty() {
		return errors.New("pool ID is required")
	}

	return nil
}

func (cmd *poolListAttrsCmd) Execute(_ []string) error {
	if err := cmd.processArguments(); err != nil {
		return err
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	attrs, err := listDaosAttributes(cmd.cPoolHandle, poolAttr, cmd.Verbose)
	if err != nil {
		return errors.Wrapf(err,
			"failed to list attributes for pool %s", cmd.poolUUID)
	}

	if cmd.jsonOutputEnabled() {
		if cmd.Verbose {
			return cmd.outputJSON(attrs.asMap(), nil)
		}
		return cmd.outputJSON(attrs.asList(), nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for pool %s:", cmd.poolUUID)
	printAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

type poolGetAttrCmd struct {
	poolBaseCmd

	FlagAttr string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	Args     struct {
		Name string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes"`
}

func (cmd *poolGetAttrCmd) processArguments() error {
	// Special handling for deprecated --attr flag.
	if cmd.FlagAttr != "" {
		// Don't allow mixture of positional and deprecated flag arguments.
		if cmd.PoolFlag.Empty() {
			return errors.New("--attr requires --pool")
		}

		cmd.Args.Name = cmd.FlagAttr
	} else if !cmd.PoolFlag.Empty() {
		return errors.New("--attr is required when --pool is specified")
	}

	if err := cmd.poolBaseCmd.processArguments(); err != nil {
		return err
	}

	if cmd.poolBaseCmd.Args.Pool.Empty() || cmd.Args.Name == "" {
		return errors.New("pool ID and attribute name are required")
	}

	return nil
}

func (cmd *poolGetAttrCmd) Execute(_ []string) error {
	if err := cmd.processArguments(); err != nil {
		return err
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	attr, err := cmd.getAttr(cmd.Args.Name)
	if err != nil {
		return errors.Wrapf(err,
			"failed to get attribute %q from pool %s",
			cmd.Args.Name, cmd.poolUUID)
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(attr, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for pool %s:", cmd.poolUUID)
	printAttributes(&bld, title, attr)

	cmd.Info(bld.String())

	return nil
}

type poolSetAttrCmd struct {
	poolBaseCmd

	FlagAttr  string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	FlagValue string `long:"value" short:"v" description:"attribute value (deprecated; use positional argument)"`
	Args      struct {
		Name  string `positional-arg-name:"<attribute name>"`
		Value string `positional-arg-name:"<attribute value>"`
	} `positional-args:"yes"`
}

func (cmd *poolSetAttrCmd) processArguments() error {
	// Special handling for deprecated --attr flag.
	if cmd.FlagAttr != "" {
		if cmd.FlagValue == "" {
			return errors.New("--attr requires --value")
		}

		// Don't allow mixture of positional and deprecated flag arguments.
		if cmd.PoolFlag.Empty() {
			return errors.New("--attr requires --pool")
		}

		cmd.Args.Name = cmd.FlagAttr
	} else if !cmd.PoolFlag.Empty() {
		return errors.New("--attr is required when --pool is specified")
	}

	// Special handling for deprecated --value flag.
	if cmd.FlagValue != "" {
		if cmd.FlagAttr == "" {
			return errors.New("--value requires --attr")
		}

		cmd.Args.Value = cmd.FlagValue
	}

	if err := cmd.poolBaseCmd.processArguments(); err != nil {
		return err
	}

	if cmd.Args.Name == "" {
		return errors.New("attribute name and value are required")
	}
	if cmd.Args.Value == "" {
		return errors.New("attribute name and value are required")
	}

	return nil
}

func (cmd *poolSetAttrCmd) Execute(_ []string) error {
	if err := cmd.processArguments(); err != nil {
		return err
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := setDaosAttribute(cmd.cPoolHandle, poolAttr, &attribute{
		Name:  cmd.Args.Name,
		Value: []byte(cmd.Args.Value),
	}); err != nil {
		return errors.Wrapf(err,
			"failed to set attribute %q on pool %s",
			cmd.Args.Name, cmd.poolUUID)
	}

	return nil
}

type poolDelAttrCmd struct {
	poolBaseCmd

	FlagAttr string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	Args     struct {
		Name string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes"`
}

func (cmd *poolDelAttrCmd) processArguments() error {
	// Special handling for deprecated --attr flag.
	if cmd.FlagAttr != "" {
		// Don't allow mixture of positional and deprecated flag arguments.
		if cmd.PoolFlag.Empty() {
			return errors.New("--attr requires --pool")
		}

		cmd.Args.Name = cmd.FlagAttr
	} else if !cmd.PoolFlag.Empty() {
		return errors.New("--attr is required when --pool is specified")
	}

	if err := cmd.poolBaseCmd.processArguments(); err != nil {
		return err
	}

	if cmd.poolBaseCmd.Args.Pool.Empty() || cmd.Args.Name == "" {
		return errors.New("pool ID and attribute name are required")
	}

	return nil
}

func (cmd *poolDelAttrCmd) Execute(_ []string) error {
	if err := cmd.processArguments(); err != nil {
		return err
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := delDaosAttribute(cmd.cPoolHandle, poolAttr, cmd.Args.Name); err != nil {
		return errors.Wrapf(err,
			"failed to delete attribute %q on pool %s",
			cmd.Args.Name, cmd.poolUUID)
	}

	return nil
}

type poolAutoTestCmd struct {
	poolBaseCmd

	SkipBig       C.bool `long:"skip-big" short:"S" description:"skip big tests"`
	DeadlineLimit C.int  `long:"deadline-limit" short:"D" description:"deadline limit for test (seconds)"`
}

func (cmd *poolAutoTestCmd) Execute(_ []string) error {
	if err := cmd.processArguments(); err != nil {
		return err
	}

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
	if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
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
			cmd.poolUUID)
	}

	return nil
}
