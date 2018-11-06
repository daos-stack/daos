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
	SupportedFeatures FeatureMap
}

// Teardown delegates to Storage implementation's Teardowns
func (c *ControlService) Teardown() (err error) {
	if err = c.nvme.Teardown(); err != nil {
		return
	}
	if err = c.scm.Teardown(); err != nil {
		return
	}
	return
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

// NewControlServer creates a new instance of ControlServer struct.
func NewControlServer() *ControlService {
	logger := log.NewLogger()
	logger.SetLevel(log.Debug)

	fMap, err := loadInitData(jsonDBRelPath)
	if err != nil {
		panic(err)
	}

	return &ControlService{
		nvme:              newNvmeStorage(logger),
		scm:               newScmStorage(logger),
		logger:            logger,
		SupportedFeatures: fMap,
	}
}
