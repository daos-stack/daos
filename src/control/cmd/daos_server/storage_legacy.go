//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type legacyStorageCmd struct {
	Prepare legacyPrepCmd `command:"prepare" description:"Prepare SCM and NVMe storage attached to local servers (deprecated, use scm (prepare|reset) or nvme (prepare|reset) instead)."`
	Scan    legacyScanCmd `command:"scan" description:"Scan SCM and NVMe storage attached to local server (deprecated, use scm scan or nvme scan instead)."`
}

type legacyPrepCmd struct {
	cmdutil.LogCmd
	helperLogCmd
	iommuCheckerCmd
	scs *server.StorageControlService

	Reset bool `long:"reset" description:"Reset SCM modules to memory mode after removing namespaces. Reset SPDK returning NVMe device bindings back to kernel modules."`

	ScmOnly               bool `short:"s" long:"scm-only" description:"Only prepare SCM."`
	NrNamespacesPerSocket uint `short:"S" long:"scm-ns-per-socket" description:"Number of SCM namespaces to create per socket" default:"1"`
	Force                 bool `short:"f" long:"force" description:"Perform format without prompting for confirmation"`

	NvmeOnly     bool   `short:"n" long:"nvme-only" description:"Only prepare NVMe storage."`
	PCIAllowList string `long:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	PCIBlockList string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO  bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
}

// Validate checks both only options are not set and returns flags to direct
// which subsystem types to prepare.
func (cmd *legacyPrepCmd) Validate() (bool, bool, error) {
	prepScm := cmd.ScmOnly || !cmd.NvmeOnly
	if prepScm {
		cmd.Debug("legacy prepare of scm selected")
	}
	prepNvme := cmd.NvmeOnly || !cmd.ScmOnly
	if prepNvme {
		cmd.Debug("legacy prepare of nvme selected")
	}

	if cmd.ScmOnly && cmd.NvmeOnly {
		return false, false, errors.New(
			"scm-only and nvme-only options should not be set together")
	}

	return prepScm, prepNvme, nil
}

func (cmd *legacyPrepCmd) prep(scs *server.StorageControlService) error {
	var errSCM, errNVMe error

	doSCM, doNVMe, err := cmd.Validate()
	if err != nil {
		return err
	}

	if cmd.Reset {
		if doSCM {
			var rnc resetSCMCmd
			if err := convert.Types(cmd, &rnc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			rnc.LogCmd = cmdutil.LogCmd{Logger: cmd.Logger}
			errSCM = rnc.resetPMem(scs.ScmPrepare)
		}
		if doNVMe {
			var rdc resetNVMeCmd
			if err := convert.Types(cmd, &rdc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			rdc.LogCmd = cmdutil.LogCmd{Logger: cmd.Logger}
			// resetNVMeCmd expects positional argument, so set it
			rdc.Args.PCIAllowList = cmd.PCIAllowList
			rdc.setIOMMUChecker(cmd.isIOMMUEnabled)
			req := storage.BdevPrepareRequest{
				TargetUser:   rdc.TargetUser,
				PCIAllowList: rdc.Args.PCIAllowList,
				PCIBlockList: rdc.PCIBlockList,
				DisableVFIO:  rdc.DisableVFIO,
				Reset_:       true,
			}
			errNVMe = resetNVMe(req, &rdc.nvmeCmd, scs.NvmePrepare)
		}
	} else {
		if doSCM {
			var cnc prepareSCMCmd
			if err := convert.Types(cmd, &cnc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			cnc.LogCmd = cmdutil.LogCmd{Logger: cmd.Logger}
			errSCM = cnc.preparePMem(scs.ScmPrepare)
		}
		if doNVMe {
			var pdc prepareNVMeCmd
			if err := convert.Types(cmd, &pdc); err != nil {
				return errors.Wrap(err, "converting legacy prepare command")
			}
			pdc.LogCmd = cmdutil.LogCmd{Logger: cmd.Logger}
			// prepareNVMeCmd expects positional argument, so set it
			pdc.Args.PCIAllowList = cmd.PCIAllowList
			pdc.setIOMMUChecker(cmd.isIOMMUEnabled)
			req := storage.BdevPrepareRequest{
				HugepageCount: pdc.NrHugepages,
				TargetUser:    pdc.TargetUser,
				PCIAllowList:  pdc.Args.PCIAllowList,
				PCIBlockList:  pdc.PCIBlockList,
				DisableVFIO:   pdc.DisableVFIO,
			}
			errNVMe = prepareNVMe(req, &pdc.nvmeCmd, scs.NvmePrepare)
		}
	}

	switch {
	case errSCM != nil && errNVMe != nil:
		return errors.Wrap(common.ConcatErrors([]error{errSCM, errNVMe}, nil),
			"storage prepare command returned multiple errors")
	case errSCM != nil:
		return errSCM
	}

	return errNVMe
}

func (cmd *legacyPrepCmd) Execute(args []string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}
	cmd.setIOMMUChecker(hwprov.DefaultIOMMUDetector(cmd.Logger).IsIOMMUEnabled)

	cmd.Info("storage prepare subcommand is deprecated, use nvme or scm subcommands instead")

	if cmd.scs == nil {
		cmd.scs = server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	}

	cmd.Debugf("executing legacy storage prepare command: %+v", cmd)
	return cmd.prep(cmd.scs)
}

type legacyScanCmd struct {
	cmdutil.LogCmd
	helperLogCmd
	DisableVMD bool `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
}

func (cmd *legacyScanCmd) Execute(args []string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}
	cmd.Notice("storage scan subcommand is deprecated, use nvme or scm subcommands instead")
	if err := cmd.setHelperLogFile(); err != nil {
		return err
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
