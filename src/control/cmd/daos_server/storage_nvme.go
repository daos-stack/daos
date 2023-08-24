//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os/user"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const cliPCIAddrSep = ","

type (
	nvmePrepareResetFn func(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)
	nvmeScanFn         func(storage.BdevScanRequest) (*storage.BdevScanResponse, error)
)

type nvmeStorageCmd struct {
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

func nvmeBdevsFromCfg(cfg *config.Server) *storage.BdevDeviceList {
	if cfg == nil {
		return nil
	}

	// Combine engine bdev_lists to create total device list.
	var bdevCfgs storage.TierConfigs
	for _, ec := range cfg.Engines {
		bdevCfgs = append(bdevCfgs, ec.Storage.Tiers.BdevConfigs()...)
	}

	bds := bdevCfgs.NVMeBdevs()
	if bds.Len() == 0 {
		return nil
	}

	return bds
}

func updateNVMePrepReqFromConfig(log logging.Logger, cfg *config.Server, req *storage.BdevPrepareRequest) error {
	if cfg == nil {
		log.Debugf("skip updating request from config")
		return nil
	}

	for idx, ec := range cfg.Engines {
		ec.ConvertLegacyStorage(log, idx)
	}

	if cfg.DisableHugepages {
		return errors.New("hugepage usage has been disabled in the config file")
	}

	req.DisableVFIO = req.DisableVFIO || cfg.DisableVFIO

	if req.HugepageCount == 0 && cfg.NrHugepages > 0 {
		req.HugepageCount = cfg.NrHugepages
	}

	if req.PCIAllowList == "" {
		allowed := nvmeBdevsFromCfg(cfg)
		if allowed.Len() != 0 {
			log.Debugf("reading bdev_list entries (%q) from cfg", allowed)
			req.PCIAllowList = strings.Join(allowed.Strings(), storage.BdevPciAddrSep)
		}
	}

	if req.PCIBlockList == "" && len(cfg.BdevExclude) > 0 {
		blocked, err := hardware.NewPCIAddressSet(cfg.BdevExclude...)
		if err != nil {
			return errors.Wrap(err, "invalid addresses in pci block list")
		}
		if blocked.Len() != 0 {
			log.Debugf("reading bdev_exclude entries (%q) from cfg", blocked)
			req.PCIBlockList = strings.Join(blocked.Strings(), storage.BdevPciAddrSep)
		}
	}

	return nil
}

// Commandline PCI address lists will be comma-separated, sanitize into expected format.
func sanitizePCIAddrLists(req *storage.BdevPrepareRequest) error {
	if !strings.Contains(req.PCIAllowList, " ") {
		allowed, err := hardware.NewPCIAddressSet(strings.Split(req.PCIAllowList, cliPCIAddrSep)...)
		if err != nil {
			return errors.Wrap(err, "invalid addresses in pci allow list")
		}
		req.PCIAllowList = strings.Join(allowed.Strings(), storage.BdevPciAddrSep)
	}

	if !strings.Contains(req.PCIBlockList, " ") {
		blocked, err := hardware.NewPCIAddressSet(strings.Split(req.PCIBlockList, cliPCIAddrSep)...)
		if err != nil {
			return errors.Wrap(err, "invalid addresses in pci block list")
		}
		req.PCIBlockList = strings.Join(blocked.Strings(), storage.BdevPciAddrSep)
	}

	return nil
}

type prepareNVMeCmd struct {
	nvmeCmd
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

func prepareNVMe(req storage.BdevPrepareRequest, cmd *nvmeCmd, prepareBackend nvmePrepareResetFn) error {
	cmd.Debug("Prepare locally-attached NVMe storage...")

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

func (cmd *prepareNVMeCmd) Execute(_ []string) error {
	if err := cmd.init(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing prepare drives command: %+v", cmd)

	req := storage.BdevPrepareRequest{
		HugepageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.Args.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		DisableVFIO:   cmd.DisableVFIO,
	}

	return prepareNVMe(req, &cmd.nvmeCmd, scs.NvmePrepare)
}

type resetNVMeCmd struct {
	nvmeCmd
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

func resetNVMe(resetReq storage.BdevPrepareRequest, cmd *nvmeCmd, resetBackend nvmePrepareResetFn) error {
	cmd.Debug("Reset locally-attached NVMe storage...")

	cleanReq := storage.BdevPrepareRequest{
		CleanHugepagesOnly: true,
	}

	msg := "cleanup hugepages before nvme reset"

	if resp, err := resetBackend(cleanReq); err != nil {
		cmd.Errorf("%s", errors.Wrap(err, msg))
	} else {
		cmd.Debugf("%s: %d removed", msg, resp.NrHugepagesRemoved)
	}

	cfgParam := cmd.config
	if cmd.IgnoreConfig {
		cfgParam = nil
	}

	if err := processNVMePrepReq(cmd.Logger, cfgParam, cmd, &resetReq); err != nil {
		return errors.Wrap(err, "processing request parameters")
	}

	// Apply request parameter field values required specifically for reset operation.
	resetReq.HugepageCount = 0
	resetReq.HugeNodes = ""
	resetReq.CleanHugepagesOnly = false
	resetReq.Reset_ = true

	cmd.Debugf("nvme reset request parameters: %+v", resetReq)

	resetResp, err := resetBackend(resetReq)
	if err != nil {
		return errors.Wrap(err, "nvme reset backend")
	}

	// SPDK-2926: If VMD has been detected, perform an extra SPDK reset (without PCI_ALLOWED)
	//            to reset dangling NVMe devices left unbound after the DRIVER_OVERRIDE=none
	//            setup call was used in nvme prepare.
	if resetResp.VMDPrepared {
		resetReq.PCIAllowList = ""
		resetReq.PCIBlockList = ""
		resetReq.EnableVMD = false // Prevents VMD endpoints being auto populated

		cmd.Debugf("vmd second nvme reset request parameters: %+v", resetReq)

		if _, err := resetBackend(resetReq); err != nil {
			return errors.Wrap(err, "nvme reset backend")
		}
	}

	return nil
}

func (cmd *resetNVMeCmd) Execute(_ []string) error {
	if err := cmd.init(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing reset nvme command: %+v", cmd)

	req := storage.BdevPrepareRequest{
		TargetUser:   cmd.TargetUser,
		PCIAllowList: cmd.Args.PCIAllowList,
		PCIBlockList: cmd.PCIBlockList,
		DisableVFIO:  cmd.DisableVFIO,
	}

	return resetNVMe(req, &cmd.nvmeCmd, scs.NvmePrepare)
}

type scanNVMeCmd struct {
	nvmeCmd
	DisableVMD bool `long:"disable-vmd" description:"Disable VMD-aware scan."`
	SkipPrep   bool `long:"skip-prep" description:"Skip preparation of devices during scan."`
}

func (cmd *scanNVMeCmd) getVMDState() bool {
	cfgDisableVMD := false
	if !cmd.IgnoreConfig && cmd.config != nil && cmd.config.DisableVMD != nil {
		cfgDisableVMD = *cmd.config.DisableVMD
	}
	return !cmd.DisableVMD && !cfgDisableVMD
}

func (cmd *scanNVMeCmd) scanNVMe(scanBackend nvmeScanFn, prepResetBackend nvmePrepareResetFn) error {
	var bld strings.Builder
	req := storage.BdevScanRequest{}

	if !cmd.IgnoreConfig && cmd.config != nil {
		req.DeviceList = nvmeBdevsFromCfg(cmd.config)
		if req.DeviceList.Len() > 0 {
			cmd.Debugf("applying devices filter derived from config file: %s", req.DeviceList)
		}
	}

	if !cmd.SkipPrep {
		req := storage.BdevPrepareRequest{
			PCIAllowList: strings.Join(req.DeviceList.Devices(), storage.BdevPciAddrSep),
		}
		if err := prepareNVMe(req, &cmd.nvmeCmd, prepResetBackend); err != nil {
			return errors.Wrap(err,
				"nvme prep before scan failed, try with --skip-prep after manual nvme prepare")
		}
	}

	cmd.Info("Scan locally-attached NVMe storage...")

	resp, err := scanBackend(req)
	if err != nil {
		return err
	}

	if err := pretty.PrintNvmeControllers(resp.Controllers, &bld); err != nil {
		return err
	}

	cmd.Info(bld.String())

	if !cmd.SkipPrep {
		req := storage.BdevPrepareRequest{
			PCIAllowList: strings.Join(req.DeviceList.Devices(), storage.BdevPciAddrSep),
		}
		if err := resetNVMe(req, &cmd.nvmeCmd, prepResetBackend); err != nil {
			return errors.Wrap(err,
				"nvme reset after scan failed, try with --skip-prep before manual nvme reset")
		}
	}

	return nil
}

func (cmd *scanNVMeCmd) Execute(_ []string) error {
	if err := cmd.init(); err != nil {
		return err
	}

	svc := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	if cmd.getVMDState() {
		svc.WithVMDEnabled()
	}

	cmd.Debugf("executing scan nvme command: %+v", cmd)
	return cmd.scanNVMe(svc.NvmeScan, svc.NvmePrepare)
}
