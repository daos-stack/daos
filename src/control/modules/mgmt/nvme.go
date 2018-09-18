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
	"fmt"
	"os/exec"

	"go-spdk/nvme"
	"go-spdk/spdk"

	"golang.org/x/net/context"

	pb "modules/mgmt/proto"
)

var spdkSetupPath = "share/spdk/scripts/setup.sh"

// Init method implementation for NvmeStorage.
//
// Setup available NVMe devices to be used by SPDK
// and initialise SPDK environment before probing controllers.
func (sn *NvmeStorage) Init() error {
	absSetupPath, err := getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return err
	}
	// run setup to allocate hugepages and bind PCI devices
	// (that don't have active mountpoints) to generic kernel driver
	//
	// todo: we need to be sure that we want to do this, it
	//       will make the controller invisible to other
	//       consumers.
	if err := exec.Command(absSetupPath).Run(); err != nil {
		return err
	}
	if err := spdk.InitSPDKEnv(); err != nil {
		return err
	}
	return nil
}

// Discover method implementation for NvmeStorage
func (sn *NvmeStorage) Discover() interface{} {
	cs, ns, err := nvme.Discover()
	return NVMeReturn{cs, ns, err}
}

// Update method implementation for NvmeStorage
func (sn *NvmeStorage) Update(params interface{}) interface{} {
	switch t := params.(type) {
	case UpdateParams:
		cs, ns, err := nvme.Update(t.CtrlrID, t.Path, t.Slot)
		return NVMeReturn{cs, ns, err}
	default:
		return fmt.Errorf("unexpected return type")
	}
}

// Teardown method implementation for NvmeStorage.
//
// Cleanup references to NVMe devices held by go-spdk
// bindings, rebind PCI devices back to their original drivers
// and cleanup any leftover spdk files/resources.
func (sn *NvmeStorage) Teardown() error {
	nvme.Cleanup()

	absSetupPath, err := getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return err
	}
	if err := exec.Command(absSetupPath, "reset").Run(); err != nil {
		return err
	}
	return nil
}

// loadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func loadControllers(ctrlrs []nvme.Controller) []*pb.NVMeController {
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

// loadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func loadNamespaces(
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

// populateNVMe unpacks return type and loads protobuf representations.
func (s *ControlService) populateNVMe(ret interface{}) error {
	switch ret := ret.(type) {
	case NVMeReturn:
		if ret.Err != nil {
			return ret.Err
		}
		s.NvmeControllers = loadControllers(ret.Ctrlrs)
		s.NvmeNamespaces = loadNamespaces(s.NvmeControllers, ret.Nss)
		s.storageInitialised = true
	default:
		return fmt.Errorf("unexpected return type")
	}

	return nil
}

// FetchNVMe populates controllers and namespaces in ControlService
// as side effect.
//
// todo: presumably we want to be able to detect namespaces added
//       during the lifetime of the daos_server process, in that case
//       will need to rerun discover here (currently SPDK throws
//		 exception if you try to probe a second time).
func (s *ControlService) FetchNVMe() error {
	if s.storageInitialised != true {
		if err := s.Storage.Init(); err != nil {
			return err
		}
		ret := s.Storage.Discover()
		if err := s.populateNVMe(ret); err != nil {
			return err
		}
	}
	return nil
}

// ListNVMeCtrlrs lists all NVMe controllers.
func (s *ControlService) ListNVMeCtrlrs(
	empty *pb.ListNVMeCtrlrsParams, stream pb.MgmtControl_ListNVMeCtrlrsServer) error {
	if err := s.FetchNVMe(); err != nil {
		return err
	}
	for _, c := range s.NvmeControllers {
		if err := stream.Send(c); err != nil {
			return err
		}
	}
	return nil
}

// ListNVMeNss lists all namespaces discovered on attached NVMe controllers.
func (s *ControlService) ListNVMeNss(
	ctrlr *pb.NVMeController, stream pb.MgmtControl_ListNVMeNssServer) error {
	if err := s.FetchNVMe(); err != nil {
		return err
	}
	for _, ns := range s.NvmeNamespaces {
		if ns.Controller.Id == ctrlr.Id {
			if err := stream.Send(ns); err != nil {
				return err
			}
		}
	}
	return nil
}

// UpdateNVMeCtrlr updates the firmware on a NVMe controller, verifying that the
// fwrev reported changes after update.
func (s *ControlService) UpdateNVMeCtrlr(
	ctx context.Context, params *pb.UpdateNVMeCtrlrParams) (*pb.NVMeController, error) {
	id := params.Ctrlr.Id
	ret := s.Storage.Update(UpdateParams{id, params.Path, params.Slot})
	if err := s.populateNVMe(ret); err != nil {
		return nil, err
	}
	for _, c := range s.NvmeControllers {
		if c.Id == id {
			if c.Fwrev == params.Ctrlr.Fwrev {
				return nil, fmt.Errorf("update failed, firmware revision unchanged")
			}
			return c, nil
		}
	}
	return nil, fmt.Errorf("update failed, no matching controller found")
}
