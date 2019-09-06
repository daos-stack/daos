//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package common_storage

const MsgStoragePrepareWarn = "Memory allocation goals for SCM will be changed and " +
	"namespaces modified, this will be a destructive operation. Please ensure " +
	"namespaces are unmounted and locally attached SCM & NVMe devices " +
	"are not in use. Please be patient as it may take several minutes " +
	"and subsequent reboot maybe required.\n"

type StoragePrepareNvmeCmd struct {
	PCIWhiteList string `short:"w" long:"pci-whitelist" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate (in MB) for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
}

type StoragePrepareScmCmd struct{}

type StoragePrepareCmd struct {
	StoragePrepareNvmeCmd
	StoragePrepareScmCmd
	Reset bool `long:"reset" description:"Reset SCM modules to memory mode after removing namespaces. Reset SPDK returning NVMe device bindings back to kernel modules."`
	Force bool `short:"f" long:"force" description:"Perform format without prompting for confirmation"`
}
