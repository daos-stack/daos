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
	types "github.com/daos-stack/daos/src/control/common/storage"
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
	svc, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	cmd.log.Info("Scanning locally-attached storage...")
	scanErrors := make([]error, 0, 2)
	controllers, err := svc.ScanNvme()
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		cmd.log.Infof("NVMe SSD controller and constituent namespaces:\n%s", controllers)
	}

	modules, err := svc.ScanScm()
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
	cfgCmd
	types.StoragePrepareNvmeCmd
}

func (cmd *storagePrepNvmeCmd) Execute(args []string) error {
	svc, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	return svc.PrepareNvme(server.PrepareNvmeRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIWhitelist:  cmd.PCIWhiteList,
		ResetOnly:     cmd.Reset,
	})
}

type storagePrepScmCmd struct {
	cfgCmd
	logCmd
	types.StoragePrepareScmCmd
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

	svc, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	needsReboot, devices, err := svc.PrepareScm(server.PrepareScmRequest{
		Reset: cmd.Reset,
	})
	if err != nil {
		return err
	}

	if needsReboot {
		cmd.log.Info(server.MsgScmRebootRequired)
		return nil
	}

	if len(devices) > 0 {
		cmd.log.Infof("persistent memory kernel devices:\n\t%+v\n", devices)
		return nil
	}

	return errors.New("unexpected failure")
}
