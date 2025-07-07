//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os/user"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const cliPCIAddrSep = ","

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

func updateNVMePrepReqAllowedFromCfg(log logging.Logger, cfg *config.Server, req *storage.BdevPrepareRequest) error {
	if cfg == nil {
		return errors.Errorf("nil %T", cfg)
	}
	if req == nil {
		return errors.Errorf("nil %T", req)
	}

	if req.PCIAllowList == "" {
		allowed := nvmeBdevsFromCfg(cfg)
		if allowed.Len() != 0 {
			log.Tracef("reading bdev_list entries (%q) from cfg", allowed)
			req.PCIAllowList = strings.Join(allowed.Strings(), storage.BdevPciAddrSep)
		}
	}

	return nil
}

func updateNVMePrepReqBlockedFromCfg(log logging.Logger, cfg *config.Server, req *storage.BdevPrepareRequest) error {
	if cfg == nil {
		return errors.Errorf("nil %T", cfg)
	}
	if req == nil {
		return errors.Errorf("nil %T", req)
	}

	if req.PCIBlockList == "" && len(cfg.BdevExclude) > 0 {
		blocked, err := hardware.NewPCIAddressSet(cfg.BdevExclude...)
		if err != nil {
			return errors.Wrap(err, "invalid addresses in pci block list")
		}
		if blocked.Len() != 0 {
			log.Tracef("reading bdev_exclude entries (%q) from cfg", blocked)
			req.PCIBlockList = strings.Join(blocked.Strings(), storage.BdevPciAddrSep)
		}
	}

	return nil
}

