//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"os/user"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	commands "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type storageCmd struct {
	Prepare storagePrepareCmd `command:"prepare" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan    storageScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
}

type storagePrepareCmd struct {
	logCmd
	commands.StoragePrepareCmd
	scs           *server.StorageControlService
	iommuDetector hardware.IOMMUDetector
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
}

func (cmd *storagePrepareCmd) Execute(args []string) error {
	prepNvme, prepScm, err := cmd.Validate()
	if err != nil {
		return err
	}

	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.log.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	// This is a little ugly, but allows for easier unit testing.
	// FIXME: With the benefit of hindsight, it seems apparent
	// that we should have made these Execute() methods thin
	// wrappers around more easily-testable functions.
	if cmd.scs == nil {
		cmd.scs = server.NewStorageControlService(cmd.log, config.DefaultServer().Engines)
	}

	op := "Preparing"
	if cmd.Reset {
		op = "Resetting"
	}

	scanErrors := make([]error, 0, 2)

	if prepNvme {
		cmd.log.Info(op + " locally-attached NVMe storage...")

		if cmd.iommuDetector == nil {
			cmd.iommuDetector = hwprov.DefaultIOMMUDetector(cmd.log)
		}

		iommuEnabled, err := cmd.iommuDetector.IsIOMMUEnabled()
		if err != nil {
			return err
		}

		if cmd.TargetUser == "" {
			runningUser, err := user.Current()
			if err != nil {
				return errors.Wrap(err, "couldn't lookup running user")
			}
			cmd.TargetUser = runningUser.Username
		}

		if cmd.TargetUser != "root" {
			if cmd.DisableVFIO {
				return errors.New("VFIO can not be disabled if running as non-root user")
			}
			if !iommuEnabled {
				return errors.New("no IOMMU detected, to discover NVMe devices enable " +
					"IOMMU per the DAOS Admin Guide")
			}
		}

		req := storage.BdevPrepareRequest{
			HugePageCount: cmd.NrHugepages,
			TargetUser:    cmd.TargetUser,
			PCIAllowList:  cmd.PCIAllowList,
			PCIBlockList:  cmd.PCIBlockList,
			Reset_:        cmd.Reset,
			DisableVFIO:   cmd.DisableVFIO,
		}

		switch {
		case cmd.DisableVFIO:
			cmd.log.Info("VMD not enabled because VFIO disabled in command options")
		case !iommuEnabled:
			cmd.log.Info("VMD not enabled because IOMMU disabled on platform")
		default:
			// If none of the cases above match, set enable VMD flag in request.
			req.EnableVMD = true
		}

		cmd.log.Debugf("nvme prep request parameters: %+v", req)

		// Prepare NVMe access through SPDK
		if _, err := cmd.scs.NvmePrepare(req); err != nil {
			scanErrors = append(scanErrors, err)
		}
	}

	scmScan, err := cmd.scs.ScmScan(storage.ScmScanRequest{})
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
		resp, err := cmd.scs.ScmPrepare(storage.ScmPrepareRequest{Reset: cmd.Reset})
		if err != nil {
			return common.ConcatErrors(scanErrors, err)
		}
		if resp.RebootRequired {
			cmd.log.Info(storage.ScmMsgRebootRequired)
		} else if len(resp.Namespaces) > 0 {
			var bld strings.Builder
			if err := pretty.PrintScmNamespaces(resp.Namespaces, &bld); err != nil {
				return err
			}
			cmd.log.Infof("SCM namespaces:\n%s\n", bld.String())
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
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
	DisableVMD    bool   `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
}

func (cmd *storageScanCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.log.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	svc := server.NewStorageControlService(cmd.log, config.DefaultServer().Engines)
	if !cmd.DisableVMD {
		svc.WithVMDEnabled()
	}

	msg := "Scanning locally-attached storage..."

	cmd.log.Info(msg)

	var bld strings.Builder
	scanErrors := make([]error, 0, 2)

	nvmeResp, err := svc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := pretty.PrintNvmeControllers(nvmeResp.Controllers, &bld); err != nil {
			return err
		}
	}

	scmResp, err := svc.ScmScan(storage.ScmScanRequest{})
	switch {
	case err != nil:
		scanErrors = append(scanErrors, err)
	case len(scmResp.Namespaces) > 0:
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := pretty.PrintScmNamespaces(scmResp.Namespaces, &bld); err != nil {
			return err
		}
	default:
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := pretty.PrintScmModules(scmResp.Modules, &bld); err != nil {
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
