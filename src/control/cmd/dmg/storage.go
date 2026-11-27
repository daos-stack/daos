//
// (C) Copyright 2019-2023 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Scan            storageScanCmd            `command:"scan" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format          storageFormatCmd          `command:"format" description:"Format SCM and NVMe storage attached to remote servers."`
	Query           storageQueryCmd           `command:"query" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
	NvmeRebind      nvmeRebindCmd             `command:"nvme-rebind" description:"Detach NVMe SSD from kernel driver and rebind to userspace driver for use with DAOS."`
	NvmeAddDevice   nvmeAddDeviceCmd          `command:"nvme-add-device" description:"Add a hot-inserted NVMe SSD to a specific engine configuration to enable the new device to be used."`
	Set             setFaultyCmd              `command:"set" description:"Manually set the device state."`
	Replace         storageReplaceCmd         `command:"replace" description:"Replace a storage device that has been hot-removed with a new device."`
	LedManage       ledManageCmd              `command:"led" description:"Manage LED status for supported drives."`
	PrepareReformat storagePrepareReformatCmd `command:"prepare-reformat" description:"Remove the superblock of a stopped engine, selected by rank, to prepare it for reformat."`
}

// storageScanCmd is the struct representing the scan storage subcommand.
type storageScanCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	Verbose    bool `short:"v" long:"verbose" description:"List SCM & NVMe device details"`
	NvmeHealth bool `short:"n" long:"nvme-health" description:"Display NVMe device health statistics"`
}

// Execute is run when storageScanCmd activates.
//
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(_ []string) error {
	if cmd.Verbose && cmd.NvmeHealth {
		return errors.New("cannot use --verbose with --nvme-health")
	}

	req := &control.StorageScanReq{
		NvmeHealth: cmd.NvmeHealth,
		// Strip nvme details if verbose and health flags are unset.
		NvmeBasic: !(cmd.Verbose || cmd.NvmeHealth),
	}
	req.SetHostList(cmd.getHostList())

	cmd.Debugf("storage scan request: %+v", req)

	resp, err := control.StorageScan(cmd.MustLogCtx(), cmd.ctlInvoker, req)
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
	if cmd.NvmeHealth {
		if err := pretty.PrintNvmeHealthMap(resp.HostStorage, &out); err != nil {
			return err
		}
	} else {
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
	Verbose bool    `short:"v" long:"verbose" description:"Show results of each SCM & NVMe device format operation"`
	Force   bool    `long:"force" description:"Force storage format on a host, stopping any running engines (CAUTION: destructive operation)"`
	Replace bool    `long:"replace" description:"Replace an excluded rank. Allows a DAOS engine instance to reclaim its old rank number after metadata is lost due to PMem or other storage media failure"`
	Rank    *uint32 `long:"rank" description:"Specific rank to replace (only valid with --replace)"`
}

// Execute is run when storageFormatCmd activates.
//
// Run NVMe and SCM storage format on all connected servers.
func (cmd *storageFormatCmd) Execute(args []string) (err error) {
	ctx := cmd.MustLogCtx()

	if cmd.Replace && cmd.Force {
		return errIncompatFlags("replace", "force")
	}

	if cmd.Replace && len(cmd.getHostList()) != 1 {
		return errors.New("command expects a single host in hostlist if replace option used")
	}

	// Default to NilRank if no rank specified
	rank := uint32(ranklist.NilRank)
	if cmd.Rank != nil {
		if !cmd.Replace {
			return errors.New("--rank option is only valid when used with --replace")
		}
		rank = *cmd.Rank
	}

	req := &control.StorageFormatReq{Reformat: cmd.Force, Replace: cmd.Replace, Rank: rank}
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
	ctx := cmd.MustLogCtx()

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
	ctx := cmd.MustLogCtx()

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

// storagePrepareReformatCmd is the struct representing the prepare-reformat storage subcommand.
type storagePrepareReformatCmd struct {
	baseCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd
	Rank uint32 `short:"r" long:"rank" required:"1" description:"Rank of the stopped DAOS engine to prepare for reformat; its host is resolved from the system membership"`
}

// Execute is run when storagePrepareReformatCmd activates.
//
// Remove the superblock of the stopped engine with the given rank, so that the engine requests a
// format when it is next started.
func (cmd *storagePrepareReformatCmd) Execute(args []string) error {
	ctx := cmd.MustLogCtx()

	rank := ranklist.Rank(cmd.Rank)
	host, err := resolveStoragePrepareReformatHost(ctx, cmd.ctlInvoker, rank)
	if err != nil {
		return err
	}

	req := &control.StoragePrepareReformatReq{Rank: cmd.Rank}
	req.SetHostList([]string{host})

	cmd.Debugf("prepare reformat req: %+v", req)
	resp, err := control.StoragePrepareReformat(ctx, cmd.ctlInvoker, req)
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
		cmd.Info(prepareReformatSuccessMsg(rank, host))
	}

	return resp.Errors()
}

// prepareReformatSuccessMsg reports how to start the engine so that it requests a format. The
// rank is no longer known to the server once its superblock is removed, so "dmg system start"
// cannot be used and daos_server has to be restarted on the host instead.
func prepareReformatSuccessMsg(rank ranklist.Rank, host string) string {
	return fmt.Sprintf("Rank %d on host %s prepared for reformat (superblock removed).\n"+
		"Start the engine by restarting daos_server on that host (\"dmg system start\" "+
		"cannot start it); the engine will then wait for \"dmg storage format\".",
		rank, host)
}

// resolveStoragePrepareReformatHost returns the control address of the host that runs the
// engine with the given rank, as reported by the management service.
func resolveStoragePrepareReformatHost(ctx context.Context, rpc control.UnaryInvoker,
	rank ranklist.Rank,
) (string, error) {
	queryReq := &control.SystemQueryReq{}
	ranks := ranklist.NewRankSet()
	ranks.Add(rank)
	queryReq.SetRanks(ranks)
	queryResp, err := control.SystemQuery(ctx, rpc, queryReq)
	if err != nil {
		return "", errors.Wrapf(err, "resolving host for rank %d", rank)
	}

	var host string
	for _, member := range queryResp.Members {
		if member == nil || member.Rank != rank {
			continue
		}
		if member.Addr == nil {
			return "", errors.Errorf("system query returned no control address for rank %d", rank)
		}
		if host != "" && host != member.Addr.String() {
			return "", errors.Errorf("system query returned multiple hosts for rank %d", rank)
		}
		host = member.Addr.String()
	}
	if host == "" {
		return "", errors.Errorf("rank %d not found in system query response", rank)
	}

	return host, nil
}
