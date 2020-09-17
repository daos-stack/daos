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
	"strings"

	"github.com/dustin/go-humanize/english"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/system"
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
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(err)
		}
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
		if cmd.jsonOutputEnabled() && !cmd.Force {
			return cmd.errorJSON(errors.New("Cannot use --json without --force"))
		}
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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
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
	Verbose    bool `short:"v" long:"verbose" description:"List SCM & NVMe device details"`
	NvmeHealth bool `short:"n" long:"nvme-health" description:"Display NVMe device health statistics"`
}

// Execute is run when storageScanCmd activates.
//
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.StorageScanReq{ConfigDevicesOnly: cmd.NvmeHealth}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StorageScan(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		if cmd.Verbose {
			cmd.log.Debug("--verbose flag ignored if --json specified")
		}
		if cmd.NvmeHealth {
			cmd.log.Debug("--health flag ignored if --json specified")
		}
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	verbose := control.PrintWithVerboseOutput(cmd.Verbose)
	if err := control.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if cmd.NvmeHealth {
		if cmd.Verbose {
			cmd.log.Debug("--verbose flag ignored if --health specified")
		}
		if err := pretty.PrintNvmeHealthMap(resp.HostStorage, &bld); err != nil {
			return err
		}
		cmd.log.Info(bld.String())

		return resp.Errors()
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
	Reformat bool `long:"reformat" description:"Reformat storage overwriting any existing filesystem (CAUTION: destructive operation)"`
}

// shouldReformatSystem queries system to interrogate membership before deciding
// whether a system reformat is appropriate.
//
// Reformat system if membership is not empty and all member ranks are stopped.
func (cmd *storageFormatCmd) shouldReformatSystem(ctx context.Context) (bool, error) {
	if cmd.Reformat {
		resp, err := control.SystemQuery(ctx, cmd.ctlInvoker, &control.SystemQueryReq{})
		if err != nil {
			return false, errors.Wrap(err, "System-Query command failed")
		}

		if len(resp.Members) == 0 {
			cmd.log.Debug("no system members, reformat host list")

			return false, nil
		}

		notStoppedRanks, err := system.CreateRankSet("")
		if err != nil {
			return false, err
		}
		for _, member := range resp.Members {
			if member.State() != system.MemberStateStopped {
				if err := notStoppedRanks.Add(member.Rank); err != nil {
					return false, errors.Wrap(err, "adding to rank set")
				}
			}
		}
		if notStoppedRanks.Count() > 0 {
			return false, errors.Errorf(
				"system reformat requires the following %s to be stopped: %s",
				english.Plural(notStoppedRanks.Count(), "rank", "ranks"),
				notStoppedRanks.String())
		}

		return true, nil
	}

	return false, nil
}

func (cmd *storageFormatCmd) systemReformat(ctx context.Context) error {
	resp, err := control.SystemReformat(ctx, cmd.ctlInvoker,
		new(control.SystemResetFormatReq))

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	return cmd.printFormatResp(resp)
}

// Execute is run when storageFormatCmd activates.
//
// Run NVMe and SCM storage format on all connected servers.
func (cmd *storageFormatCmd) Execute(args []string) (err error) {
	ctx := context.Background()

	sysReformat, err := cmd.shouldReformatSystem(ctx)
	if err != nil {
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(err)
		}
		return err
	}
	if sysReformat {
		return cmd.systemReformat(ctx)
	}

	req := &control.StorageFormatReq{Reformat: cmd.Reformat}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StorageFormat(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	return cmd.printFormatResp(resp)
}

func (cmd *storageFormatCmd) printFormatResp(resp *control.StorageFormatResp) error {
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
	smdQueryCmd
	UUID  string `short:"u" long:"uuid" description:"Device UUID to set" required:"1"`
	Force bool   `short:"f" long:"force" description:"Do not require confirmation"`
}

// Execute is run when nvmeSetFaultyCmd activates
// Set the SMD device state of the given device to "FAULTY"
func (cmd *nvmeSetFaultyCmd) Execute(_ []string) error {
	cmd.log.Info("WARNING: This command will permanently mark the device as unusable!")
	if !cmd.Force {
		if cmd.jsonOutputEnabled() {
			return cmd.errorJSON(errors.New("Cannot use --json without --force"))
		}
		if !common.GetConsent(cmd.log) {
			return errors.New("consent not given")
		}
	}

	ctx := context.Background()
	req := &control.SmdQueryReq{
		UUID:      cmd.UUID,
		SetFaulty: true,
	}
	return cmd.makeRequest(ctx, req)
}
