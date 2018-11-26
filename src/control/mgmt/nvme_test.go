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
	"testing"

	. "github.com/daos-stack/daos/src/control/utils/test"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
)

func mockNvmeCS(ns *nvmeStorage) *ControlService {
	return &ControlService{nvme: ns}
}

func TestFetchNvme(t *testing.T) {
	s := mockNvmeCS(newMockNvmeStorage("1.0.0", "1.0.1"))

	if err := s.FetchNvme(); err != nil {
		t.Fatal(err.Error())
	}

	cExpect := MockControllerPB("1.0.0")

	AssertEqual(t, s.nvme.Controllers[cExpect.Id], cExpect, "unexpected Controller populated")
}

func TestUpdateNvmeCtrlr(t *testing.T) {
	s := mockNvmeCS(newMockNvmeStorage("1.0.0", "1.0.1"))

	if err := s.FetchNvme(); err != nil {
		t.Fatal(err.Error())
	}

	cExpect := MockControllerPB("1.0.1")
	c := s.nvme.Controllers[cExpect.Id]

	// after fetching controller details, simulate updated firmware
	// version being reported
	params := &pb.UpdateNvmeCtrlrParams{
		Ctrlr: c, Path: "/foo/bar", Slot: 0}

	newC, err := s.UpdateNvmeCtrlr(nil, params)
	if err != nil {
		t.Fatal(err.Error())
	}

	AssertEqual(t, s.nvme.Controllers[cExpect.Id], cExpect, "unexpected Controller populated")
	AssertEqual(t, newC, cExpect, "unexpected Controller returned")
}

func TestUpdateNvmeCtrlrFail(t *testing.T) {
	s := mockNvmeCS(newMockNvmeStorage("1.0.0", "1.0.0"))

	if err := s.FetchNvme(); err != nil {
		t.Fatal(err.Error())
	}

	cExpect := MockControllerPB("1.0.0")
	c := s.nvme.Controllers[cExpect.Id]

	// after fetching controller details, simulate the same firmware
	// version being reported
	params := &pb.UpdateNvmeCtrlrParams{
		Ctrlr: c, Path: "/foo/bar", Slot: 0}

	_, err := s.UpdateNvmeCtrlr(nil, params)
	ExpectError(t, err, "update failed, firmware revision unchanged", "")
}