func updateNVMePrepReqFromCfg(log logging.Logger, cfg *config.Server, req *storage.BdevPrepareRequest) error {
	if cfg == nil {
		return errors.Errorf("nil %T", cfg)
	}
	if req == nil {
		return errors.Errorf("nil %T", req)
	}

	req.DisableVFIO = req.DisableVFIO || cfg.DisableVFIO

	if err := updateNVMePrepReqAllowedFromCfg(log, cfg, req); err != nil {
		return err
	}
	if err := updateNVMePrepReqBlockedFromCfg(log, cfg, req); err != nil {
		return err
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

func isVMDEnabled(cfg *config.Server) bool {
	return cfg == nil || cfg.DisableVMD == nil || !*(cfg.DisableVMD)
}

func processNVMePrepReq(log logging.Logger, cfg *config.Server, iommuChecker hardware.IOMMUDetector, req *storage.BdevPrepareRequest) error {
	if err := sanitizePCIAddrLists(req); err != nil {
		return errors.Wrap(err, "sanitizing cli input pci address lists")
	}

	if cfg != nil {
		if err := updateNVMePrepReqFromCfg(log, cfg, req); err != nil {
			return errors.Wrap(err, "updating request parameters with config file settings")
		}
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
	case !isVMDEnabled(cfg):
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

// Clean hugepages for non-existent processes. Remove lockfiles for any devices specified in request
// or config. Clean all lockfiles if none have been specified on cli or in config. Honor both allow
// and block lists when processing removal of lockfiles.
func cleanSpdkResources(log logging.Logger, req storage.BdevPrepareRequest, cmd *nvmeCmd) {
	cleanReq := storage.BdevPrepareRequest{
		CleanSpdkHugepages: true,
		CleanSpdkLockfiles: true,
	}

	// Differentiate between an empty allow list due to filter combinations (all allowed devices
	// also present in block list) from situation where no devices are selected in cli or config
	// lists. In the latter case, all lockfiles should be removed.
	if req.PCIAllowList == "" && req.PCIBlockList == "" {
		cleanReq.CleanSpdkLockfilesAny = true
	} else {
		cleanReq.PCIAllowList = req.PCIAllowList
		cleanReq.PCIBlockList = req.PCIBlockList
	}

	msg := "cleanup spdk resources"

	cleanResp, err := cmd.ctlSvc.NvmePrepare(cleanReq)
	if err != nil {
		log.Error(errors.Wrap(err, msg).Error())
	} else {
		log.Debugf("%s: %d hugepages and lockfiles %v removed", msg,
			cleanResp.NrHugepagesRemoved, cleanResp.LockfilesRemoved)
	}
}

func prepareNVMe(req storage.BdevPrepareRequest, cmd *nvmeCmd, smi *common.SysMemInfo) error {
	cmd.Debug("Prepare locally-attached NVMe storage...")

	if cmd.config != nil && cmd.config.DisableHugepages {
		return storage.FaultHugepagesDisabled
	}

	if err := processNVMePrepReq(cmd.Logger, cmd.config, cmd, &req); err != nil {
		return errors.Wrap(err, "processing request parameters")
	}

	cleanSpdkResources(cmd.Logger, req, cmd)

	// Set hugepage allocations in prepare request. As evaluating engine affinity requires
	// various prerequisite steps to be performed (as in server service start-up) we don't want
	// to incorrectly assign hugepages to NUMA nodes here (because of not having fully evaluated
	// engine affinity). Instead skip hugepage config processing and simply ensure we have
	// enough to perform NVMe discovery. This introduces some inconsistency in terms of ignoring
	// config parameters but more importantly should prevent inaccurate allocations.

	if req.HugepageCount < config.ScanMinHugepageCount {
		req.HugepageCount = config.ScanMinHugepageCount
	}
	if err := server.SetHugeNodes(cmd.Logger, nil, smi, &req); err != nil {
		return errors.Wrap(err, "setting hugenodes in bdev prep request")
	}

	cmd.Tracef("nvme prepare request parameters: %+v", req)

	// Prepare NVMe device access.
	_, err := cmd.ctlSvc.NvmePrepare(req)

	return errors.Wrap(err, "nvme prepare backend")
}

func (cmd *prepareNVMeCmd) Execute(_ []string) error {
	cmd.Debugf("executing prepare drives command: %+v", cmd)

	req := storage.BdevPrepareRequest{
		HugepageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.Args.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		DisableVFIO:   cmd.DisableVFIO,
	}

	smi, err := common.GetSysMemInfo()
	if err != nil {
		return errors.Wrap(err, "get meminfo")
	}

	return prepareNVMe(req, &cmd.nvmeCmd, smi)
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

func resetNVMe(req storage.BdevPrepareRequest, cmd *nvmeCmd) error {
	cmd.Debug("Reset locally-attached NVMe storage...")

	// For the moment assume that lockfile and hugepage cleanup should be skipped if hugepages
	// have been disabled in the server config.
	if cmd.config != nil && cmd.config.DisableHugepages {
		return storage.FaultHugepagesDisabled
	}

	if err := processNVMePrepReq(cmd.Logger, cmd.config, cmd, &req); err != nil {
		return errors.Wrap(err, "processing request parameters")
	}

	cleanSpdkResources(cmd.Logger, req, cmd)

	// Apply request parameter field values required specifically for reset operation.
	req.HugepageCount = 0
	req.HugeNodes = ""
	req.CleanSpdkHugepages = false
	req.CleanSpdkLockfiles = false
	req.Reset_ = true

	cmd.Tracef("nvme reset request parameters: %+v", req)

	resetResp, err := cmd.ctlSvc.NvmePrepare(req)
	if err != nil {
		return errors.Wrap(err, "nvme reset backend")
	}

	// SPDK-2926: If VMD has been detected, perform an extra SPDK reset (without PCI_ALLOWED)
	//            to reset dangling NVMe devices left unbound after the DRIVER_OVERRIDE=none
	//            setup call was used in nvme prepare. This is required in the situation where
	//            VMD is enabled and PCI_ALLOWED list is set to a subset of VMD controllers
	//            (as specified in the server config file) then the backing devices of the
	//            unselected VMD controllers will be bound to no driver and therefore
	//            inaccessible from both OS and SPDK. Workaround is to run nvme scan
	//            --ignore-config to reset driver bindings.
	if resetResp.VMDPrepared {
		req.PCIAllowList = ""
		req.PCIBlockList = ""
		req.EnableVMD = false // Prevents VMD endpoints being auto populated

		cmd.Tracef("vmd second nvme reset request parameters: %+v", req)

		if _, err := cmd.ctlSvc.NvmePrepare(req); err != nil {
			return errors.Wrap(err, "nvme reset backend")
		}
	}

	return nil
}

func (cmd *resetNVMeCmd) Execute(_ []string) (err error) {
	cmd.Debugf("executing reset nvme command: %+v", cmd)

	req := storage.BdevPrepareRequest{
		TargetUser:   cmd.TargetUser,
		PCIAllowList: cmd.Args.PCIAllowList,
		PCIBlockList: cmd.PCIBlockList,
		DisableVFIO:  cmd.DisableVFIO,
	}

	return resetNVMe(req, &cmd.nvmeCmd)
}

type scanNVMeCmd struct {
	nvmeCmd
	DisableVMD bool `long:"disable-vmd" description:"Disable VMD-aware scan."`
	SkipPrep   bool `long:"skip-prep" description:"Skip preparation of devices during scan."`
}

func (cmd *scanNVMeCmd) getVMDState() bool {
	if cmd.DisableVMD {
		return false
	}

	return isVMDEnabled(cmd.config)
}

func scanNVMe(cmd *scanNVMeCmd, smi *common.SysMemInfo) (_ *storage.BdevScanResponse, errOut error) {
	if cmd.getVMDState() {
		cmd.ctlSvc.WithVMDEnabled()
	}

	req := storage.BdevScanRequest{}

	if cmd.config != nil {
		if cmd.config.DisableHugepages {
			return nil, storage.FaultHugepagesDisabled
		}
		req.DeviceList = nvmeBdevsFromCfg(cmd.config)
		if req.DeviceList.Len() > 0 {
			cmd.Debugf("applying devices filter derived from config file: %s",
				req.DeviceList)
		}
	}

	reqPrep := storage.BdevPrepareRequest{
		PCIAllowList: strings.Join(req.DeviceList.Devices(),
			storage.BdevPciAddrSep),
	}

	if !cmd.SkipPrep {
		if err := prepareNVMe(reqPrep, &cmd.nvmeCmd, smi); err != nil {
			return nil, errors.Wrap(err, "nvme prep before scan failed, try with "+
				"--skip-prep after manual nvme prepare")
		}
		defer func() {
			if err := resetNVMe(reqPrep, &cmd.nvmeCmd); err != nil {
				err = errors.Wrap(err, "nvme reset after scan failed, "+
					"try with --skip-prep before manual nvme reset")
				if errOut == nil {
					errOut = err
					return
				}
				cmd.Error(err.Error())
			}
		}()
	}

	cmd.Info("Scan locally-attached NVMe storage...")

	cmd.Tracef("nvme scan request: %+v", req)
	return cmd.ctlSvc.NvmeScan(req)
}

func (cmd *scanNVMeCmd) Execute(_ []string) (err error) {
	cmd.Debugf("executing scan nvme command: %+v", cmd)

	smi, err := common.GetSysMemInfo()
	if err != nil {
		return errors.Wrap(err, "get meminfo")
	}

	resp, err := scanNVMe(cmd, smi)
	if err != nil {
		return errors.Wrap(err, "nvme scan backend")
	}
	cmd.Tracef("scm scan response: %+v", resp)

	if cmd.JSONOutputEnabled() {
		if err := cmd.OutputJSON(resp.Controllers, nil); err != nil {
			return err
		}

		return nil
	}

	var bld strings.Builder
	if err := pretty.PrintNvmeControllers(resp.Controllers, &bld); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}
