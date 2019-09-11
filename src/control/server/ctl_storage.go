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

package server

import (
	"os"

	"github.com/pkg/errors"

	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

// StorageControlService encapsulates the storage part of the control service
type StorageControlService struct {
	log  logging.Logger
	nvme *nvmeStorage
	scm  *scmStorage
	drpc drpc.DomainSocketClient
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, cfg *Configuration) (*StorageControlService, error) {
	scriptPath, err := cfg.ext.getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return nil, err
	}

	spdkScript := &spdkSetup{
		log:         log,
		scriptPath:  scriptPath,
		nrHugePages: cfg.NrHugepages,
	}

	return &StorageControlService{
		log:  log,
		nvme: newNvmeStorage(log, cfg.NvmeShmID, spdkScript, cfg.ext),
		scm:  newScmStorage(log, cfg.ext),
		drpc: getDrpcClientConnection(cfg.SocketDir),
	}, nil
}

type PrepareNvmeRequest struct {
	HugePageCount int
	TargetUser    string
	PCIWhitelist  string
	ResetOnly     bool
}

// PrepareNvme preps locally attached SSDs and returns error.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) PrepareNvme(req PrepareNvmeRequest) error {
	ok, usr := c.nvme.ext.checkSudo()
	if !ok {
		return errors.Errorf("%s must be run as root or sudo", os.Args[0])
	}

	// falls back to sudoer or root if TargetUser is unspecified
	tUsr := usr
	if req.TargetUser != "" {
		tUsr = req.TargetUser
	}

	// run reset first to ensure reallocation of hugepages
	if err := c.nvme.spdk.reset(); err != nil {
		return errors.WithMessage(err, "SPDK setup reset")
	}

	// if we're only resetting, just return before prep
	if req.ResetOnly {
		return nil
	}

	return errors.WithMessage(
		c.nvme.spdk.prep(req.HugePageCount, tUsr, req.PCIWhitelist),
		"SPDK setup",
	)
}

type PrepareScmRequest struct {
	Reset bool
}

// PrepareScm preps locally attached modules and returns need to reboot message,
// list of pmem kernel devices and error directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) PrepareScm(req PrepareScmRequest, state types.ScmState,
) (needsReboot bool, pmemDevs []pmemDev, err error) {

	ok, _ := c.scm.ext.checkSudo()
	if !ok {
		err = errors.Errorf("%s must be run as root or sudo", os.Args[0])
		return
	}

	if err = c.scm.Setup(); err != nil {
		err = errors.WithMessage(err, "SCM setup")
		return
	}

	if !c.scm.initialized {
		err = errors.New(msgScmNotInited)
		return
	}

	if len(c.scm.modules) == 0 {
		err = errors.New(msgScmNoModules)
		return
	}

	if req.Reset {
		// run reset to remove namespaces and clear regions
		needsReboot, err = c.scm.PrepReset(state)
		return
	}

	// transition to the next state in SCM preparation
	return c.scm.Prep(state)
}

// ScanNvme scans locally attached SSDs and returns list directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) ScanNvme() (types.NvmeControllers, error) {
	if err := c.nvme.Discover(); err != nil {
		return nil, errors.Wrap(err, "NVMe storage scan")
	}

	return c.nvme.controllers, nil
}

// ScanScm scans locally attached modules and returns list directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) ScanScm() (types.ScmModules, error) {
	if err := c.scm.Discover(); err != nil {
		return nil, errors.Wrap(err, "SCM storage scan")
	}

	return c.scm.modules, nil
}

// GetScmState returns current state of SCM module preparation
func (c *StorageControlService) GetScmState() (types.ScmState, error) {
	return c.scm.prep.GetState()
}
