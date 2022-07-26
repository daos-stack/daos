//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/pkg/errors"
)

type legacyStorageCmd struct {
	Prepare lsPrepareCmd `command:"prepare" description:"Prepare SCM and NVMe storage attached to local servers (deprecated, use scm (create|remove)-namespaces or nvme (prepare|release)-drives instead)."`
	Scan    lsScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to local server (deprecated, use scm scan or nvme scan instead)."`
}

type lsPrepareCmd struct {
	cmdutil.LogCmd
	scs                   *server.StorageControlService
	PCIAllowList          string `long:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	PCIBlockList          string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)."`
	NrHugepages           int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	TargetUser            string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO           bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	NrNamespacesPerSocket uint   `short:"S" long:"scm-ns-per-socket" description:"Number of SCM namespaces to create per socket" default:"1"`

	NvmeOnly bool `short:"n" long:"nvme-only" description:"Only prepare NVMe storage."`
	ScmOnly  bool `short:"s" long:"scm-only" description:"Only prepare SCM."`
	Reset    bool `long:"reset" description:"Reset SCM modules to memory mode after removing namespaces. Reset SPDK returning NVMe device bindings back to kernel modules."`
	Force    bool `short:"f" long:"force" description:"Perform format without prompting for confirmation"`
}

// Validate checks both only options are not set and returns flags to direct
// which subsystem types to prepare.
func (cmd *lsPrepareCmd) Validate() (bool, bool, error) {
	prepNvme := cmd.NvmeOnly || !cmd.ScmOnly
	prepScm := cmd.ScmOnly || !cmd.NvmeOnly

	if cmd.NvmeOnly && cmd.ScmOnly {
		return false, false, errors.New(
			"nvme-only and scm-only options should not be set together")
	}

	return prepNvme, prepScm, nil
}

func (cmd *lsPrepareCmd) Execute(args []string) error {
	doNVMe, doSCM, err := cmd.Validate()
	if err != nil {
		return err
	}

	// This is a little ugly, but allows for easier unit testing.
	// FIXME: With the benefit of hindsight, it seems apparent
	// that we should have made these Execute() methods thin
	// wrappers around more easily-testable functions.
	if cmd.scs == nil {
		cmd.scs = server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	}

	var errNVMe, errSCM error

	if cmd.Reset {
		if doNVMe {
			var rdc releaseDrivesCmd
			if err := convert.Types(cmd, &rdc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			errNVMe = rdc.resetNVMe(cmd.scs.NvmePrepare)
		}
		if doSCM {
			var rnc removeNamespacesCmd
			if err := convert.Types(cmd, &rnc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			errSCM = rnc.resetPMem(cmd.scs.ScmPrepare)
		}
	} else {
		if doNVMe {
			var pdc prepareDrivesCmd
			if err := convert.Types(cmd, &pdc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			iommuEnabled, err := hwprov.DefaultIOMMUDetector(cmd).IsIOMMUEnabled()
			if err != nil {
				return errors.Wrap(err, "detecting if iommu is enabled")
			}
			errNVMe = pdc.prepareNVMe(cmd.scs.NvmePrepare, iommuEnabled)
		}
		if doSCM {
			var cnc createNamespacesCmd
			if err := convert.Types(cmd, &cnc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			errSCM = cnc.preparePMem(cmd.scs.ScmPrepare)
		}
	}

	switch {
	case errNVMe != nil && errSCM != nil:
		return common.ConcatErrors([]error{errNVMe, errSCM}, nil)
	case errNVMe != nil:
		return errNVMe
	}

	return errSCM
}

type lsScanCmd struct {
	cmdutil.LogCmd
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
	DisableVMD    bool   `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
}

func (cmd *lsScanCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	svc := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	if !cmd.DisableVMD {
		svc.WithVMDEnabled()
	}

	msg := "Scanning locally-attached storage..."

	cmd.Info(msg)

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

	cmd.Info(bld.String())

	if len(scanErrors) > 0 {
		errStr := "scan error(s):\n"
		for _, err := range scanErrors {
			errStr += fmt.Sprintf("  %s\n", err.Error())
		}
		return errors.New(errStr)
	}

	return nil
}
