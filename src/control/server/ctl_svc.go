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
	"io/ioutil"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

var jsonDBRelPath = "share/daos/control/mgmtinit_db.json"

// ControlService implements the control plane control service, satisfying
// ctlpb.MgmtCtlServer, and is the data container for the service.
type ControlService struct {
	StorageControlService
	harness           *IOServerHarness
	membership        *common.Membership
	supportedFeatures FeatureMap
}

func NewControlService(l logging.Logger, h *IOServerHarness, sp *scm.Provider, cfg *Configuration,
	m *common.Membership) (*ControlService, error) {

	scs, err := DefaultStorageControlService(l, cfg)
	if err != nil {
		return nil, err
	}
	scs.scm.provider = sp

	fMap, err := loadInitData(jsonDBRelPath)
	if err != nil {
		return nil, err
	}

	return &ControlService{
		StorageControlService: *scs,
		harness:               h,
		membership:            m,
		supportedFeatures:     fMap,
	}, nil
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

	var features []*ctlpb.Feature
	if err = json.Unmarshal(file, &features); err != nil {
		return
	}

	for _, f := range features {
		m[f.Fname.Name] = f
	}

	return
}
