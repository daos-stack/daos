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

	commands "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

type storageCmd struct {
	Prepare storagePrepareCmd `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan    storageScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
}

type storagePrepareCmd struct {
	logCmd
	commands.StoragePrepareCmd
}

func concatErrors(scanErrors []error, err error) error {
	if err != nil {
		scanErrors = append(scanErrors, err)
	}

	errStr := "scan error(s):\n"
	for _, err := range scanErrors {
		errStr += fmt.Sprintf("  %s\n", err.Error())
	}

	return errors.New(errStr)
}

func (cmd *storagePrepareCmd) Execute(args []string) error {
	prepNvme, prepScm, err := cmd.Validate()
	if err != nil {
		return err
	}

	cfg := server.NewConfiguration()
	svc, err := server.DefaultStorageControlService(cmd.log, cfg)
	if err != nil {
		return errors.WithMessage(err, "init control service")
	}

	op := "Preparing"
	if cmd.Reset {
		op = "Resetting"
	}

	scanErrors := make([]error, 0, 2)

	if prepNvme {
		cmd.log.Info(op + " locally-attached NVMe storage...")

		// Prepare NVMe access through SPDK
		if _, err := svc.NvmePrepare(bdev.PrepareRequest{
			HugePageCount: cmd.NrHugepages,
			TargetUser:    cmd.TargetUser,
			PCIWhitelist:  cmd.PCIWhiteList,
			ResetOnly:     cmd.Reset,
		}); err != nil {
			scanErrors = append(scanErrors, err)
		}
	}

	if prepScm {
		cmd.log.Info(op + " locally-attached SCM...")

		state, err := svc.GetScmState()
		if err != nil {
			return concatErrors(scanErrors, err)
		}

		if err := cmd.CheckWarn(cmd.log, state); err != nil {
			return concatErrors(scanErrors, err)
		}

		// Prepare SCM modules to be presented as pmem device files.
		// Pass evaluated state to avoid running GetScmState() twice.
		resp, err := svc.ScmPrepare(scm.PrepareRequest{Reset: cmd.Reset})
		if err != nil {
			return concatErrors(scanErrors, err)
		}
		if resp.RebootRequired {
			cmd.log.Info(scm.MsgScmRebootRequired)
		} else if len(resp.Namespaces) > 0 {
			cmd.log.Infof("SCM namespaces:\n\t%+v\n", resp.Namespaces)
		} else {
			cmd.log.Info("no SCM namespaces")
		}
	}

	if len(scanErrors) > 0 {
		return concatErrors(scanErrors, nil)
	}

	return nil
}

type storageScanCmd struct {
	logCmd
}

func (cmd *storageScanCmd) Execute(args []string) error {
	svc, err := server.DefaultStorageControlService(cmd.log, server.NewConfiguration())
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	cmd.log.Info("Scanning locally-attached storage...")

	scanErrors := make([]error, 0, 2)

	res, err := svc.NvmeScan()
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		cmd.log.Info(res.Controllers.String())
	}

	scmResp, err := svc.ScmScan()
	switch {
	case err != nil:
		scanErrors = append(scanErrors, err)
	case len(scmResp.Namespaces) > 0:
		cmd.log.Infof("SCM Namespaces:\n%s\n", scmResp.Namespaces)
	default:
		cmd.log.Infof("SCM Modules:\n%s\n", scmResp.Modules)
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
