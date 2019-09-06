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
	Scan    storageScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
	Prepare storagePrepareCmd `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
}

type storageScanCmd struct {
	logCmd
}

func (cmd *storageScanCmd) Execute(args []string) error {
	svc, err := server.NewStorageControlService(cmd.log, server.NewConfiguration())
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	cmd.log.Info("Scanning locally-attached storage...")

	scanErrors := make([]error, 0, 2)
	e := make(chan error)
	c := make(chan types.NvmeControllers)
	m := make(chan types.ScmModules)

	go func() {
		controllers, err := svc.ScanNvme()
		if err == nil {
			c <- controllers
		}
		e <- err // last err value always processed after entries returned
	}()

	go func() {
		modules, err := svc.ScanScm()
		if err == nil {
			m <- modules
		}
		e <- err
	}()

	for {
		select {
		case err := <-e:
			scanErrors = append(scanErrors, err)
			if len(scanErrors) == 2 {
				break
			}
		case modules := <-m:
			cmd.log.Infof("SCM modules:\n%s", modules)
		case controllers := <-c:
			cmd.log.Infof("NVMe SSD controller and constituent namespaces:\n%s", controllers)
		}
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

type storagePrepareCmd struct {
	logCmd
	types.StoragePrepareCmd
}

func (cmd *storagePrepareCmd) Execute(args []string) error {
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

	svc, err := server.NewStorageControlService(cmd.log, server.NewConfiguration())
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	cmd.log.Info("Preparing locally-attached storage...")

	scanErrors := make([]error, 0, 2)
	e := make(chan error)

	go func() {
		e <- svc.PrepareNvme(server.PrepareNvmeRequest{
			HugePageCount: cmd.NrHugepages,
			TargetUser:    cmd.TargetUser,
			PCIWhitelist:  cmd.PCIWhiteList,
			ResetOnly:     cmd.Reset,
		})
	}()

	go func() {
		needsReboot, devices, err := svc.PrepareScm(server.PrepareScmRequest{
			Reset: cmd.Reset,
		})
		if err != nil {
			e <- err
		}

		if needsReboot {
			// TODO: return message on channel for deterministic logging
			cmd.log.Info(server.MsgScmRebootRequired)
			e <- nil
		}

		if len(devices) > 0 {
			cmd.log.Infof("persistent memory kernel devices:\n\t%+v\n", devices)
		}

		e <- nil
	}()

	for {
		select {
		case err := <-e:
			scanErrors = append(scanErrors, err)
			if len(scanErrors) == 2 {
				break
			}
		}
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
