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

package main

import (
	"encoding/json"
	"io/ioutil"
	"sync"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
)

var jsonDBRelPath = "share/control/mgmtinit_db.json"

// controlService type is the data container for the service.
type controlService struct {
	nvme              *nvmeStorage
	scm               *scmStorage
	supportedFeatures FeatureMap
	config            *configuration
	drpc              drpc.DomainSocketClient
}

// Setup delegates to Storage implementation's Setup methods.
func (c *controlService) Setup() {
	// init condition variables used to block until storage gets formatted
	for idx := range c.config.Servers {
		cv := sync.NewCond(&sync.Mutex{})
		cv.L.Lock()

		c.config.Servers[idx].FormatCond = cv
		log.Debugf("Setup(): locked format cond var for server %d\n", idx)
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
func (c *controlService) Teardown() {
	if err := c.nvme.Teardown(); err != nil {
		log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, NVMe Teardown"))
	}

	if err := c.scm.Teardown(); err != nil {
		log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, SCM Teardown"))
	}
}

// Format delegates to Storage implementation's Format methods.
func (c *controlService) FormatStorage(
	ctx context.Context, params *pb.FormatStorageParams) (
	resp *pb.FormatStorageResponse, e error) {

	if c.config.FormatOverride {
		e = errors.New(
			"Format() call unsupported when " +
				"format_override set in server config file, ")
		return
	}

	// TODO: execute in parallel across servers
	for i := range c.config.Servers {
		cond := c.config.Servers[i].FormatCond
		// wait for lock to be released when main is ready
		cond.L.Lock()

		if err := c.nvme.Format(i); err != nil {
			e = errors.WithStack(err)
			cond.L.Unlock()
			return
		}

		if err := c.scm.Format(i); err != nil {
			e = errors.WithStack(err)
			cond.L.Unlock()
			return
		}

		// storage subsystem format successful, signal to alert main.
		log.Debugf("FormatStorage(): signaled cond var for server %d\n", i)
		cond.Signal()
		cond.L.Unlock()
	}

	return
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

// newcontrolService creates a new instance of controlService struct.
func newControlService(
	config *configuration, client drpc.DomainSocketClient) (
	cs *controlService, err error) {

	fMap, err := loadInitData(jsonDBRelPath)
	if err != nil {
		return
	}

	nvmeStorage, err := newNvmeStorage(config)
	if err != nil {
		return
	}

	cs = &controlService{
		nvme:              nvmeStorage,
		scm:               newScmStorage(config),
		supportedFeatures: fMap,
		config:            config,
		drpc:              client,
	}
	return
}
