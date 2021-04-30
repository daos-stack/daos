//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Prepare  storagePrepareCmd  `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan     storageScanCmd     `command:"scan" alias:"s" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format   storageFormatCmd   `command:"format" alias:"f" description:"Format SCM and NVMe storage attached to remote servers."`
	Query    storageQueryCmd    `command:"query" alias:"q" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
	Set      setFaultyCmd       `command:"set" alias:"s" description:"Manually set the device state."`
	Replace  storageReplaceCmd  `command:"replace" alias:"r" description:"Replace a storage device that has been hot-removed with a new device."`
	Identify storageIdentifyCmd `command:"identify" alias:"i" description:"Blink the status LED on a given VMD device for visual SSD identification."`
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

	req := &control.StoragePrepareReq{}
	if prepNvme {
		cmd.log.Debug("setting nvme in storage prepare request")
		req.NVMe = &control.NvmePrepareReq{
			PCIAllowList: cmd.PCIAllowList,
			NrHugePages:  int32(cmd.NrHugepages),
			TargetUser:   cmd.TargetUser,
			Reset:        cmd.Reset,
		}
	}
	if prepScm {
		cmd.log.Debug("setting scm in storage prepare request")
		if cmd.jsonOutputEnabled() && !cmd.Force {
			return errors.New("Cannot use --json without --force")
		}
		if err := cmd.Warn(cmd.log); err != nil {
			return err
		}
		req.SCM = &control.ScmPrepareReq{Reset: cmd.Reset}
	}

	req.SetHostList(cmd.hostlist)
	resp, err := control.StoragePrepare(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.log.Error(outErr.String())
	}

	if prepScm {
		var out strings.Builder
		if err := pretty.PrintScmPrepareMap(resp.HostStorage, &out); err != nil {
			return err
		}
		cmd.log.Info(out.String())
	}

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
	NvmeMeta   bool `short:"m" long:"nvme-meta" description:"Display server meta data held on NVMe storage"`
}

// Execute is run when storageScanCmd activates.
//
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(_ []string) error {
	if cmd.NvmeHealth && cmd.NvmeMeta {
		return errors.New("cannot use --nvme-health and --nvme-meta together")
	}
	if cmd.Verbose && (cmd.NvmeHealth || cmd.NvmeMeta) {
		return errors.New("cannot use --verbose with --nvme-health or --nvme-meta")
	}

	req := &control.StorageScanReq{
		NvmeHealth: cmd.NvmeHealth,
		NvmeMeta:   cmd.NvmeMeta,
		// don't strip nvme details if verbose or health or meta set
		NvmeBasic: !(cmd.Verbose || cmd.NvmeHealth || cmd.NvmeMeta),
	}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StorageScan(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.log.Error(outErr.String())
	}

	var out strings.Builder
	switch {
	case cmd.NvmeHealth:
		if err := pretty.PrintNvmeHealthMap(resp.HostStorage, &out); err != nil {
			return err
		}
	case cmd.NvmeMeta:
		if err := pretty.PrintNvmeMetaMap(resp.HostStorage, &out); err != nil {
			return err
		}
	default:
		verbose := pretty.PrintWithVerboseOutput(cmd.Verbose)
		if err := pretty.PrintHostStorageMap(resp.HostStorage, &out, verbose); err != nil {
			return err
		}
	}
	cmd.log.Info(out.String())

	return resp.Errors()
}

// storageFormatCmd is the struct representing the format storage subcommand.
type storageFormatCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	Verbose  bool `short:"v" long:"verbose" description:"Show results of each SCM & NVMe device format operation"`
	Reformat bool `long:"reformat" description:"Alias for --force, will be removed in a future release"`
	Force    bool `long:"force" description:"Force storage format on a host, stopping any running engines (CAUTION: destructive operation)"`
}

// Execute is run when storageFormatCmd activates.
//
// Run NVMe and SCM storage format on all connected servers.
func (cmd *storageFormatCmd) Execute(args []string) (err error) {
	ctx := context.Background()

	req := &control.StorageFormatReq{Reformat: cmd.Force}
	req.SetHostList(cmd.hostlist)

	// TODO (DAOS-7080): Deprecate this parameter in favor of wiping SCM
	// during the erase operation. For the moment, though, the reworked
	// logic will prevent format of a running system, so the main use case
	// here is to enable backward-compatibility for existing scripts.
	if cmd.Reformat {
		req.Reformat = true
	}

	resp, err := control.StorageFormat(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	return cmd.printFormatResp(resp)
}

func (cmd *storageFormatCmd) printFormatResp(resp *control.StorageFormatResp) error {
	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.log.Error(outErr.String())
	}

	var out strings.Builder
	verbose := pretty.PrintWithVerboseOutput(cmd.Verbose)
	if err := pretty.PrintStorageFormatMap(resp.HostStorage, &out, verbose); err != nil {
		return err
	}
	cmd.log.Info(out.String())

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
			return errors.New("Cannot use --json without --force")
		}
		if !common.GetConsent(cmd.log) {
			return errors.New("consent not given")
		}
	}

	req := &control.SmdQueryReq{
		UUID:      cmd.UUID,
		SetFaulty: true,
	}
	return cmd.makeRequest(context.Background(), req)
}

// storageReplaceCmd is the struct representing the replace storage subcommand
type storageReplaceCmd struct {
	NVMe nvmeReplaceCmd `command:"nvme" alias:"n" description:"Replace an evicted/FAULTY NVMe SSD with another device."`
}

// nvmeReplaceCmd is the struct representing the replace nvme storage subcommand
type nvmeReplaceCmd struct {
	smdQueryCmd
	OldDevUUID string `long:"old-uuid" description:"Device UUID of hot-removed SSD" required:"1"`
	NewDevUUID string `long:"new-uuid" description:"Device UUID of new device" required:"1"`
	NoReint    bool   `long:"no-reint" description:"Bypass reintegration of device and just bring back online."`
}

// Execute is run when storageReplaceCmd activates
// Replace a hot-removed device with a newly plugged device, or reuse a FAULTY device
func (cmd *nvmeReplaceCmd) Execute(_ []string) error {
	if cmd.OldDevUUID == cmd.NewDevUUID {
		cmd.log.Info("WARNING: Attempting to reuse a previously set FAULTY device!")
	}

	// TODO: Implement no-reint flag option
	if cmd.NoReint {
		cmd.log.Info("NoReint is not currently implemented")
	}

	req := &control.SmdQueryReq{
		UUID:        cmd.OldDevUUID,
		ReplaceUUID: cmd.NewDevUUID,
		NoReint:     cmd.NoReint,
	}
	return cmd.makeRequest(context.Background(), req)
}

// storageIdentifyCmd is the struct representing the identify storage subcommand.
type storageIdentifyCmd struct {
	VMD vmdIdentifyCmd `command:"vmd" alias:"n" description:"Quickly blink the status LED on a VMD NVMe SSD for device identification."`
}

// vmdIdentifyCmd is the struct representing the identify vmd storage subcommand.
type vmdIdentifyCmd struct {
	smdQueryCmd
	UUID string `long:"uuid" description:"Device UUID of the VMD device to identify" required:"1"`
}

// Execute is run when vmdIdentifyCmd activates.
//
// Runs SPDK VMD API commands to set the LED state on the VMD to "IDENTIFY"
func (cmd *vmdIdentifyCmd) Execute(_ []string) error {
	req := &control.SmdQueryReq{
		UUID:     cmd.UUID,
		Identify: true,
	}
	return cmd.makeRequest(context.Background(), req)
}
