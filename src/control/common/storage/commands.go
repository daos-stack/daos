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

type StoragePrepareNvmeCmd struct {
	PCIWhiteList string `short:"w" long:"pci-whitelist" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate (in MB) for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	Reset        bool   `short:"r" long:"reset" description:"Reset SPDK returning devices to kernel modules"`
}

type StoragePrepareScmCmd struct {
	Reset bool `short:"r" long:"reset" description:"Reset modules to memory mode after removing namespaces"`
	Force bool `short:"f" long:"force" description:"Perform format without prompting for confirmation"`
}
