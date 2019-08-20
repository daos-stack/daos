//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	log "github.com/daos-stack/daos/src/control/logging"
)

var jsonDBRelPath = "share/daos/control/mgmtinit_db.json"

// ControlService implements the control plane control service, satisfying
// pb.MgmtCtlServer, and is the data container for the service.
type ControlService struct {
	nvme              *nvmeStorage
	scm               *scmStorage
	supportedFeatures FeatureMap
	config            *Configuration
	drpc              drpc.DomainSocketClient
}

// Setup delegates to Storage implementation's Setup methods.
func (c *ControlService) Setup() {
	// init sync primitive for storage formatting on each server
	for idx := range c.config.Servers {
		c.config.Servers[idx].formatted = make(chan struct{})
	}

	if err := c.nvme.Setup(); err != nil {
		log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, NVMe Setup"))
	}

	if err := c.scm.Setup(); err != nil {
		log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, SCM Setup"))
	}
}

// Teardown delegates to Storage implementation's Teardown methods.
func (c *ControlService) Teardown() {
	if err := c.nvme.Teardown(); err != nil {
		log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, NVMe Teardown"))
	}

	if err := c.scm.Teardown(); err != nil {
		log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, SCM Teardown"))
	}
}

func (c *ControlService) ScanNVMe() ([]*pb.NvmeController, error) {
	resp := new(pb.ScanStorageResp)

	c.nvme.Discover(resp)
	if resp.Nvmestate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		return nil, fmt.Errorf("nvme scan: %s", resp.Nvmestate.Error)
	}
	return resp.Ctrlrs, nil
}

func (c *ControlService) ScanSCM() ([]*pb.ScmModule, error) {
	resp := new(pb.ScanStorageResp)

	c.scm.Discover(resp)
	if resp.Scmstate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		return nil, fmt.Errorf("scm scan: %s", resp.Scmstate.Error)
	}
	return resp.Modules, nil
}

type PrepNvmeRequest struct {
	HugePageCount int
	TargetUser    string
	PCIWhitelist  string
	ResetOnly     bool
}

func (c *ControlService) PrepNvme(req PrepNvmeRequest) error {
	// run reset first to ensure reallocation of hugepages
	if err := c.nvme.spdk.reset(); err != nil {
		return errors.WithMessage(err, "SPDK setup reset")
	}

	// if we're only resetting, just return before prep
	if req.ResetOnly {
		return nil
	}

	return errors.WithMessage(
		c.nvme.spdk.prep(req.HugePageCount, req.TargetUser, req.PCIWhitelist),
		"SPDK setup",
	)
}

type PrepScmRequest struct {
	Reset bool
}

func (c *ControlService) PrepScm(req PrepScmRequest) (rebootStr string, pmemDevs []pmemDev, err error) {
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

	var needsReboot bool
	if req.Reset {
		// run reset to remove namespaces and clear regions
		needsReboot, err = c.scm.PrepReset()
		if err != nil {
			err = errors.WithMessage(err, "SCM prep reset")
			return
		}
	} else {
		// transition to the next state in SCM preparation
		needsReboot, pmemDevs, err = c.scm.Prep()
		if err != nil {
			err = errors.WithMessage(err, "SCM prep reset")
			return
		}
	}

	if needsReboot {
		rebootStr = msgScmRebootRequired
	}

	return
}

// awaitStorageFormat checks if running as root and server superblocks exist,
// if both conditions are true, wait until storage is formatted through client
// API calls from management tool. Then drop privileges of running process.
func awaitStorageFormat(config *Configuration) error {
	msgFormat := "storage format on server %d"
	msgSkip := "skipping " + msgFormat
	msgWait := "waiting for " + msgFormat + "\n"

	for i, srv := range config.Servers {
		isMount, err := config.ext.isMountPoint(srv.ScmMount)
		if err == nil && !isMount {
			log.Debugf("attempting to mount existing SCM dir %s\n", srv.ScmMount)

			mntType, devPath, mntOpts, err := getMntParams(srv)
			if err != nil {
				return errors.WithMessage(err, "getting scm mount params")
			}

			log.Debugf("mounting scm %s at %s (%s)...", devPath, srv.ScmMount, mntType)

			err = config.ext.mount(devPath, srv.ScmMount, mntType, uintptr(0), mntOpts)
			if err != nil {
				return errors.WithMessage(err, "mounting existing scm dir")
			}
		} else if !os.IsNotExist(err) {
			return errors.WithMessage(err, "checking scm mounted")
		}

		if ok, err := config.ext.exists(iosrvSuperPath(srv.ScmMount)); err != nil {
			return errors.WithMessage(err, "checking superblock exists")
		} else if ok {
			log.Debugf(msgSkip+" (server already formatted)\n", i)
			continue
		}

		// want this to be visible on stdout and log
		fmt.Printf(msgWait, i)
		log.Debugf(msgWait, i)

		// wait on storage format client API call
		<-srv.formatted
	}

	return nil
}

// loadInitData retrieves initial data from relative file path.
func loadInitData(relPath string) (m FeatureMap, err error) {
	absPath, err := common.GetAbsInstallPath(relPath)
	if err != nil {
		return
	}

	m = make(FeatureMap)

	file, err := ioutil.ReadFile(absPath)
	if err != nil {
		return
	}

	var features []*pb.Feature
	if err = json.Unmarshal(file, &features); err != nil {
		return
	}

	for _, f := range features {
		m[f.Fname.Name] = f
	}

	return
}

func NewControlService(cfg *Configuration) (*ControlService, error) {
	return newControlService(cfg, getDrpcClientConnection(cfg.SocketDir))
}

// newcontrolService creates a new instance of controlService struct.
func newControlService(
	config *Configuration, client drpc.DomainSocketClient) (
	cs *ControlService, err error) {

	fMap, err := loadInitData(jsonDBRelPath)
	if err != nil {
		return
	}

	nvmeStorage, err := newNvmeStorage(config)
	if err != nil {
		return
	}

	cs = &ControlService{
		nvme:              nvmeStorage,
		scm:               newScmStorage(config),
		supportedFeatures: fMap,
		config:            config,
		drpc:              client,
	}
	return
}
