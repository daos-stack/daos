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

	"common/log"
	"go-spdk/nvme"
	"go-spdk/spdk"

	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"

	pb "modules/mgmt/proto"
)

var (
	jsonDBRelPath = "share/control/mgmtinit_db.json"
)

// Nvme interface represents binding to SPDK NVMe library
type Nvme interface {
	SpdkInit() error
	Discover() ([]nvme.Controller, []nvme.Namespace, error)
}

// SpdkNvme struct implements Nvme interface
type SpdkNvme struct{}

// SpdkInit Nvme interface method implementation
func (sn *SpdkNvme) SpdkInit() error {
	if err := spdk.InitSPDKEnv(); err != nil {
		return err
	}

	return nil
}

// Discover Nvme interface method implementation
func (sn *SpdkNvme) Discover() ([]nvme.Controller, []nvme.Namespace, error) {
	return nvme.Discover()
}

// ControlService type is the data container for the service.
type ControlService struct {
	logger *log.Logger
	Nvme
	nvmeInitialised   bool
	supportedFeatures []*pb.Feature
	nvmeNamespaces    []*pb.NVMeNamespace
	nvmeControllers   []*pb.NVMeController
}

// GetFeature returns the feature from feature name.
func (s *ControlService) GetFeature(
	ctx context.Context, name *pb.FeatureName) (*pb.Feature, error) {

	for _, feature := range s.supportedFeatures {
		if proto.Equal(feature.GetFname(), name) {
			return feature, nil
		}
	}
	// No feature was found, return an unnamed feature.
	return &pb.Feature{Fname: &pb.FeatureName{Name: "none"}, Description: "none"}, nil
}

// ListAllFeatures lists all features supported by the management server.
func (s *ControlService) ListAllFeatures(
	empty *pb.ListAllFeaturesParams, stream pb.MgmtControl_ListAllFeaturesServer) error {

	for _, feature := range s.supportedFeatures {
		if err := stream.Send(feature); err != nil {
			return err
		}
	}
	return nil
}

// ListFeatures lists all features supported by the management server.
func (s *ControlService) ListFeatures(
	category *pb.Category, stream pb.MgmtControl_ListFeaturesServer) error {

	for _, feature := range s.supportedFeatures {
		if proto.Equal(feature.GetCategory(), category) {
			if err := stream.Send(feature); err != nil {
				return err
			}
		}
	}
	return nil
}

// LoadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func LoadControllers(ctrlrs []nvme.Controller) []*pb.NVMeController {
	var pbCtrlrs []*pb.NVMeController
	for _, c := range ctrlrs {
		pbCtrlrs = append(
			pbCtrlrs,
			&pb.NVMeController{
				Id:      c.ID,
				Model:   c.Model,
				Serial:  c.Serial,
				Pciaddr: c.PCIAddr,
				Fwrev:   c.FWRev,
			})
	}

	return pbCtrlrs
}

// LoadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func LoadNamespaces(
	pbCtrlrs []*pb.NVMeController, nss []nvme.Namespace) []*pb.NVMeNamespace {

	var pbNamespaces []*pb.NVMeNamespace
	for _, ns := range nss {
		for _, c := range pbCtrlrs {
			if c.Id == ns.CtrlrID {
				pbNamespaces = append(
					pbNamespaces,
					&pb.NVMeNamespace{
						Controller: c,
						Id:         ns.ID,
						Capacity:   ns.Size,
					})
			}
		}
	}

	return pbNamespaces
}

// fetchNVMe populates controllers and namespaces in ControlService
// as side effect.
//
// todo: presumably we want to be able to detect namespaces added
//       during the lifetime of the daos_server process, in that case
//       will need to rerun discover here (currently SPDK throws).
func (s *ControlService) fetchNVMe() error {
	if s.nvmeInitialised != true {
		if err := s.Nvme.SpdkInit(); err != nil {
			return err
		}

		ctrlrs, nss, err := s.Nvme.Discover()
		if err != nil {
			return err
		}

		s.nvmeControllers = LoadControllers(ctrlrs)
		s.nvmeNamespaces = LoadNamespaces(s.nvmeControllers, nss)
		s.nvmeInitialised = true
	}

	return nil
}

// ListNVMeCtrlrs lists all NVMe controllers.
func (s *ControlService) ListNVMeCtrlrs(
	empty *pb.ListNVMeCtrlrsParams, stream pb.MgmtControl_ListNVMeCtrlrsServer) error {

	if err := s.fetchNVMe(); err != nil {
		return err
	}
	for _, c := range s.nvmeControllers {
		if err := stream.Send(c); err != nil {
			return err
		}
	}
	return nil
}

// ListNVMeNss lists all namespaces discovered on attached NVMe controllers.
func (s *ControlService) ListNVMeNss(
	ctrlr *pb.NVMeController, stream pb.MgmtControl_ListNVMeNssServer) error {

	if err := s.fetchNVMe(); err != nil {
		return err
	}
	for _, ns := range s.nvmeNamespaces {
		if ns.Controller.Id == ctrlr.Id {
			if err := stream.Send(ns); err != nil {
				return err
			}
		}
	}

	return nil
}

// loadInitData retrieves initial data from file.
func (s *ControlService) loadInitData(filePath string) error {
	file, err := ioutil.ReadFile(filePath)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(file, &s.supportedFeatures); err != nil {
		return err
	}

	return nil
}

// NewControlServer creates a new instance of our ControlServer struct.
func NewControlServer() *ControlService {
	s := &ControlService{Nvme: &SpdkNvme{}, nvmeInitialised: false}

	// Retrieve absolute path of init DB file and load data
	ex, err := os.Executable()
	if err != nil {
		panic(err)
	}
	dbAbsPath := filepath.Join(filepath.Dir(ex), "..", jsonDBRelPath)
	if err := s.loadInitData(dbAbsPath); err != nil {
		panic(err)
	}

	// Discover NVMe controllers, assumes they will not be added
	// during the lifetime of daos_server process
	//if err := s.getControllers(); err != nil {
	//	panic(err)
	//}

	s.logger = log.NewLogger()

	return s
}
