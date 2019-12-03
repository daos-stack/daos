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

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

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

func (cmd *StoragePrepareCmd) CheckWarn(log *logging.LeveledLogger, state storage.ScmState) error {
	switch state {
	case storage.ScmStateNoRegions:
		if cmd.Reset {
			return nil
		}
	case storage.ScmStateFreeCapacity, storage.ScmStateNoCapacity:
		if !cmd.Reset {
			return nil
		}
	case storage.ScmStateUnknown:
		return errors.New("unknown scm state")
	default:
		return errors.Errorf("unhandled scm state %q", state)
	}

	return cmd.Warn(log)
}

func (cmd *StoragePrepareCmd) Warn(log *logging.LeveledLogger) error {
	log.Info(MsgStoragePrepareWarn)

	if !cmd.Force && !common.GetConsent(log) {
		return errors.New("consent not given")
	}

	return nil
}
