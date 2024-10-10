//
// (C) Copyright 2021-2024 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
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
	poolUUID uuid.UUID

	cPoolHandle C.daos_handle_t

	Args struct {
		Pool PoolID `positional-arg-name:"pool label or UUID" description:"required if --path is not used"`
	} `positional-args:"yes"`
}

func (cmd *poolBaseCmd) poolUUIDPtr() *C.uchar {
	if cmd.poolUUID == uuid.Nil {
		cmd.Errorf("poolUUIDPtr(): nil UUID")
		return nil
	}
	return (*C.uchar)(unsafe.Pointer(&cmd.poolUUID[0]))
}

func (cmd *poolBaseCmd) PoolID() ui.LabelOrUUIDFlag {
	return cmd.Args.Pool.LabelOrUUIDFlag
}

// poolConnect is a convenience wrapper around poolConnectAPI.
func poolConnect(poolID, sysName string, flags uint, query bool) (C.daos_handle_t, *C.daos_pool_info_t, error) {
	var cSysName *C.char
	if sysName != "" {
		cSysName = C.CString(sysName)
		defer freeString(cSysName)
	}

	cPoolID := C.CString(poolID)
	defer freeString(cPoolID)

	var hdl C.daos_handle_t
	var infoPtr *C.daos_pool_info_t
	if query {
		infoPtr = &C.daos_pool_info_t{
			pi_bits: C.ulong(daos.DefaultPoolQueryMask),
		}
	}

	return hdl, infoPtr, poolConnectAPI(cPoolID, cSysName, C.uint(flags), &hdl, infoPtr)
}

// poolConnectAPI is a lower-level wrapper around daos_pool_connect().
func poolConnectAPI(poolID, sysName *C.char, flags C.uint, hdl *C.daos_handle_t, info *C.daos_pool_info_t) error {
	return daosError(C.daos_pool_connect(poolID, sysName, flags, hdl, info, nil))
}

// poolDisconnectAPI is a convenience wrapper around daos_pool_disconnect().
func poolDisconnectAPI(hdl C.daos_handle_t) error {
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_pool_disconnect(hdl, nil)
	if rc == -C.DER_NOMEM {
		rc = C.daos_pool_disconnect(hdl, nil)
		// DAOS-8866, daos_pool_disconnect() might have failed, but worked anyway.
		if rc == -C.DER_NO_HDL {
			rc = -C.DER_SUCCESS
		}
	}

	return daosError(rc)
}

func (cmd *poolBaseCmd) connectPool(flags C.uint) error {
	sysName := cmd.SysName
	var cSysName *C.char
	if sysName != "" {
		cSysName := C.CString(sysName)
		defer freeString(cSysName)
	}

	switch {
	case cmd.PoolID().HasLabel():
		var poolInfo C.daos_pool_info_t
		cLabel := C.CString(cmd.PoolID().Label)
		defer freeString(cLabel)

		cmd.Debugf("connecting to pool: %s", cmd.PoolID().Label)
		if err := poolConnectAPI(cLabel, cSysName, flags, &cmd.cPoolHandle, &poolInfo); err != nil {
			return err
		}
		var err error
		cmd.poolUUID, err = uuidFromC(poolInfo.pi_uuid)
		if err != nil {
			cmd.disconnectPool()
			return err
		}
	case cmd.PoolID().HasUUID():
		cmd.poolUUID = cmd.PoolID().UUID
		cmd.Debugf("connecting to pool: %s", cmd.poolUUID)
		cUUIDstr := C.CString(cmd.poolUUID.String())
		defer freeString(cUUIDstr)
		if err := poolConnectAPI(cUUIDstr, cSysName, flags, &cmd.cPoolHandle, nil); err != nil {
			return err
		}
	default:
		return errors.New("no pool UUID or label supplied")
	}

	return nil
}

func (cmd *poolBaseCmd) disconnectPool() {
	cmd.Debugf("disconnecting pool %s", cmd.PoolID())
	if err := poolDisconnectAPI(cmd.cPoolHandle); err != nil {
		cmd.Errorf("pool disconnect failed: %v", err)
	}
}

