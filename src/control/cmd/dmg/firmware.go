//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"io"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// firmwareOption defines a DMG option that enables firmware management in DAOS.
type firmwareOption struct {
	Firmware firmwareCmd `command:"firmware" hidden:"true" description:"Manage the storage device firmware"`
}

// firmwareCmd defines the firmware management subcommands.
type firmwareCmd struct {
	Query  firmwareQueryCmd  `command:"query" description:"Query device firmware versions and status on DAOS storage nodes"`
	Update firmwareUpdateCmd `command:"update" description:"Update the device firmware on DAOS storage nodes"`
}

// firmwareQueryCmd is used to query the storage device firmware on a set of DAOS hosts.
type firmwareQueryCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	DeviceType  string `short:"t" long:"type" choice:"nvme" choice:"scm" choice:"all" default:"all" description:"Type of storage devices to query"`
	Devices     string `short:"d" long:"devices" description:"Comma-separated list of device identifiers to query"`
	ModelID     string `short:"m" long:"model" description:"Model ID to filter results by"`
	FirmwareRev string `short:"f" long:"fwrev" description:"Firmware revision to filter results by"`
	Verbose     bool   `short:"v" long:"verbose" description:"Display verbose output"`
}

// Execute runs the firmware query command.
func (cmd *firmwareQueryCmd) Execute(args []string) error {
	ctx := context.Background()

	req := &control.FirmwareQueryReq{
		SCM:         cmd.isSCMRequested(),
		NVMe:        cmd.isNVMeRequested(),
		ModelID:     cmd.ModelID,
		FirmwareRev: cmd.FirmwareRev,
	}

	if cmd.Devices != "" {
		req.Devices = strings.Split(cmd.Devices, ",")
	}

	req.SetHostList(cmd.getHostList())
	resp, err := control.FirmwareQuery(ctx, cmd.ctlInvoker, req)

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

	printSCMFirmware, printNVMeFirmware := cmd.getDisplayFunctions()
	if cmd.isSCMRequested() {
		if err := printSCMFirmware(resp.HostSCMFirmware, &bld); err != nil {
			return err
		}
	}
	if cmd.isNVMeRequested() {
		if err := printNVMeFirmware(resp.HostNVMeFirmware, &bld); err != nil {
			return err
		}
	}
	cmd.Info(bld.String())

	return resp.Errors()
}

func (cmd *firmwareQueryCmd) isSCMRequested() bool {
	return cmd.DeviceType == "scm" || cmd.DeviceType == "all"
}

func (cmd *firmwareQueryCmd) isNVMeRequested() bool {
	return cmd.DeviceType == "nvme" || cmd.DeviceType == "all"
}

type (
	hostSCMQueryMapPrinter  func(control.HostSCMQueryMap, io.Writer, ...pretty.PrintConfigOption) error
	hostNVMeQueryMapPrinter func(control.HostNVMeQueryMap, io.Writer, ...pretty.PrintConfigOption) error
)

func (cmd *firmwareQueryCmd) getDisplayFunctions() (hostSCMQueryMapPrinter, hostNVMeQueryMapPrinter) {
	printSCM := pretty.PrintSCMFirmwareQueryMap
	printNVMe := pretty.PrintNVMeFirmwareQueryMap
	if cmd.Verbose {
		printSCM = pretty.PrintSCMFirmwareQueryMapVerbose
		printNVMe = pretty.PrintNVMeFirmwareQueryMapVerbose
	}

	return printSCM, printNVMe
}

// firmwareUpdateCmd updates the firmware on storage devices on a set of DAOS hosts.
type firmwareUpdateCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	DeviceType  string `short:"t" long:"type" choice:"nvme" choice:"scm" required:"1" description:"Type of storage devices to update"`
	FilePath    string `short:"p" long:"path" required:"1" description:"Path to the firmware file accessible from all nodes"`
	Devices     string `short:"d" long:"devices" description:"Comma-separated list of device identifiers to update"`
	ModelID     string `short:"m" long:"model" description:"Limit update to a model ID"`
	FirmwareRev string `short:"f" long:"fwrev" description:"Limit update to a current firmware revision"`
	Verbose     bool   `short:"v" long:"verbose" description:"Display verbose output"`
}

// Execute runs the firmware update command.
func (cmd *firmwareUpdateCmd) Execute(args []string) error {
	ctx := context.Background()

	req := &control.FirmwareUpdateReq{
		FirmwarePath: cmd.FilePath,
		ModelID:      cmd.ModelID,
		FirmwareRev:  cmd.FirmwareRev,
	}

	if cmd.isSCMUpdate() {
		req.Type = control.DeviceTypeSCM
	} else {
		req.Type = control.DeviceTypeNVMe
	}

	if cmd.Devices != "" {
		req.Devices = strings.Split(cmd.Devices, ",")
	}

	req.SetHostList(cmd.getHostList())
	resp, err := control.FirmwareUpdate(ctx, cmd.ctlInvoker, req)

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

	if err := cmd.printUpdateResult(resp, &bld); err != nil {
		return err
	}

	cmd.Info(bld.String())

	return resp.Errors()
}

func (cmd *firmwareUpdateCmd) isSCMUpdate() bool {
	return cmd.DeviceType == "scm"
}

func (cmd *firmwareUpdateCmd) printUpdateResult(resp *control.FirmwareUpdateResp, out io.Writer) error {
	if cmd.isSCMUpdate() {
		return cmd.printSCMUpdateResult(resp, out)
	}
	return cmd.printNVMeUpdateResult(resp, out)
}

func (cmd *firmwareUpdateCmd) printSCMUpdateResult(resp *control.FirmwareUpdateResp, out io.Writer) error {
	if cmd.Verbose {
		return pretty.PrintSCMFirmwareUpdateMapVerbose(resp.HostSCMResult, out)
	}
	return pretty.PrintSCMFirmwareUpdateMap(resp.HostSCMResult, out)
}

func (cmd *firmwareUpdateCmd) printNVMeUpdateResult(resp *control.FirmwareUpdateResp, out io.Writer) error {
	if cmd.Verbose {
		return pretty.PrintNVMeFirmwareUpdateMapVerbose(resp.HostNVMeResult, out)
	}
	return pretty.PrintNVMeFirmwareUpdateMap(resp.HostNVMeResult, out)
}
