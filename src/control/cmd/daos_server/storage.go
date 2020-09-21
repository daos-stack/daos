//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	commands "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

type storageCmd struct {
	Prepare storagePrepareCmd `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan    storageScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
}

type storagePrepareCmd struct {
	scs *server.StorageControlService
	logCmd
	commands.StoragePrepareCmd
}

func (cmd *storagePrepareCmd) Execute(args []string) error {
	prepNvme, prepScm, err := cmd.Validate()
	if err != nil {
		return err
	}

	// This is a little ugly, but allows for easier unit testing.
	// FIXME: With the benefit of hindsight, it seems apparent
	// that we should have made these Execute() methods thin
	// wrappers around more easily-testable functions.
	if cmd.scs == nil {
		cmd.scs = server.NewStorageControlService(cmd.log, bdev.DefaultProvider(cmd.log),
			scm.DefaultProvider(cmd.log), server.NewConfiguration().Servers)
	}

	op := "Preparing"
	if cmd.Reset {
		op = "Resetting"
	}

	scanErrors := make([]error, 0, 2)

	if prepNvme {
		cmd.log.Info(op + " locally-attached NVMe storage...")

		// Prepare NVMe access through SPDK
		if _, err := cmd.scs.NvmePrepare(bdev.PrepareRequest{
			HugePageCount: cmd.NrHugepages,
			TargetUser:    cmd.TargetUser,
			PCIWhitelist:  cmd.PCIWhiteList,
			ResetOnly:     cmd.Reset,
		}); err != nil {
			scanErrors = append(scanErrors, err)
		}
	}

	scmScan, err := cmd.scs.ScmScan(scm.ScanRequest{})
	if err != nil {
		return common.ConcatErrors(scanErrors, err)
	}

	if prepScm && len(scmScan.Modules) > 0 {
		cmd.log.Info(op + " locally-attached SCM...")

		if err := cmd.CheckWarn(cmd.log, scmScan.State); err != nil {
			return common.ConcatErrors(scanErrors, err)
		}

		// Prepare SCM modules to be presented as pmem device files.
		// Pass evaluated state to avoid running GetScmState() twice.
		resp, err := cmd.scs.ScmPrepare(scm.PrepareRequest{Reset: cmd.Reset})
		if err != nil {
			return common.ConcatErrors(scanErrors, err)
		}
		if resp.RebootRequired {
			cmd.log.Info(scm.MsgScmRebootRequired)
		} else if len(resp.Namespaces) > 0 {
			cmd.log.Infof("SCM namespaces:\n\t%+v\n", resp.Namespaces)
		} else {
			cmd.log.Info("no SCM namespaces")
		}
	} else if prepScm {
		cmd.log.Info("No SCM modules detected; skipping operation")
	}

	if len(scanErrors) > 0 {
		return common.ConcatErrors(scanErrors, nil)
	}

	return nil
}

type storageScanCmd struct {
	logCmd
}

func (cmd *storageScanCmd) Execute(args []string) error {
	svc := server.NewStorageControlService(cmd.log, bdev.DefaultProvider(cmd.log),
		scm.DefaultProvider(cmd.log), server.NewConfiguration().Servers)

	cmd.log.Info("Scanning locally-attached storage...")

	var bld strings.Builder
	scanErrors := make([]error, 0, 2)

	nvmeResp, err := svc.NvmeScan(bdev.ScanRequest{})
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := control.PrintNvmeControllers(nvmeResp.Controllers, &bld); err != nil {
			return err
		}
	}

	scmResp, err := svc.ScmScan(scm.ScanRequest{})
	switch {
	case err != nil:
		scanErrors = append(scanErrors, err)
	case len(scmResp.Namespaces) > 0:
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := control.PrintScmNamespaces(scmResp.Namespaces, &bld); err != nil {
			return err
		}
	default:
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := control.PrintScmModules(scmResp.Modules, &bld); err != nil {
			return err
		}
	}

	cmd.log.Info(bld.String())

	if len(scanErrors) > 0 {
		errStr := "scan error(s):\n"
		for _, err := range scanErrors {
			errStr += fmt.Sprintf("  %s\n", err.Error())
		}
		return errors.New(errStr)
	}

	return nil
}
