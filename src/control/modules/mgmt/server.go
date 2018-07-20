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
	"io/ioutil"
	"os"
	"path/filepath"

	"common/util"
	"go-spdk/spdk"

	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"

	pb "modules/mgmt/proto"
)

var (
	jsonDBRelPath = "share/control/mgmtinit_db.json"
)

// ControlService type is the data container for the service.
type ControlService struct {
	// read-only after initialized
	supportedFeatures []*pb.Feature
	nvmeNamespaces    []*pb.NVMeNamespace
}

// GetFeature returns the feature from feature name.
func (s *ControlService) GetFeature(ctx context.Context, name *pb.FeatureName) (*pb.Feature, error) {
	for _, feature := range s.supportedFeatures {
		if proto.Equal(feature.GetFname(), name) {
			return feature, nil
		}
	}
	// No feature was found, return an unnamed feature.
	return &pb.Feature{Fname: &pb.FeatureName{Name: "none"}, Description: "none"}, nil
}

// ListFeatures lists all features supported by the management server.
func (s *ControlService) ListFeatures(empty *pb.ListFeaturesParams, stream pb.MgmtControl_ListFeaturesServer) error {
	for _, feature := range s.supportedFeatures {
		if err := stream.Send(feature); err != nil {
			return err
		}
	}
	return nil
}

// ListNVMe lists all namespaces discovered on attached NVMe controllers.
func (s *ControlService) ListNVMe(empty *pb.ListNVMeParams, stream pb.MgmtControl_ListNVMeServer) error {
	if err := spdk.InitSPDKEnv(); err != nil {
		return err
	}

	var protoNamespaces []*pb.NVMeNamespace
	spdkNamespaces, err := spdk.NVMeDiscover()
	if err != nil {
		return err
	}
	for _, ns := range spdkNamespaces {
		c := &pb.NVMeCtrlr{Model: ns.CtrlrModel, Serial: ns.CtrlrSerial}
		d := &pb.NVMeNamespace{Controller: c, Id: ns.Id, Capacity: ns.Size}

		protoNamespaces = append(protoNamespaces, d)

		if err := stream.Send(d); err != nil {
			return err
		}
	}
	s.nvmeNamespaces = protoNamespaces

	return nil
}

// loadInitData retrieves initial data from file.
func (s *ControlService) loadInitData(filePath string) {
	file, err := ioutil.ReadFile(filePath)
	if err != nil {
		util.LogGrpcErr(err)
		return
	}
	if err := json.Unmarshal(file, &s.supportedFeatures); err != nil {
		util.LogGrpcErr(err)
		return
	}
}

// NewControlServer creates a new instance of our controlServer struct.
func NewControlServer() *ControlService {
	s := &ControlService{}

	// Retrieve absolute path of init DB file and load data
	ex, err := os.Executable()
	if err != nil {
		panic(err)
	}
	dbAbsPath := filepath.Join(filepath.Dir(ex), "..", jsonDBRelPath)
	s.loadInitData(dbAbsPath)

	return s
}