func (cmd *poolBaseCmd) resolveAndConnect(flags C.uint, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.connectPool(flags); err != nil {
		return nil, errors.Wrapf(err,
			"failed to connect to pool %s", cmd.PoolID())
	}

	if ap != nil {
		if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
			return nil, err
		}
		ap.pool = cmd.cPoolHandle
		switch {
		case cmd.PoolID().HasLabel():
			pLabel := C.CString(cmd.PoolID().Label)
			defer freeString(pLabel)
			C.strncpy(&ap.pool_str[0], pLabel, C.DAOS_PROP_LABEL_MAX_LEN)
		case cmd.PoolID().HasUUID():
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

func convertPoolSpaceInfo(in *C.struct_daos_pool_space, mt C.uint) *daos.StorageUsageStats {
	if in == nil {
		return nil
	}

	return &daos.StorageUsageStats{
		Total:     uint64(in.ps_space.s_total[mt]),
		Free:      uint64(in.ps_space.s_free[mt]),
		Min:       uint64(in.ps_free_min[mt]),
		Max:       uint64(in.ps_free_max[mt]),
		Mean:      uint64(in.ps_free_mean[mt]),
		MediaType: daos.StorageMediaType(mt),
	}
}

func convertPoolRebuildStatus(in *C.struct_daos_rebuild_status) *daos.PoolRebuildStatus {
	if in == nil {
		return nil
	}

	out := &daos.PoolRebuildStatus{
		Status: int32(in.rs_errno),
	}
	if out.Status == 0 {
		out.TotalObjects = uint64(in.rs_toberb_obj_nr)
		out.Objects = uint64(in.rs_obj_nr)
		out.Records = uint64(in.rs_rec_nr)
		switch {
		case in.rs_version == 0:
			out.State = daos.PoolRebuildStateIdle
		case C.get_rebuild_state(in) == C.DRS_COMPLETED:
			out.State = daos.PoolRebuildStateDone
		default:
			out.State = daos.PoolRebuildStateBusy
		}
	}

	return out
}

func convertPoolInfo(pinfo *C.daos_pool_info_t) (*daos.PoolInfo, error) {
	poolInfo := new(daos.PoolInfo)

	poolInfo.QueryMask = daos.PoolQueryMask(pinfo.pi_bits)
	poolInfo.UUID = uuid.Must(uuidFromC(pinfo.pi_uuid))
	poolInfo.TotalTargets = uint32(pinfo.pi_ntargets)
	poolInfo.DisabledTargets = uint32(pinfo.pi_ndisabled)
	poolInfo.ActiveTargets = uint32(pinfo.pi_space.ps_ntargets)
	poolInfo.TotalEngines = uint32(pinfo.pi_nnodes)
	poolInfo.ServiceLeader = uint32(pinfo.pi_leader)
	poolInfo.Version = uint32(pinfo.pi_map_ver)
	poolInfo.State = daos.PoolServiceStateReady
	if poolInfo.DisabledTargets > 0 {
		poolInfo.State = daos.PoolServiceStateDegraded
	}

	poolInfo.Rebuild = convertPoolRebuildStatus(&pinfo.pi_rebuild_st)
	if poolInfo.QueryMask.HasOption(daos.PoolQueryOptionSpace) {
		poolInfo.TierStats = []*daos.StorageUsageStats{
			convertPoolSpaceInfo(&pinfo.pi_space, C.DAOS_MEDIA_SCM),
			convertPoolSpaceInfo(&pinfo.pi_space, C.DAOS_MEDIA_NVME),
		}
	}

	return poolInfo, nil
}

func queryPool(poolHdl C.daos_handle_t, queryMask daos.PoolQueryMask) (*daos.PoolInfo, error) {
	var rlPtr **C.d_rank_list_t = nil
	var rl *C.d_rank_list_t = nil

	if queryMask.HasOption(daos.PoolQueryOptionEnabledEngines) || queryMask.HasOption(daos.PoolQueryOptionDisabledEngines) {
		rlPtr = &rl
	}

	cPoolInfo := C.daos_pool_info_t{
		pi_bits: C.uint64_t(queryMask),
	}

	rc := C.daos_pool_query(poolHdl, rlPtr, &cPoolInfo, nil, nil)
	defer C.d_rank_list_free(rl)
	if err := daosError(rc); err != nil {
		return nil, err
	}

	poolInfo, err := convertPoolInfo(&cPoolInfo)
	if err != nil {
		return nil, err
	}

	if rlPtr != nil {
		rs, err := rankSetFromC(rl)
		if err != nil {
			return nil, err
		}
		if queryMask.HasOption(daos.PoolQueryOptionEnabledEngines) {
			poolInfo.EnabledRanks = rs
		}
		if queryMask.HasOption(daos.PoolQueryOptionDisabledEngines) {
			poolInfo.DisabledRanks = rs
		}
	}

	return poolInfo, nil
}

func (cmd *poolQueryCmd) Execute(_ []string) error {
	queryMask := daos.DefaultPoolQueryMask
	if cmd.HealthOnly {
		queryMask = daos.HealthOnlyPoolQueryMask
	}
	if cmd.ShowEnabledRanks {
		queryMask.SetOptions(daos.PoolQueryOptionEnabledEngines)
	}
	queryMask.SetOptions(daos.PoolQueryOptionDisabledEngines)

	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	poolInfo, err := queryPool(cmd.cPoolHandle, queryMask)
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

	Rank    uint32 `long:"rank" required:"1" description:"Engine rank of the targets to be queried"`
	Targets string `long:"target-idx" description:"Comma-separated list of target idx(s) to be queried"`
}

// For using the pretty printer that dmg uses for this target info.
func convertPoolTargetInfo(ptinfo *C.daos_target_info_t) (*daos.PoolQueryTargetInfo, error) {
	pqti := new(daos.PoolQueryTargetInfo)
	pqti.Type = daos.PoolQueryTargetType(ptinfo.ta_type)
	pqti.State = daos.PoolQueryTargetState(ptinfo.ta_state)
	pqti.Space = []*daos.StorageUsageStats{
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

	if len(idxList) == 0 {
		pi, err := queryPool(cmd.cPoolHandle, daos.HealthOnlyPoolQueryMask)
		if err != nil || (pi.TotalTargets == 0 || pi.TotalEngines == 0) {
			if err != nil {
				return errors.Wrap(err, "pool query failed")
			}
			return errors.New("failed to derive target count from pool query")
		}
		tgtCount := pi.TotalTargets / pi.TotalEngines
		for i := uint32(0); i < tgtCount; i++ {
			idxList = append(idxList, i)
		}
	}

	ptInfo := new(C.daos_target_info_t)
	var rc C.int

	infos := make([]*daos.PoolQueryTargetInfo, 0, len(idxList))
	for tgt := 0; tgt < len(idxList); tgt++ {
		rc = C.daos_pool_query_target(cmd.cPoolHandle, C.uint32_t(idxList[tgt]), C.uint32_t(cmd.Rank), ptInfo, nil)
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err,
				"failed to query pool %s rank:target %d:%d", cmd.poolUUID, cmd.Rank, idxList[tgt])
		}

		tgtInfo, err := convertPoolTargetInfo(ptInfo)
		if err != nil {
			return err
		}
		infos = append(infos, tgtInfo)
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

	if cmd.JSONOutputEnabled() {
		if cmd.Verbose {
			return cmd.OutputJSON(attrs.asMap(), nil)
		}
		return cmd.OutputJSON(attrs.asList(), nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for pool %s:", cmd.poolUUID)
	printAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

type poolGetAttrCmd struct {
	poolBaseCmd

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

	var attrs attrList
	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		attrs, err = listDaosAttributes(cmd.cPoolHandle, poolAttr, true)
	} else {
		attrs, err = getDaosAttributes(cmd.cPoolHandle, poolAttr, cmd.Args.Attrs.ParsedProps.ToSlice())
	}
	if err != nil {
		return errors.Wrapf(err, "failed to get attributes for pool %s", cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with older behavior.
		if len(cmd.Args.Attrs.ParsedProps) == 1 && len(attrs) == 1 {
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

	attrs := make(attrList, 0, len(cmd.Args.Attrs.ParsedProps))
	for key, val := range cmd.Args.Attrs.ParsedProps {
		attrs = append(attrs, &attribute{
			Name:  key,
			Value: []byte(val),
		})
	}

	if err := setDaosAttributes(cmd.cPoolHandle, poolAttr, attrs); err != nil {
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

func getPoolList(log logging.Logger, sysName string, queryEnabled bool) ([]*daos.PoolInfo, error) {
	var cSysName *C.char
	if sysName != "" {
		cSysName := C.CString(sysName)
		defer freeString(cSysName)
	}

	var cPools []C.daos_mgmt_pool_info_t
	for {
		var rc C.int
		var poolCount C.size_t

		// First, fetch the total number of pools in the system.
		// We may not have access to all of them, so this is an upper bound.
		rc = C.daos_mgmt_list_pools(cSysName, &poolCount, nil, nil)
		if err := daosError(rc); err != nil {
			return nil, err
		}
		log.Debugf("pools in system: %d", poolCount)

		if poolCount < 1 {
			return nil, nil
		}

		// Now, we actually fetch the pools into the buffer that we've created.
		cPools = make([]C.daos_mgmt_pool_info_t, poolCount)
		rc = C.daos_mgmt_list_pools(cSysName, &poolCount, &cPools[0], nil)
		err := daosError(rc)
		if err == nil {
			cPools = cPools[:poolCount] // adjust the slice to the number of pools retrieved
			log.Debugf("fetched %d pools", len(cPools))
			break
		}
		if err == daos.StructTooSmall {
			log.Notice("server-side pool list changed; re-fetching")
			continue
		}
		log.Errorf("failed to fetch pool list: %s", err)
		return nil, err
	}

	pools := make([]*daos.PoolInfo, 0, len(cPools))
	for i := 0; i < len(cPools); i++ {
		cPool := &cPools[i]

		svcRanks, err := rankSetFromC(cPool.mgpi_svc)
		if err != nil {
			return nil, err
		}
		poolUUID, err := uuidFromC(cPool.mgpi_uuid)
		if err != nil {
			return nil, err
		}
		poolLabel := C.GoString(cPool.mgpi_label)

		var pool *daos.PoolInfo
		if queryEnabled {
			poolHandle, poolInfo, err := poolConnect(poolUUID.String(), sysName, daos.PoolConnectFlagReadOnly, true)
			if err != nil {
				log.Errorf("failed to connect to pool %q: %s", poolLabel, err)
				continue
			}

			var qErr error
			pool, qErr = convertPoolInfo(poolInfo)
			if qErr != nil {
				log.Errorf("failed to query pool %q: %s", poolLabel, qErr)
			}
			if err := poolDisconnectAPI(poolHandle); err != nil {
				log.Errorf("failed to disconnect from pool %q: %s", poolLabel, err)
			}
			if qErr != nil {
				continue
			}

			// Add a few missing pieces that the query doesn't fill in.
			pool.Label = poolLabel
			pool.ServiceReplicas = svcRanks.Ranks()
		} else {
			// Just populate the basic info.
			pool = &daos.PoolInfo{
				UUID:            poolUUID,
				Label:           poolLabel,
				ServiceReplicas: svcRanks.Ranks(),
				State:           daos.PoolServiceStateReady,
			}
		}

		pools = append(pools, pool)
	}

	log.Debugf("fetched %d/%d pools", len(pools), len(cPools))
	return pools, nil
}

type poolListCmd struct {
	daosCmd
	Verbose bool `short:"v" long:"verbose" description:"Add pool UUIDs and service replica lists to display"`
	NoQuery bool `short:"n" long:"no-query" description:"Disable query of listed pools"`
}

func (cmd *poolListCmd) Execute(_ []string) error {
	pools, err := getPoolList(cmd.Logger, cmd.SysName, !cmd.NoQuery)
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
