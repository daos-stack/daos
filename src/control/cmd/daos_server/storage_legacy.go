//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import "github.com/pkg/errors"

type StoragePrepareLegacyCmd struct {
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
func (cmd *StoragePrepareCmd) Validate() (bool, bool, error) {
	prepNvme := cmd.NvmeOnly || !cmd.ScmOnly
	prepScm := cmd.ScmOnly || !cmd.NvmeOnly

	if cmd.NvmeOnly && cmd.ScmOnly {
		return false, false, errors.New(
			"nvme-only and scm-only options should not be set together")
	}

	return prepNvme, prepScm, nil
}
