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
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

type rankCmd struct {
	Rank *uint32 `short:"r" long:"rank" description:"Constrain operation to the specified server rank"`
}

func (r *rankCmd) GetRank() ranklist.Rank {
	if r.Rank == nil {
		return ranklist.NilRank
	}
	return ranklist.Rank(*r.Rank)
}

type smdQueryCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
}

func (cmd *smdQueryCmd) makeRequest(ctx context.Context, req *control.SmdQueryReq, opts ...pretty.PrintConfigOption) error {
	req.SetHostList(cmd.getHostList())
	resp, err := control.SmdQuery(ctx, cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintResponseErrors(resp, &bld, opts...); err != nil {
		return err
	}
	if err := pretty.PrintSmdInfoMap(req.OmitDevices, req.OmitPools, resp.HostStorage, &bld, opts...); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return resp.Errors()
}

// storageQueryCmd is the struct representing the storage query subcommand
type storageQueryCmd struct {
	DeviceHealth devHealthQueryCmd   `command:"device-health" description:"Query the device health"`
	ListPools    listPoolsQueryCmd   `command:"list-pools" description:"List pools on the server"`
	ListDevices  listDevicesQueryCmd `command:"list-devices" description:"List storage devices on the server"`
	Usage        usageQueryCmd       `command:"usage" description:"Show SCM & NVMe storage space utilization per storage server"`
}

type devHealthQueryCmd struct {
	smdQueryCmd
	UUID string `short:"u" long:"uuid" required:"1" description:"Device UUID"`
}

func (cmd *devHealthQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.SmdQueryReq{
		OmitPools:        true,
		IncludeBioHealth: true,
		Rank:             ranklist.NilRank,
		UUID:             cmd.UUID,
	}
	return cmd.makeRequest(ctx, req)
}

type listDevicesQueryCmd struct {
	smdQueryCmd
	rankCmd
	Health      bool   `short:"b" long:"health" description:"Include device health in results"`
	UUID        string `short:"u" long:"uuid" description:"Device UUID (all devices if blank)"`
	EvictedOnly bool   `short:"e" long:"show-evicted" description:"Show only evicted faulty devices"`
}

func (cmd *listDevicesQueryCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := &control.SmdQueryReq{
		OmitPools:        true,
		IncludeBioHealth: cmd.Health,
		Rank:             cmd.GetRank(),
		UUID:             cmd.UUID,
		FaultyDevsOnly:   cmd.EvictedOnly,
	}
	return cmd.makeRequest(ctx, req)
}

type listPoolsQueryCmd struct {
	smdQueryCmd
	rankCmd
	UUID    string `short:"u" long:"uuid" description:"Pool UUID (all pools if blank)"`
	Verbose bool   `short:"v" long:"verbose" description:"Show more detail about pools"`
}

func (cmd *listPoolsQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.SmdQueryReq{
		OmitDevices: true,
		Rank:        cmd.GetRank(),
		UUID:        cmd.UUID,
	}
	return cmd.makeRequest(ctx, req, pretty.PrintWithVerboseOutput(cmd.Verbose))
}

// usageQueryCmd is the struct representing the scan storage subcommand.
type usageQueryCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
}

// Execute is run when usageQueryCmd activates.
//
// Queries NVMe and SCM usage on hosts.
func (cmd *usageQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.StorageScanReq{Usage: true}
	req.SetHostList(cmd.getHostList())
	resp, err := control.StorageScan(ctx, cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if err := pretty.PrintHostStorageUsageMap(resp.HostStorage, &bld); err != nil {
		return err
	}
	// Infof prints raw string and doesn't try to expand "%"
	// preserving column formatting in txtfmt table
	cmd.Infof("%s", bld.String())

	return resp.Errors()
}

type smdManageCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
}

