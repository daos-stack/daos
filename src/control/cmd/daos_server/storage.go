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
	srv, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	outStrs, err := server.StorageScan()

	// print output entries before returning failures
	for _, str := range outStrs {
		cmd.log.Info(str)
	}

	return err
}

type storagePrepNvmeCmd struct {
	cfgCmd
	common.StoragePrepNvmeCmd
}

func (cmd *storagePrepNvmeCmd) Execute(args []string) error {
	srv, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	return srv.PrepNvme(server.PrepareNvmeRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIWhitelist:  cmd.PCIWhiteList,
		ResetOnly:     cmd.Reset,
	})
}

type storagePrepScmCmd struct {
	cfgCmd
	logCmd
	common.StoragePrepScmCmd
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

	srv, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	needsReboot, devices, err := srv.PrepareScm(server.PrepareScmRequest{
		Reset: cmd.Reset,
	})
	if err != nil {
		return err
	}

	if needsReboot {
		cmd.log.Info(msgScmRebootRequired)
		return nil
	}

	if len(devices) > 0 {
		cmd.log.Infof("persistent memory kernel devices:\n\t%+v\n", devices)
		return nil
	}

	return errors.New("unexpected failure")
}
