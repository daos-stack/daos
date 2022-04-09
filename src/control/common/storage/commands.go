//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common_storage

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const MsgStoragePrepareWarn = "Memory allocation goals for PMem will be changed and namespaces " +
	"modified, this may be a destructive operation. Please ensure namespaces are unmounted " +
	"and locally attached PMem modules are not in use. Please be patient as it may take " +
	"several minutes and subsequent reboot maybe required.\n"

type StoragePrepareNvmeCmd struct {
	PCIAllowList string `long:"pci-allow-list" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	PCIBlockList string `long:"pci-block-list" description:"Whitespace separated list of PCI devices (by address) to be ignored when unbinding devices from Kernel driver to be used with SPDK (default is no PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
}

type StoragePrepareScmCmd struct {
	NrNamespacesPerSocket uint `short:"S" long:"scm-ns-per-socket" description:"Number of SCM namespaces to create per socket" default:"1"`
}

type StoragePrepareCmd struct {
	StoragePrepareNvmeCmd
	StoragePrepareScmCmd
	NvmeOnly bool `short:"n" long:"nvme-only" description:"Only prepare NVMe storage."`
	ScmOnly  bool `short:"s" long:"scm-only" description:"Only prepare SCM."`
	Reset    bool `long:"reset" description:"Reset SCM modules to memory mode after removing namespaces. Reset SPDK returning NVMe device bindings back to kernel modules."`
	Force    bool `short:"f" long:"force" description:"Perform format without prompting for confirmation"`
}

// Validate checks both only options are not set and returns flags to direct
// which subsystem types to prepare.
func (cmd *StoragePrepareCmd) Validate() (bool, bool, error) {
	prepNvme := cmd.NvmeOnly || !cmd.ScmOnly
	prepScm := cmd.ScmOnly || !cmd.NvmeOnly

	if cmd.NvmeOnly && cmd.ScmOnly {
		return false, false, errors.New(
			"nvme-only and scm-only options should not be set together")
	}

	return prepNvme, prepScm, nil
}

func (cmd *StoragePrepareCmd) Warn(log logging.Logger) error {
	log.Info(MsgStoragePrepareWarn)

	if !cmd.Force && !common.GetConsent(log) {
		return errors.New("consent not given")
	}

	return nil
}
