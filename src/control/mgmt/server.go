//
// (C) Copyright 2018 Intel Corporation.
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

package mgmt

import (
	"encoding/json"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
	"github.com/daos-stack/daos/src/control/utils/handlers"
	"github.com/daos-stack/daos/src/control/utils/log"

	"io/ioutil"
)

var jsonDBRelPath = "share/control/mgmtinit_db.json"

// ControlService type is the data container for the service.
type ControlService struct {
	nvme              *nvmeStorage
	scm               *scmStorage
	logger            *log.Logger
	supportedFeatures FeatureMap
}

// Setup delegates to Storage implementation's Setup methods
func (c *ControlService) Setup() {
	if err := c.nvme.Setup(); err != nil {
		println("Failed NVMe subsystem setup: " + err.Error())
	}
	if err := c.scm.Setup(); err != nil {
		println("Failed SCM subsystem setup: " + err.Error())
	}
}

// Teardown delegates to Storage implementation's Teardown methods
func (c *ControlService) Teardown() {
	if err := c.nvme.Teardown(); err != nil {
		println("Failed NVMe subsystem teardown: " + err.Error())
	}
	if err := c.scm.Teardown(); err != nil {
		println("Failed SCM subsystem teardown: " + err.Error())
	}
}

// loadInitData retrieves initial data from relative file path.
func loadInitData(relPath string) (m FeatureMap, err error) {
	absPath, err := handlers.GetAbsInstallPath(relPath)
	if err != nil {
		panic(err)
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

func dumpLocalStorage(name string, i interface{}) {
	println(name + ":")
	s, err := handlers.StructsToString(i)
	if err != nil {
		println("Unable to YAML encode response: " + err.Error())
		return
	}
	println(s)
}

// ShowLocalStorage retrieves and prints details of locally attached SCM and
// NVMe storage to daos_server stdout.
func (c *ControlService) ShowLocalStorage() {
	println("Listing attached storage...")
	if err := c.nvme.Discover(); err != nil {
		println("Failure retrieving NVMe details: " + err.Error())
	} else {
		dumpLocalStorage("NVMe", c.nvme.Controllers)
	}
	if err := c.scm.Discover(); err != nil {
		println("Failure retrieving SCM details: " + err.Error())
	} else {
		dumpLocalStorage("SCM", c.scm.Modules)
	}
}

// NewControlServer creates a new instance of ControlService struct.
func NewControlServer(shmID int) *ControlService {
	logger := log.NewLogger()
	logger.SetLevel(log.Debug)

	fMap, err := loadInitData(jsonDBRelPath)
	if err != nil {
		panic(err)
	}

	return &ControlService{
		nvme:              newNvmeStorage(logger, shmID),
		scm:               newScmStorage(logger),
		logger:            logger,
		supportedFeatures: fMap,
	}
}
