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
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const pciAddrSep = ","

type nvmePrepareResetFn func(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)

type nvmeCmd struct {
	Prepare prepareNVMeCmd `command:"prepare" description:"Prepare NVMe SSDs for use by DAOS"`
	Release releaseNVMeCmd `command:"release" description:"Release NVMe SSDs for use by OS"`
	Scan    scanNVMeCmd    `command:"scan" description:"Scan NVMe SSDs"`
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

	// Commandline PCI address lists will be comma-separated, sanitize into expected format.

	if strings.Contains(req.PCIAllowList, " ") {
		return errors.New("expecting comma-separated list of allowed pci addresses but found space separator")
	}
	allowed, err := hardware.NewPCIAddressSet(strings.Split(req.PCIAllowList, pciAddrSep)...)
	if err != nil {
		return errors.Wrap(err, "invalid addresses in pci allow list")
	}
	req.PCIAllowList = allowed.String()

	if strings.Contains(req.PCIBlockList, " ") {
		return errors.New("expecting comma-separated list of blocked pci addresses but found space separator")
	}
	blocked, err := hardware.NewPCIAddressSet(strings.Split(req.PCIBlockList, pciAddrSep)...)
	if err != nil {
		return errors.Wrap(err, "invalid addresses in pci block list")
	}
	req.PCIBlockList = blocked.String()

	return nil
}

type prepareNVMeCmd struct {
	cmdutil.LogCmd  `json:"-"`
	helperLogCmd    `json:"-"`
	iommuCheckerCmd `json:"-"`

	PCIBlockList string `long:"pci-block-list" description:"Comma-separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO  bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	Args         struct {
		PCIAllowList string `positional-arg-name:"pci-allow-list" description:"Comma-separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	} `positional-args:"yes"`
}

func (cmd *prepareNVMeCmd) WithDisableVFIO(b bool) *prepareNVMeCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *prepareNVMeCmd) WithTargetUser(u string) *prepareNVMeCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *prepareNVMeCmd) WithPCIAllowList(al string) *prepareNVMeCmd {
	cmd.Args.PCIAllowList = al
	return cmd
}

func (cmd *prepareNVMeCmd) prepareNVMe(prepareBackend nvmePrepareResetFn) error {
	cmd.Info("Prepare locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.Args.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		DisableVFIO:   cmd.DisableVFIO,
	}

	iommuEnabled, err := cmd.isIOMMUEnabled()
	if err != nil {
		return err
	}

	if err := updatePrepReqParams(cmd.Logger, iommuEnabled, &req); err != nil {
		return errors.Wrap(err, "updating prepare request params")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Prepare NVMe device access.
	_, err = prepareBackend(req)

	return err
}

func (cmd *prepareNVMeCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing prepare drives command: %+v", cmd)
	return cmd.prepareNVMe(scs.NvmePrepare)
}

type releaseNVMeCmd struct {
	cmdutil.LogCmd  `json:"-"`
	helperLogCmd    `json:"-"`
	iommuCheckerCmd `json:"-"`

	PCIBlockList string `long:"pci-block-list" description:"Comma-separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO  bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	Args         struct {
		PCIAllowList string `positional-arg-name:"pci-allow-list" description:"Comma-separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	} `positional-args:"yes"`
}

func (cmd *releaseNVMeCmd) WithDisableVFIO(b bool) *releaseNVMeCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *releaseNVMeCmd) WithTargetUser(u string) *releaseNVMeCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *releaseNVMeCmd) WithPCIAllowList(al string) *releaseNVMeCmd {
	cmd.Args.PCIAllowList = al
	return cmd
}

func (cmd *releaseNVMeCmd) resetNVMe(resetBackend nvmePrepareResetFn) error {
	cmd.Info("Release locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		TargetUser:   cmd.TargetUser,
		PCIAllowList: cmd.Args.PCIAllowList,
		PCIBlockList: cmd.PCIBlockList,
		DisableVFIO:  cmd.DisableVFIO,
		Reset_:       true,
	}

	iommuEnabled, err := cmd.isIOMMUEnabled()
	if err != nil {
		return err
	}

	if err := updatePrepReqParams(cmd.Logger, iommuEnabled, &req); err != nil {
		return errors.Wrap(err, "updating prepare request params")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Reset NVMe device access.
	_, err = resetBackend(req)

	return err
}

func (cmd *releaseNVMeCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing release drives command: %+v", cmd)
	return cmd.resetNVMe(scs.NvmePrepare)
}

type scanNVMeCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`

	DisableVMD bool `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
}

func (cmd *scanNVMeCmd) Execute(args []string) error {
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
