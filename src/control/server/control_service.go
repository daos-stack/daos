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
	"context"
	"encoding/json"
	"io/ioutil"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	log "github.com/daos-stack/daos/src/control/logging"
)

var jsonDBRelPath = "share/daos/control/mgmtinit_db.json"

// ControlService implements the control plane control service, satisfying
// pb.MgmtCtlServer, and is the data container for the service.
type ControlService struct {
	StorageControlService
	log               logging.Logger
	harness           *IOServerHarness
	drpc              drpc.DomainSocketClient
	supportedFeatures FeatureMap
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

func NewControlService(log logging.Logger, harness *IOServerHarness, cfg *Configuration) (*ControlService, error) {
	scs, err := NewStorageControlService(log, cfg)
	if err != nil {
		return nil, err
	}

	fMap, err := loadInitData(jsonDBRelPath)
	if err != nil {
		return nil, err
	}

	return &ControlService{
		StorageControlService: *scs,
		log:                   log,
		harness:               harness,
		drpc:                  scs.drpc,
		supportedFeatures:     fMap,
	}, nil
}

// callDrpcMethodWithMessage create a new drpc Call instance, open a
// drpc connection, send a message with the protobuf message marshalled
// in the body, and closes the connection. Returns unmarshalled response.
func (c *ControlService) callDrpcMethodWithMessage(
	methodID int32, body proto.Message) (resp *pb.DaosResp, err error) {

	drpcResp, err := makeDrpcCall(c.drpc, drpcClientID, methodID, body)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	// unmarshal daos response message returned in drpc response body
	resp = &pb.DaosResp{}
	err = proto.Unmarshal(drpcResp.Body, resp)
	if err != nil {
		return nil, errors.Errorf("invalid dRPC response body: %v", err)
	}

	return
}

// KillRank implements the method defined for the MgmtControl protobuf service.
func (c *ControlService) KillRank(ctx context.Context, rank *pb.DaosRank) (*pb.DaosResp, error) {
	log.Debugf("ControlService.KillRank dispatch\n")

	return c.callDrpcMethodWithMessage(killRank, rank)
}
