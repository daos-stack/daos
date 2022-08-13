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
	Reset   resetNVMeCmd   `command:"reset" description:"Reset NVMe SSDs for use by OS"`
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

func updateNVMePrepReqFromConfig(log logging.Logger, cfg *config.Server, req *storage.BdevPrepareRequest) error {
	if cfg == nil {
		log.Debugf("nil input server config so skip updating request from config")
		return nil
	}

	if cfg.DisableHugepages {
		return errors.New("hugepage usage has been disabled in the config file")
	}

	req.DisableVFIO = req.DisableVFIO || cfg.DisableVFIO

	if req.HugePageCount == 0 && cfg.NrHugepages > 0 {
		req.HugePageCount = cfg.NrHugepages
	}

	if req.PCIAllowList == "" {
		// Combine engine bdev_lists to use as allow list.
		var bdevCfgs storage.TierConfigs
		for _, ec := range cfg.Engines {
			bdevCfgs = append(bdevCfgs, ec.Storage.Tiers.BdevConfigs()...)
		}

		nvmeBdevs := bdevCfgs.NVMeBdevs()
		log.Debugf("no allow list set in req so reading bdev_lists (%q) from cfg", nvmeBdevs)
		if nvmeBdevs.Len() != 0 {
			req.PCIAllowList = nvmeBdevs.String()
		}
	}

	if req.PCIBlockList == "" && len(cfg.BdevExclude) > 0 {
		log.Debugf("no block list set in req so reading bdev_exclude (%q) from cfg", cfg.BdevExclude)
		blocked, err := hardware.NewPCIAddressSet(cfg.BdevExclude...)
		if err != nil {
			return errors.Wrap(err, "invalid addresses in pci block list")
		}
		if blocked.Len() != 0 {
			req.PCIBlockList = blocked.String()
		}
	}

	return nil
}

// Commandline PCI address lists will be comma-separated, sanitize into expected format.
func sanitizePCIAddrLists(req *storage.BdevPrepareRequest) error {
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
	cfgCmd          `json:"-"`

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

func (cmd *prepareNVMeCmd) WithIgnoreConfig(b bool) *prepareNVMeCmd {
	cmd.IgnoreConfig = b
	return cmd
}

func processNVMePrepReq(log logging.Logger, cfg *config.Server, iommuChecker hardware.IOMMUDetector, req *storage.BdevPrepareRequest) error {
	if err := sanitizePCIAddrLists(req); err != nil {
		return errors.Wrap(err, "sanitizing cli input pci address lists")
	}

	if err := updateNVMePrepReqFromConfig(log, cfg, req); err != nil {
		return errors.Wrap(err, "updating request parameters with config file settings")
	}

	iommuEnabled, err := iommuChecker.IsIOMMUEnabled()
	if err != nil {
		return errors.Wrap(err, "verifying iommu capability on host")
	}

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
	case cfg != nil && cfg.DisableVMD != nil && *(cfg.DisableVMD):
		log.Info("VMD not enabled because VMD disabled in config file")
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

func (cmd *prepareNVMeCmd) prepareNVMe(prepareBackend nvmePrepareResetFn) error {
	cmd.Info("Prepare locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.Args.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		DisableVFIO:   cmd.DisableVFIO,
	}

	cfgParam := cmd.config
	if cmd.IgnoreConfig {
		cfgParam = nil
	}

	if err := processNVMePrepReq(cmd.Logger, cfgParam, cmd, &req); err != nil {
		return errors.Wrap(err, "processing request parameters")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Prepare NVMe device access.
	_, err := prepareBackend(req)

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

type resetNVMeCmd struct {
	cmdutil.LogCmd  `json:"-"`
	helperLogCmd    `json:"-"`
	iommuCheckerCmd `json:"-"`
	cfgCmd          `json:"-"`

	PCIBlockList string `long:"pci-block-list" description:"Comma-separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO  bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	Args         struct {
		PCIAllowList string `positional-arg-name:"pci-allow-list" description:"Comma-separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	} `positional-args:"yes"`
}

func (cmd *resetNVMeCmd) WithDisableVFIO(b bool) *resetNVMeCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *resetNVMeCmd) WithTargetUser(u string) *resetNVMeCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *resetNVMeCmd) WithPCIAllowList(al string) *resetNVMeCmd {
	cmd.Args.PCIAllowList = al
	return cmd
}

func (cmd *resetNVMeCmd) WithIgnoreConfig(b bool) *resetNVMeCmd {
	cmd.IgnoreConfig = b
	return cmd
}

func (cmd *resetNVMeCmd) resetNVMe(resetBackend nvmePrepareResetFn) error {
	cmd.Info("Reset locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		TargetUser:   cmd.TargetUser,
		PCIAllowList: cmd.Args.PCIAllowList,
		PCIBlockList: cmd.PCIBlockList,
		DisableVFIO:  cmd.DisableVFIO,
		Reset_:       true,
	}

	cfgParam := cmd.config
	if cmd.IgnoreConfig {
		cfgParam = nil
	}

	if err := processNVMePrepReq(cmd.Logger, cfgParam, cmd, &req); err != nil {
		return errors.Wrap(err, "processing request parameters")
	}
	// As reset nvme backend doesn't use NrHugepages, set to zero value.
	req.HugePageCount = 0

	cmd.Debugf("nvme reset request parameters: %+v", req)

	// Reset NVMe device access.
	_, err := resetBackend(req)

	return err
}

func (cmd *resetNVMeCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing reset nvme command: %+v", cmd)
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
