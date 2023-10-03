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
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Scan          storageScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format        storageFormatCmd  `command:"format" description:"Format SCM and NVMe storage attached to remote servers."`
	Query         storageQueryCmd   `command:"query" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
	NvmeRebind    nvmeRebindCmd     `command:"nvme-rebind" description:"Detach NVMe SSD from kernel driver and rebind to userspace driver for use with DAOS."`
	NvmeAddDevice nvmeAddDeviceCmd  `command:"nvme-add-device" description:"Add a hot-inserted NVMe SSD to a specific engine configuration to enable the new device to be used."`
	Set           setFaultyCmd      `command:"set" description:"Manually set the device state."`
	Replace       storageReplaceCmd `command:"replace" description:"Replace a storage device that has been hot-removed with a new device."`
	LedManage     ledManageCmd      `command:"led" description:"Manage LED status for supported drives."`
}

// storageScanCmd is the struct representing the scan storage subcommand.
type storageScanCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
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
	req.SetHostList(cmd.getHostList())

	cmd.Debugf("storage scan request: %+v", req)

	resp, err := control.StorageScan(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	cmd.Debugf("storage scan response: %+v", resp.HostStorage)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.Error(outErr.String())
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
	cmd.Info(out.String())

	return resp.Errors()
}

// storageFormatCmd is the struct representing the format storage subcommand.
type storageFormatCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	Verbose bool `short:"v" long:"verbose" description:"Show results of each SCM & NVMe device format operation"`
	Force   bool `long:"force" description:"Force storage format on a host, stopping any running engines (CAUTION: destructive operation)"`
}

// Execute is run when storageFormatCmd activates.
//
// Run NVMe and SCM storage format on all connected servers.
func (cmd *storageFormatCmd) Execute(args []string) (err error) {
	ctx := context.Background()

	req := &control.StorageFormatReq{Reformat: cmd.Force}
	req.SetHostList(cmd.getHostList())

	resp, err := control.StorageFormat(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	return cmd.printFormatResp(resp)
}

func (cmd *storageFormatCmd) printFormatResp(resp *control.StorageFormatResp) error {
	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.Error(outErr.String())
	}

	var out strings.Builder
	verbose := pretty.PrintWithVerboseOutput(cmd.Verbose)
	if err := pretty.PrintStorageFormatMap(resp.HostStorage, &out, verbose); err != nil {
		return err
	}
	cmd.Info(out.String())

	return resp.Errors()
}

// nvmeRebindCmd is the struct representing the nvme-rebind storage subcommand.
type nvmeRebindCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	PCIAddr string `short:"a" long:"pci-address" required:"1" description:"NVMe SSD PCI address to rebind."`
}

// Execute is run when nvmeRebindCmd activates.
//
// Rebind NVMe SSD from kernel driver and bind to user-space driver on single server.
func (cmd *nvmeRebindCmd) Execute(args []string) error {
	ctx := context.Background()

	if len(cmd.getHostList()) != 1 {
		return errors.New("command expects a single host in hostlist")
	}

	req := &control.NvmeRebindReq{PCIAddr: cmd.PCIAddr}
	req.SetHostList(cmd.getHostList())

	resp, err := control.StorageNvmeRebind(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.Error(outErr.String())
	} else {
		cmd.Info("Command completed successfully")
	}

	return resp.Errors()
}

// nvmeAddDeviceCmd is the struct representing the nvme-add-device storage subcommand.
//
// StorageTierIndex is by default set -1 to signal the server to add the device to the first
// configured bdev tier.
type nvmeAddDeviceCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	PCIAddr          string `short:"a" long:"pci-address" required:"1" description:"NVMe SSD PCI address to add."`
	EngineIndex      uint32 `short:"e" long:"engine-index" required:"1" description:"Index of DAOS engine to add NVMe device to."`
	StorageTierIndex int32  `short:"t" long:"tier-index" default:"-1" description:"Index of storage tier on DAOS engine to add NVMe device to."`
}

// Execute is run when nvmeAddDeviceCmd activates.
//
// Add recently inserted NVMe SSD to a running engine by updating relevant NVMe config file.
func (cmd *nvmeAddDeviceCmd) Execute(args []string) error {
	ctx := context.Background()

	if len(cmd.getHostList()) != 1 {
		return errors.New("command expects a single host in hostlist")
	}

	req := &control.NvmeAddDeviceReq{
		PCIAddr:          cmd.PCIAddr,
		EngineIndex:      cmd.EngineIndex,
		StorageTierIndex: cmd.StorageTierIndex,
	}
	req.SetHostList(cmd.getHostList())

	cmd.Debugf("nvme add device req: %+v", req)
	resp, err := control.StorageNvmeAddDevice(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.Error(outErr.String())
	} else {
		cmd.Info("Command completed successfully")
	}

	return resp.Errors()
}
