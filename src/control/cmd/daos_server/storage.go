//
// (C) Copyright 2019 Intel Corporation.
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
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server"
)

type storageCmd struct {
	Scan     storageScanCmd     `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
	PrepNvme storagePrepNvmeCmd `command:"prep-nvme" description:"Prep NVMe devices for use with SPDK as current user"`
	PrepScm  storagePrepScmCmd  `command:"prep-scm" description:"Prep SCM modules into interleaved AppDirect and create the relevant namespace kernel devices"`
}

type storageScanCmd struct {
	cfgCmd
	logCmd
}

func (cmd *storageScanCmd) Execute(args []string) error {
	srv, err := server.NewStorageControlService(cmd.log, cmd.config)
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	cmd.log.Info("Scanning locally-attached storage...")
	scanErrors := make([]error, 0, 2)
	controllers, err := srv.ScanNVMe()
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		cmd.log.Infof("NVMe SSD controller and constituent namespaces:\n%s", controllers)
	}

	modules, err := srv.ScanSCM()
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		cmd.log.Infof("SCM modules:\n%s", modules)
	}

	if len(scanErrors) > 0 {
		errStr := "scan error(s):\n"
		for _, err := range scanErrors {
			errStr += fmt.Sprintf("  %s\n", err.Error())
		}
		return errors.New(errStr)
	}

	return nil
}

type storagePrepNvmeCmd struct {
	logCmd
	cfgCmd
	PCIWhiteList string `short:"w" long:"pci-whitelist" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate (in MB) for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	Reset        bool   `short:"r" long:"reset" description:"Reset SPDK returning devices to kernel modules"`
}

func (cmd *storagePrepNvmeCmd) Execute(args []string) error {
	ok, usr := common.CheckSudo()
	if !ok {
		return errors.New("subcommand must be run as root or sudo")
	}

	// falls back to sudoer or root if TargetUser is unspecified
	tUsr := usr
	if cmd.TargetUser != "" {
		tUsr = cmd.TargetUser
	}

	srv, err := server.NewStorageControlService(cmd.log, cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	return srv.PrepNvme(server.PrepNvmeRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    tUsr,
		PCIWhitelist:  cmd.PCIWhiteList,
		ResetOnly:     cmd.Reset,
	})
}

type storagePrepScmCmd struct {
	cfgCmd
	logCmd
	Reset bool `short:"r" long:"reset" description:"Reset modules to memory mode after removing namespaces"`
	Force bool `short:"f" long:"force" description:"Perform format without prompting for confirmation"`
}

func (cmd *storagePrepScmCmd) Execute(args []string) (err error) {
	ok, _ := common.CheckSudo()
	if !ok {
		return errors.New("subcommand must be run as root or sudo")
	}

	cmd.log.Info("Memory allocation goals for SCM will be changed and namespaces " +
		"modified, this will be a destructive operation. Please ensure " +
		"namespaces are unmounted and SCM is otherwise unused.\n")

	if !cmd.Force && !common.GetConsent() {
		return errors.New("consent not given")
	}

	srv, err := server.NewStorageControlService(cmd.log, cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	rebootStr, devices, err := srv.PrepScm(server.PrepScmRequest{
		Reset: cmd.Reset,
	})
	if err != nil {
		return err
	}

	if rebootStr != "" {
		cmd.log.Info(rebootStr)
	} else {
		if len(devices) > 0 {
			cmd.log.Infof("persistent memory kernel devices:\n\t%+v\n", devices)
		}
	}

	return nil
}
