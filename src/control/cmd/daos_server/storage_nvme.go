//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os/user"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type nvmePrepareResetFn func(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)

type nvmeCmd struct {
	Prepare prepareDrivesCmd `command:"prepare-drives" description:"Prepare NVMe SSDs for use by DAOS"`
	Release releaseDrivesCmd `command:"release-drives" description:"Release NVMe SSDs for use by OS"`
	Scan    scanDrivesCmd    `command:"scan" description:"Scan NVMe SSDs"`
}

func getTargetUser(reqUser string) (string, error) {
	if reqUser == "" {
		runningUser, err := user.Current()
		if err != nil {
			return "", errors.Wrap(err, "couldn't lookup running user")
		}
		if runningUser.Username == "" {
			return "", errors.New("empty username returned from current user lookup")
		}
		return runningUser.Username, nil
	}
	return reqUser, nil
}

func validateVFIOSetting(targetUser string, reqDisableVFIO bool, iommuEnabled bool) error {
	if targetUser != "root" {
		if reqDisableVFIO {
			return storage.FaultBdevNonRootVFIODisable
		}
		if !iommuEnabled {
			return storage.FaultBdevNoIOMMU
		}
	}
	return nil
}

func updatePrepReqParams(log logging.Logger, iommuEnabled bool, req *storage.BdevPrepareRequest) error {
	targetUser, err := getTargetUser(req.TargetUser)
	if err != nil {
		return err
	}
	// Update target user parameter in request.
	req.TargetUser = targetUser

	if err := validateVFIOSetting(targetUser, req.DisableVFIO, iommuEnabled); err != nil {
		return err
	}

	switch {
	case req.DisableVFIO:
		log.Info("VMD not enabled because VFIO disabled in command options")
	case !iommuEnabled:
		log.Info("VMD not enabled because IOMMU disabled on platform")
	default:
		// If none of the cases above match, set enable VMD flag in request.
		req.EnableVMD = true
	}

	return nil
}

type prepareDrivesCmd struct {
	cmdutil.LogCmd  `json:"-"`
	helperLogCmd    `json:"-"`
	iommuCheckerCmd `json:"-"`

	PCIBlockList string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO  bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	Args         struct {
		PCIAllowList string `positional-arg-name:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	} `positional-args:"yes"`
}

func (cmd *prepareDrivesCmd) WithDisableVFIO(b bool) *prepareDrivesCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *prepareDrivesCmd) WithTargetUser(u string) *prepareDrivesCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *prepareDrivesCmd) WithPCIAllowList(al string) *prepareDrivesCmd {
	cmd.Args.PCIAllowList = al
	return cmd
}

func (cmd *prepareDrivesCmd) prepareNVMe(backendCall nvmePrepareResetFn, iommuEnabled bool) error {
	cmd.Info("Prepare locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.Args.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		DisableVFIO:   cmd.DisableVFIO,
	}

	if err := updatePrepReqParams(cmd.Logger, iommuEnabled, &req); err != nil {
		return errors.Wrap(err, "evaluating vmd capability on platform")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Configure NVMe device access.
	_, err := backendCall(req)

	return err
}

func (cmd *prepareDrivesCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	iommuEnabled, err := cmd.isIOMMUEnabled(cmd.Logger)
	if err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing prepare drives command: %+v", cmd)
	return cmd.prepareNVMe(scs.NvmePrepare, iommuEnabled)
}

type releaseDrivesCmd struct {
	cmdutil.LogCmd  `json:"-"`
	helperLogCmd    `json:"-"`
	iommuCheckerCmd `json:"-"`

	PCIBlockList string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO  bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	Args         struct {
		PCIAllowList string `positional-arg-name:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	} `positional-args:"yes"`
}

func (cmd *releaseDrivesCmd) WithDisableVFIO(b bool) *releaseDrivesCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *releaseDrivesCmd) WithTargetUser(u string) *releaseDrivesCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *releaseDrivesCmd) WithPCIAllowList(al string) *releaseDrivesCmd {
	cmd.Args.PCIAllowList = al
	return cmd
}

func (cmd *releaseDrivesCmd) resetNVMe(backendCall nvmePrepareResetFn, iommuEnabled bool) error {
	cmd.Info("Release locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		TargetUser:   cmd.TargetUser,
		PCIAllowList: cmd.Args.PCIAllowList,
		PCIBlockList: cmd.PCIBlockList,
		DisableVFIO:  cmd.DisableVFIO,
		Reset_:       true,
	}

	if err := updatePrepReqParams(cmd.Logger, iommuEnabled, &req); err != nil {
		return errors.Wrap(err, "evaluating vmd capability on platform")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Configure NVMe device access.
	_, err := backendCall(req)

	return err
}

func (cmd *releaseDrivesCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	iommuEnabled, err := cmd.isIOMMUEnabled(cmd.Logger)
	if err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing release drives command: %+v", cmd)
	return cmd.resetNVMe(scs.NvmePrepare, iommuEnabled)
}

type scanDrivesCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `jcon:"-"`

	DisableVMD bool `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
}

func (cmd *scanDrivesCmd) Execute(args []string) error {
	var bld strings.Builder

	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	svc := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	if !cmd.DisableVMD {
		svc.WithVMDEnabled()
	}

	cmd.Info("Scanning locally-attached NVMe storage...\n")

	nvmeResp, err := svc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		return err
	}

	if err := pretty.PrintNvmeControllers(nvmeResp.Controllers, &bld); err != nil {
		return err
	}

	cmd.Info(bld.String())

	return nil
}
