//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/system"
)

type rankCmd struct {
	Rank *uint32 `short:"r" long:"rank" description:"Constrain operation to the specified server rank"`
}

func (r *rankCmd) GetRank() system.Rank {
	if r.Rank == nil {
		return system.NilRank
	}
	return system.Rank(*r.Rank)
}

type smdQueryCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
}

func (cmd *smdQueryCmd) makeRequest(ctx context.Context, req *control.SmdQueryReq, opts ...pretty.PrintConfigOption) error {
	req.SetHostList(cmd.hostlist)
	resp, err := control.SmdQuery(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintResponseErrors(resp, &bld, opts...); err != nil {
		return err
	}
	if err := pretty.PrintSmdInfoMap(req, resp.HostStorage, &bld, opts...); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return resp.Errors()
}

// storageQueryCmd is the struct representing the storage query subcommand
type storageQueryCmd struct {
	TargetHealth tgtHealthQueryCmd   `command:"target-health" alias:"t" description:"Query the target health"`
	DeviceHealth devHealthQueryCmd   `command:"device-health" alias:"d" description:"Query the device health"`
	ListPools    listPoolsQueryCmd   `command:"list-pools" alias:"p" description:"List pools on the server"`
	ListDevices  listDevicesQueryCmd `command:"list-devices" alias:"d" description:"List storage devices on the server"`
	Usage        usageQueryCmd       `command:"usage" alias:"u" description:"Show SCM & NVMe storage space utilization per storage server"`
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
		Rank:             system.NilRank,
		UUID:             cmd.UUID,
	}
	return cmd.makeRequest(ctx, req)
}

type tgtHealthQueryCmd struct {
	smdQueryCmd
	Rank  uint32 `short:"r" long:"rank" required:"1" description:"Server rank hosting target"`
	TgtId uint32 `short:"t" long:"tgtid" required:"1" description:"VOS target ID to query"`
}

func (cmd *tgtHealthQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.SmdQueryReq{
		OmitPools:        true,
		IncludeBioHealth: true,
		Rank:             system.Rank(cmd.Rank),
		Target:           strconv.Itoa(int(cmd.TgtId)),
	}
	return cmd.makeRequest(ctx, req)
}

type listDevicesQueryCmd struct {
	smdQueryCmd
	rankCmd
	Health bool   `short:"b" long:"health" description:"Include device health in results"`
	UUID   string `short:"u" long:"uuid" description:"Device UUID (all devices if blank)"`
}

func (cmd *listDevicesQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.SmdQueryReq{
		OmitPools:        true,
		IncludeBioHealth: cmd.Health,
		Rank:             cmd.GetRank(),
		UUID:             cmd.UUID,
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
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
}

// Execute is run when usageQueryCmd activates.
//
// Queries NVMe and SCM usage on hosts.
func (cmd *usageQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.StorageScanReq{Usage: true}
	req.SetHostList(cmd.hostlist)
	resp, err := control.StorageScan(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
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
	cmd.log.Infof("%s", bld.String())

	return resp.Errors()
}
