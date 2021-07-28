//
// (C) Copyright 2021 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ui"
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

func (cmd *poolBaseCmd) poolUUIDPtr() *C.uchar {
	if cmd.poolUUID == uuid.Nil {
		cmd.log.Errorf("poolUUIDPtr(): nil UUID")
		return nil
	}
	return (*C.uchar)(unsafe.Pointer(&cmd.poolUUID[0]))
}

func (cmd *poolBaseCmd) PoolID() PoolID {
	if !cmd.PoolFlag.Empty() {
		return cmd.PoolFlag
	}
	return cmd.Args.Pool
}

func (cmd *poolBaseCmd) connectPool() error {
	sysName := cmd.SysName
	if sysName == "" {
		sysName = build.DefaultSystemName
	}
	cSysName := C.CString(sysName)
	defer freeString(cSysName)

	var rc C.int
	switch {
	case cmd.PoolID().HasLabel():
		var poolInfo C.daos_pool_info_t
		cLabel := C.CString(cmd.PoolID().Label)
		defer freeString(cLabel)

		cmd.log.Debugf("connecting to pool: %s", cmd.PoolID().Label)
		rc = C.daos_pool_connect2(cLabel, cSysName, C.DAOS_PC_RW,
			&cmd.cPoolHandle, &poolInfo, nil)
		if rc == 0 {
			var err error
			cmd.poolUUID, err = uuidFromC(poolInfo.pi_uuid)
			if err != nil {
				cmd.disconnectPool()
				return err
			}
		}
	case cmd.PoolID().HasUUID():
		cmd.poolUUID = cmd.PoolID().UUID
		cmd.log.Debugf("connecting to pool: %s", cmd.poolUUID)
		cUUIDstr := C.CString(cmd.poolUUID.String())
		defer freeString(cUUIDstr)
		rc = C.daos_pool_connect2(cUUIDstr, cSysName, C.DAOS_PC_RW,
			&cmd.cPoolHandle, nil, nil)
	default:
		return errors.New("no pool UUID or label supplied")
	}

	return daosError(rc)
}

func (cmd *poolBaseCmd) disconnectPool() {
	cmd.log.Debugf("disconnecting pool %s", cmd.PoolID())
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_pool_disconnect(cmd.cPoolHandle, nil)
	if rc == -C.DER_NOMEM {
		rc = C.daos_pool_disconnect(cmd.cPoolHandle, nil)
	}

	if err := daosError(rc); err != nil {
		cmd.log.Errorf("pool disconnect failed: %s", err)
	}
}

func (cmd *poolBaseCmd) resolveAndConnect(ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.connectPool(); err != nil {
		return nil, errors.Wrapf(err,
			"failed to connect to pool %s", cmd.PoolID())
	}

	if ap != nil {
		if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
			return nil, err
		}
		ap.pool = cmd.cPoolHandle
	}

	return func() {
		cmd.disconnectPool()
	}, nil
}

func (cmd *poolBaseCmd) getAttr(name string) (*attribute, error) {
	return getDaosAttribute(cmd.cPoolHandle, poolAttr, name)
}

type poolCmd struct {
	Query     poolQueryCmd     `command:"query" description:"query pool info"`
	ListAttrs poolListAttrsCmd `command:"list-attr" alias:"list-attrs" alias:"lsattr" description:"list pool user-defined attributes"`
	GetAttr   poolGetAttrCmd   `command:"get-attr" alias:"getattr" description:"get pool user-defined attribute"`
	SetAttr   poolSetAttrCmd   `command:"set-attr" alias:"setattr" description:"set pool user-defined attribute"`
	DelAttr   poolDelAttrCmd   `command:"del-attr" alias:"delattr" description:"delete pool user-defined attribute"`
	AutoTest  poolAutoTestCmd  `command:"autotest" description:"verify setup with smoke tests"`
}

type poolQueryCmd struct {
	poolBaseCmd
}

func convertPoolSpaceInfo(in *C.struct_daos_pool_space, mt C.uint) *mgmtpb.StorageUsageStats {
	if in == nil {
		return nil
	}

	return &mgmtpb.StorageUsageStats{
		Total: uint64(in.ps_space.s_total[mt]),
		Free:  uint64(in.ps_space.s_free[mt]),
		Min:   uint64(in.ps_free_min[mt]),
		Max:   uint64(in.ps_free_max[mt]),
		Mean:  uint64(in.ps_free_mean[mt]),
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
		case in.rs_done == 1:
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
	pqp.TotalNodes = uint32(pinfo.pi_nnodes)
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

func (cmd *poolQueryCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	pinfo := C.daos_pool_info_t{
		pi_bits: dpiQueryAll,
	}
	rc := C.daos_pool_query(cmd.cPoolHandle, nil, &pinfo, nil, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to query pool %s", cmd.poolUUID)
	}

	pqr, err := convertPoolInfo(&pinfo)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(pqr, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintPoolQueryResponse(pqr, &bld); err != nil {
		return err
	}

	cmd.log.Info(bld.String())

	return nil
}

type poolListAttrsCmd struct {
	poolBaseCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *poolListAttrsCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
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
		return cmd.outputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for pool %s:", cmd.poolUUID)
	printAttributes(&bld, title, attrs...)

	cmd.log.Info(bld.String())

	return nil
}

type poolGetAttrCmd struct {
	poolBaseCmd

	Args struct {
		Name string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *poolGetAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
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

	cmd.log.Info(bld.String())

	return nil
}

type poolSetAttrCmd struct {
	poolBaseCmd

	Args struct {
		Name  string `positional-arg-name:"<attribute name>"`
		Value string `positional-arg-name:"<attribute value>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *poolSetAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := setDaosAttribute(cmd.cPoolHandle, poolAttr, &attribute{
		Name:  cmd.Args.Name,
		Value: cmd.Args.Value,
	}); err != nil {
		return errors.Wrapf(err,
			"failed to set attribute %q on pool %s",
			cmd.Args.Name, cmd.poolUUID)
	}

	return nil
}

type poolDelAttrCmd struct {
	poolBaseCmd

	Args struct {
		Name string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *poolDelAttrCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
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
}

func (cmd *poolAutoTestCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(nil)
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

	rc := C.pool_autotest_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to run autotest for pool %s",
			cmd.poolUUID)
	}

	return nil
}
