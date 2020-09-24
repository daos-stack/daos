//
// (C) Copyright 2020 Intel Corporation.
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
// +build firmware

package main

import (
	"context"
	"io"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// firmwareOption defines a DMG option that enables firmware management in DAOS.
type firmwareOption struct {
	Firmware firmwareCmd `command:"firmware" alias:"fw" hidden:"true" description:"Manage the storage device firmware"`
}

// firmwareCmd defines the firmware management subcommands.
type firmwareCmd struct {
	Query  firmwareQueryCmd  `command:"query" alias:"q" description:"Query device firmware versions and status on DAOS storage nodes"`
	Update firmwareUpdateCmd `command:"update" alias:"u" description:"Update the device firmware on DAOS storage nodes"`
}

// firmwareQueryCmd is used to query the storage device firmware on a set of DAOS hosts.
type firmwareQueryCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
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

	req.SetHostList(cmd.hostlist)
	resp, err := control.FirmwareQuery(ctx, cmd.ctlInvoker, req)

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
	cmd.log.Info(bld.String())

	return resp.Errors()
}

func (cmd *firmwareQueryCmd) isSCMRequested() bool {
	return cmd.DeviceType == "scm" || cmd.DeviceType == "all"
}

func (cmd *firmwareQueryCmd) isNVMeRequested() bool {
	return cmd.DeviceType == "nvme" || cmd.DeviceType == "all"
}

type (
	hostSCMQueryMapPrinter  func(control.HostSCMQueryMap, io.Writer, ...control.PrintConfigOption) error
	hostNVMeQueryMapPrinter func(control.HostNVMeQueryMap, io.Writer, ...control.PrintConfigOption) error
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
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
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

	req.SetHostList(cmd.hostlist)
	resp, err := control.FirmwareUpdate(ctx, cmd.ctlInvoker, req)

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

	if err := cmd.printUpdateResult(resp, &bld); err != nil {
		return err
	}

	cmd.log.Info(bld.String())

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
