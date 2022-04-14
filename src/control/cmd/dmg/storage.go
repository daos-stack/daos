//
// (C) Copyright 2019-2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/lib/control"
)

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Scan          storageScanCmd     `command:"scan" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format        storageFormatCmd   `command:"format" description:"Format SCM and NVMe storage attached to remote servers."`
	Query         storageQueryCmd    `command:"query" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
	NvmeRebind    nvmeRebindCmd      `command:"nvme-rebind" description:"Detach NVMe SSD from kernel driver and rebind to userspace driver for use with DAOS."`
	NvmeAddDevice nvmeAddDeviceCmd   `command:"nvme-add-device" description:"Add a hot-inserted NVMe SSD to a specific engine configuration to enable the new device to be used."`
	Set           setFaultyCmd       `command:"set" description:"Manually set the device state."`
	Replace       storageReplaceCmd  `command:"replace" description:"Replace a storage device that has been hot-removed with a new device."`
	Identify      storageIdentifyCmd `command:"identify" description:"Blink the status LED on a given VMD device for visual SSD identification."`
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

	cmd.log.Debugf("storage scan request: %+v", req)

	resp, err := control.StorageScan(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	cmd.log.Debugf("storage scan response: %+v", resp.HostStorage)

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
	Verbose bool `short:"v" long:"verbose" description:"Show results of each SCM & NVMe device format operation"`
	Force   bool `long:"force" description:"Force storage format on a host, stopping any running engines (CAUTION: destructive operation)"`
}

// Execute is run when storageFormatCmd activates.
//
// Run NVMe and SCM storage format on all connected servers.
func (cmd *storageFormatCmd) Execute(args []string) (err error) {
	ctx := context.Background()

	req := &control.StorageFormatReq{Reformat: cmd.Force}
	req.SetHostList(cmd.hostlist)

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

// nvmeRebindCmd is the struct representing the nvme-rebind storage subcommand.
type nvmeRebindCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	PCIAddr string `short:"a" long:"pci-address" required:"1" description:"NVMe SSD PCI address to rebind."`
}

// Execute is run when nvmeRebindCmd activates.
//
// Rebind NVMe SSD from kernel driver and bind to user-space driver on single server.
func (cmd *nvmeRebindCmd) Execute(args []string) error {
	ctx := context.Background()

	if len(cmd.hostlist) != 1 {
		return errors.New("command expects a single host in hostlist")
	}

	req := &control.NvmeRebindReq{PCIAddr: cmd.PCIAddr}
	req.SetHostList(cmd.hostlist)

	resp, err := control.StorageNvmeRebind(ctx, cmd.ctlInvoker, req)
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
	} else {
		cmd.log.Info("Command completed successfully")
	}

	return resp.Errors()
}

// nvmeAddDeviceCmd is the struct representing the nvme-add-device storage subcommand.
//
// StorageTierIndex is by default set -1 to signal the server to add the device to the first
// configured bdev tier.
type nvmeAddDeviceCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	PCIAddr          string `short:"a" long:"pci-address" required:"1" description:"NVMe SSD PCI address to add."`
	EngineIndex      uint32 `short:"e" long:"engine-index" required:"1" description:"Index of DAOS engine to add NVMe device to."`
	StorageTierIndex int32  `short:"t" long:"tier-index" default:"-1" description:"Index of storage tier on DAOS engine to add NVMe device to."`
}

// Execute is run when nvmeAddDeviceCmd activates.
//
// Add recently inserted NVMe SSD to a running engine by updating relevant NVMe config file.
func (cmd *nvmeAddDeviceCmd) Execute(args []string) error {
	ctx := context.Background()

	if len(cmd.hostlist) != 1 {
		return errors.New("command expects a single host in hostlist")
	}

	req := &control.NvmeAddDeviceReq{
		PCIAddr:          cmd.PCIAddr,
		EngineIndex:      cmd.EngineIndex,
		StorageTierIndex: cmd.StorageTierIndex,
	}
	req.SetHostList(cmd.hostlist)

	cmd.log.Debugf("nvme add device req: %+v", req)
	resp, err := control.StorageNvmeAddDevice(ctx, cmd.ctlInvoker, req)
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
	} else {
		cmd.log.Info("Command completed successfully")
	}

	return resp.Errors()
}

// setFaultyCmd is the struct representing the set storage subcommand
type setFaultyCmd struct {
	NVMe nvmeSetFaultyCmd `command:"nvme-faulty" description:"Manually set the device state of an NVMe SSD to FAULTY."`
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
	NVMe nvmeReplaceCmd `command:"nvme" description:"Replace an evicted/FAULTY NVMe SSD with another device."`
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
	VMD vmdIdentifyCmd `command:"vmd" description:"Quickly blink the status LED on a VMD NVMe SSD for device identification. Duration of LED event can be configured by setting the VMD_LED_PERIOD environment variable, otherwise default is 60 seconds."`
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