func (cmd *smdManageCmd) makeRequest(ctx context.Context, req *control.SmdManageReq, opts ...pretty.PrintConfigOption) error {
	req.SetHostList(cmd.getHostList())
	resp, err := control.SmdManage(ctx, cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintResponseErrors(resp, &bld, opts...); err != nil {
		return err
	}
	if err := pretty.PrintSmdInfoMap(false, true, resp.HostStorage, &bld, opts...); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return resp.Errors()
}

type setFaultyCmd struct {
	NVMe nvmeSetFaultyCmd `command:"nvme-faulty" description:"Manually set the device state of an NVMe SSD to FAULTY."`
}

type nvmeSetFaultyCmd struct {
	smdManageCmd
	UUID  string `short:"u" long:"uuid" description:"Device UUID to set" required:"1"`
	Force bool   `short:"f" long:"force" description:"Do not require confirmation"`
}

// Execute is run when nvmeSetFaultyCmd activates
// Set the SMD device state of the given device to "FAULTY"
func (cmd *nvmeSetFaultyCmd) Execute(_ []string) error {
	cmd.Notice("This command will permanently mark the device as unusable!")
	if !cmd.Force && !cmd.JSONOutputEnabled() {
		if !common.GetConsent(cmd.Logger) {
			return errors.New("consent not given")
		}
	}

	req := &control.SmdManageReq{
		Operation: control.SetFaultyOp,
		IDs:       cmd.UUID,
	}
	return cmd.makeRequest(context.Background(), req)
}

// storageReplaceCmd is the struct representing the replace storage subcommand
type storageReplaceCmd struct {
	NVMe nvmeReplaceCmd `command:"nvme" description:"Replace an evicted/FAULTY NVMe SSD with another device."`
}

// nvmeReplaceCmd is the struct representing the replace nvme storage subcommand
type nvmeReplaceCmd struct {
	smdManageCmd
	OldDevUUID string `long:"old-uuid" description:"Device UUID of hot-removed SSD" required:"1"`
	NewDevUUID string `long:"new-uuid" description:"Device UUID of new device" required:"1"`
	NoReint    bool   `long:"no-reint" description:"Bypass reintegration of device and just bring back online."`
}

// Execute is run when storageReplaceCmd activates
// Replace a hot-removed device with a newly plugged device, or reuse a FAULTY device
func (cmd *nvmeReplaceCmd) Execute(_ []string) error {
	if cmd.OldDevUUID == cmd.NewDevUUID {
		cmd.Notice("Attempting to reuse a previously set FAULTY device!")
	}

	// TODO: Implement no-reint flag option
	if cmd.NoReint {
		return errors.New("NoReint is not currently implemented")
	}

	req := &control.SmdManageReq{
		Operation:      control.DevReplaceOp,
		IDs:            cmd.OldDevUUID,
		ReplaceUUID:    cmd.NewDevUUID,
		ReplaceNoReint: cmd.NoReint,
	}
	return cmd.makeRequest(context.Background(), req)
}

type ledCmd struct {
	smdManageCmd

	Args struct {
		IDs string `positional-arg-name:"ids" description:"Comma-separated list of identifiers which could be either VMD backing device (NVMe SSD) PCI addresses or device UUIDs"`
	} `positional-args:"yes"`
}

type ledManageCmd struct {
	Check    ledCheckCmd    `command:"check" description:"Retrieve the current LED state of specified VMD device."`
	Identify ledIdentifyCmd `command:"identify" description:"Blink the status LED on specified VMD device (for the purpose of visual SSD identification). Default duration is 2 minutes."`
}

type ledIdentifyCmd struct {
	ledCmd
	Timeout uint32 `long:"timeout" description:"Number of minutes to blink the status LED for"`
	Reset   bool   `long:"reset" description:"Reset blinking LED on specified VMD device back to previous state"`
}

// Execute is run when ledIdentifyCmd activates.
//
// Runs SPDK VMD API commands to set the LED state on the VMD to "IDENTIFY" (4Hz blink).
func (cmd *ledIdentifyCmd) Execute(_ []string) error {
	if cmd.Args.IDs == "" {
		return errors.New("neither a pci address or a uuid has been supplied")
	}
	req := &control.SmdManageReq{
		Operation:       control.LedBlinkOp,
		IDs:             cmd.Args.IDs,
		IdentifyTimeout: cmd.Timeout,
	}
	if cmd.Reset {
		if cmd.Timeout != 0 {
			return errors.New("timeout option can not be set at the same time as reset")
		}
		req.Operation = control.LedResetOp
	}
	return cmd.makeRequest(context.Background(), req, pretty.PrintOnlyLEDInfo())
}

type ledCheckCmd struct {
	ledCmd
}

// Execute is run when ledCheckCmd activates.
//
// Runs SPDK VMD API commands to query the LED state on VMD devices
func (cmd *ledCheckCmd) Execute(_ []string) error {
	if cmd.Args.IDs == "" {
		return errors.New("neither a pci address or a uuid has been supplied")
	}
	req := &control.SmdManageReq{
		Operation: control.LedCheckOp,
		IDs:       cmd.Args.IDs,
	}
	return cmd.makeRequest(context.Background(), req, pretty.PrintOnlyLEDInfo())
}
