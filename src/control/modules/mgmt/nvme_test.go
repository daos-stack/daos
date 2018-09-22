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

package mgmt_test

import (
	"testing"

	. "common/test"
	. "go-spdk/nvme"
	. "modules/mgmt"

	pb "modules/mgmt/proto"
)

func mockController(fwrev string) Controller {
	return Controller{
		ID:      int32(12345),
		Model:   "ABC",
		Serial:  "123ABC",
		PCIAddr: "1:2:3.0",
		FWRev:   fwrev,
	}
}
func mockNamespace(ctrlr *Controller) Namespace {
	return Namespace{
		ID:      ctrlr.ID,
		Size:    int32(99999),
		CtrlrID: int32(12345),
	}
}
func mockControllerPB(fwRev string) *pb.NVMeController {
	c := mockController(fwRev)
	return &pb.NVMeController{
		Id:      c.ID,
		Model:   c.Model,
		Serial:  c.Serial,
		Pciaddr: c.PCIAddr,
		Fwrev:   c.FWRev,
	}
}
func mockNamespacePB(fwRev string) *pb.NVMeNamespace {
	c := mockController(fwRev)
	ns := mockNamespace(&c)
	return &pb.NVMeNamespace{
		Controller: mockControllerPB(fwRev),
		Id:         ns.ID,
		Capacity:   ns.Size,
	}
}

func NewTestControlServer(storageImpl Storage) *ControlService {
	return &ControlService{Storage: storageImpl}
}

func TestFetchNVMe(t *testing.T) {
	s := NewTestControlServer(&mockStorage{"1.0.0", "1.0.1"})

	if err := s.FetchNVMe(); err != nil {
		t.Fatal(err.Error())
	}

	cExpect := mockControllerPB("1.0.0")
	nsExpect := mockNamespacePB("1.0.0")

	AssertEqual(t, s.NvmeControllers[cExpect.Id], cExpect, "unexpected Controller populated")
	AssertEqual(t, s.NvmeNamespaces[nsExpect.Id], nsExpect, "unexpected Namespace populated")
}

func TestUpdateNVMe(t *testing.T) {
	s := NewTestControlServer(&mockStorage{"1.0.0", "1.0.1"})

	if err := s.FetchNVMe(); err != nil {
		t.Fatal(err.Error())
	}

	cExpect := mockControllerPB("1.0.1")
	c := s.NvmeControllers[cExpect.Id]

	// after fetching controller details, simulate updated firmware
	// version being reported
	params := &pb.UpdateNVMeCtrlrParams{
		Ctrlr: c, Path: "/foo/bar", Slot: 0}

	newC, err := s.UpdateNVMeCtrlr(nil, params)
	if err != nil {
		t.Fatal(err.Error())
	}

	AssertEqual(t, s.NvmeControllers[cExpect.Id], cExpect, "unexpected Controller populated")
	AssertEqual(t, newC, cExpect, "unexpected Controller returned")
}

func TestUpdateNVMeFail(t *testing.T) {
	s := NewTestControlServer(&mockStorage{"1.0.0", "1.0.0"})

	if err := s.FetchNVMe(); err != nil {
		t.Fatal(err.Error())
	}

	cExpect := mockControllerPB("1.0.0")
	c := s.NvmeControllers[cExpect.Id]

	// after fetching controller details, simulate the same firmware
	// version being reported
	params := &pb.UpdateNVMeCtrlrParams{
		Ctrlr: c, Path: "/foo/bar", Slot: 0}

	_, err := s.UpdateNVMeCtrlr(nil, params)
	ExpectError(t, err, "update failed, firmware revision unchanged")
}
