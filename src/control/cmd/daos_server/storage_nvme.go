//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"os/user"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type nvmePrepareResetFn func(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)

type nvmeCmd struct {
	Prepare prepareDrivesCmd `command:"prepare-drives" description:"Prepare NVMe SSDs for use with DAOS"`
	Release releaseDrivesCmd `command:"release-drives" description:"Release NVMe SSDs for use with OS"`
	Scan    scanDrivesCmd    `command:"scan" description:"Scan NVMe SSDs"`
}

func getTargetUser(reqUser string) (string, error) {
	if reqUser == "" {
		runningUser, err := user.Current()
		if err != nil {
			return "", errors.Wrap(err, "couldn't lookup running user")
		}
		return runningUser.Username, nil
	}
	return reqUser, nil
}

func validateVFIOSetting(targetUser string, reqDisableVFIO bool, iommuEnabled bool) error {
	if targetUser != "root" {
		if reqDisableVFIO {
			return errors.New("VFIO can not be disabled if running as non-root user")
		}
		if !iommuEnabled {
			return errors.New("no IOMMU detected, to discover NVMe devices enable " +
				"IOMMU per the DAOS Admin Guide")
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
	cmdutil.LogCmd `json:"-"`
	PCIAllowList   string `long:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	PCIBlockList   string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	NrHugepages    int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	TargetUser     string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO    bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	HelperLogFile  string `short:"l" long:"helper-log-file" description:"Log file location for debug from daos_admin binary"`
}

func (cmd *prepareDrivesCmd) WithDisableVFIO(b bool) *prepareDrivesCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *prepareDrivesCmd) WithTargetUser(u string) *prepareDrivesCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *prepareDrivesCmd) prepareNVMe(backendCall nvmePrepareResetFn, iommuEnabled bool) error {
	cmd.Info("Prepare locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		DisableVFIO:   cmd.DisableVFIO,
	}

	if err := updatePrepReqParams(cmd, iommuEnabled, &req); err != nil {
		return errors.Wrap(err, "evaluating vmd capability on platform")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Configure NVMe device access.
	_, err := backendCall(req)

	return err
}

func (cmd *prepareDrivesCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	iommuEnabled, err := hwprov.DefaultIOMMUDetector(cmd).IsIOMMUEnabled()
	if err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd, config.DefaultServer().Engines)

	cmd.Debugf("executing prepare drives command: %+v", cmd)
	return cmd.prepareNVMe(scs.NvmePrepare, iommuEnabled)
}

type releaseDrivesCmd struct {
	cmdutil.LogCmd `json:"-"`
	PCIAllowList   string `long:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)"`
	PCIBlockList   string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)"`
	TargetUser     string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	DisableVFIO    bool   `long:"disable-vfio" description:"Force SPDK to use the UIO driver for NVMe device access"`
	HelperLogFile  string `short:"l" long:"helper-log-file" description:"Log file location for debug from daos_admin binary"`
}

func (cmd *releaseDrivesCmd) WithDisableVFIO(b bool) *releaseDrivesCmd {
	cmd.DisableVFIO = b
	return cmd
}

func (cmd *releaseDrivesCmd) WithTargetUser(u string) *releaseDrivesCmd {
	cmd.TargetUser = u
	return cmd
}

func (cmd *releaseDrivesCmd) resetNVMe(backendCall nvmePrepareResetFn, iommuEnabled bool) error {
	cmd.Info("Release locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		TargetUser:   cmd.TargetUser,
		PCIAllowList: cmd.PCIAllowList,
		PCIBlockList: cmd.PCIBlockList,
		DisableVFIO:  cmd.DisableVFIO,
		Reset_:       true,
	}

	if err := updatePrepReqParams(cmd, iommuEnabled, &req); err != nil {
		return errors.Wrap(err, "evaluating vmd capability on platform")
	}

	cmd.Debugf("nvme prepare request parameters: %+v", req)

	// Configure NVMe device access.
	_, err := backendCall(req)

	return err
}

func (cmd *releaseDrivesCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	iommuEnabled, err := hwprov.DefaultIOMMUDetector(cmd).IsIOMMUEnabled()
	if err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd, config.DefaultServer().Engines)

	cmd.Debugf("executing release drives command: %+v", cmd)
	return cmd.resetNVMe(scs.NvmePrepare, iommuEnabled)
}

type scanDrivesCmd struct {
	cmdutil.LogCmd `json:"-"`
	DisableVMD     bool   `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
	HelperLogFile  string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
}

func (cmd *scanDrivesCmd) Execute(args []string) error {
	var bld strings.Builder

	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
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
