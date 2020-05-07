//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"context"
	"os"
	"strings"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/control"
)

const (
	rowFieldSep = "/"
)

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Prepare storagePrepareCmd `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan    storageScanCmd    `command:"scan" alias:"s" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format  storageFormatCmd  `command:"format" alias:"f" description:"Format SCM and NVMe storage attached to remote servers."`
	Query   storageQueryCmd   `command:"query" alias:"q" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
	Set     setFaultyCmd      `command:"set" alias:"s" description:"Manually set the device state."`
}

// storagePrepareCmd is the struct representing the prep storage subcommand.
type storagePrepareCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	types.StoragePrepareCmd
}

// Execute is run when storagePrepareCmd activates
func (cmd *storagePrepareCmd) Execute(args []string) error {
	prepNvme, prepScm, err := cmd.Validate()
	if err != nil {
		return err
	}

	var nReq *control.NvmePrepareReq
	var sReq *control.ScmPrepareReq
	if prepNvme {
		nReq = &control.NvmePrepareReq{
			PCIWhiteList: cmd.PCIWhiteList,
			NrHugePages:  int32(cmd.NrHugepages),
			TargetUser:   cmd.TargetUser,
			Reset:        cmd.Reset,
		}
	}

	if prepScm {
		if err := cmd.Warn(cmd.log); err != nil {
			return err
		}

		sReq = &control.ScmPrepareReq{Reset: cmd.Reset}
	}

	ctx := context.Background()
	req := &control.StoragePrepareReq{
		NVMe: nReq,
		SCM:  sReq,
	}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StoragePrepare(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	var bld strings.Builder
	if err := control.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if err := control.PrintStoragePrepareMap(resp.HostStorage, &bld); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return resp.Errors()
}

// storageScanCmd is the struct representing the scan storage subcommand.
type storageScanCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	Verbose bool `short:"v" long:"verbose" description:"List SCM & NVMe device details"`
}

// Execute is run when storageScanCmd activates.
//
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.StorageScanReq{}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StorageScan(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	var bld strings.Builder
	verbose := control.PrintWithVerboseOutput(cmd.Verbose)
	if err := control.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if err := control.PrintHostStorageMap(resp.HostStorage, &bld, verbose); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return resp.Errors()
}

// storageFormatCmd is the struct representing the format storage subcommand.
type storageFormatCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	Verbose  bool `short:"v" long:"verbose" description:"Show results of each SCM & NVMe device format operation"`
	Reformat bool `long:"reformat" description:"Always reformat storage (CAUTION: Potentially destructive)"`
}

// Execute is run when storageFormatCmd activates
//
// run NVMe and SCM storage format on all connected servers
func (cmd *storageFormatCmd) Execute(args []string) error {
	ctx := context.Background()
	req := &control.StorageFormatReq{
		Reformat: cmd.Reformat,
	}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StorageFormat(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	var bld strings.Builder
	verbose := control.PrintWithVerboseOutput(cmd.Verbose)
	if err := control.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if err := control.PrintStorageFormatMap(resp.HostStorage, &bld, verbose); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return resp.Errors()
}

// setFaultyCmd is the struct representing the set storage subcommand
type setFaultyCmd struct {
	NVMe nvmeSetFaultyCmd `command:"nvme-faulty" alias:"n" description:"Manually set the device state of an NVMe SSD to FAULTY."`
}

// nvmeSetFaultyCmd is the struct representing the set-faulty storage subcommand
type nvmeSetFaultyCmd struct {
	logCmd
	connectedCmd
	Devuuid string `short:"u" long:"devuuid" description:"Device/Blobstore UUID to set" required:"1"`
}

// Execute is run when nvmeSetFaultyCmd activates
// Set the SMD device state of the given device to "FAULTY"
func (s *nvmeSetFaultyCmd) Execute(args []string) error {
	// Devuuid is a required command parameter
	req := &mgmtpb.DevStateReq{DevUuid: s.Devuuid}

	s.log.Infof("Device State Info:\n%s\n", s.conns.StorageSetFaulty(req))

	return nil
}
