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
	"testing"

	. "github.com/daos-stack/daos/src/control/common"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestUpdateNvmeCtrlr(t *testing.T) {
	cs := defaultMockControlService(t)

	if err := cs.nvme.Setup(); err != nil {
		t.Fatal(err)
	}

	if err := cs.nvme.Discover(); err != nil {
		t.Fatal(err)
	}

	cExpect := MockControllerPB("1.0.1")
	c := cs.nvme.controllers[0]

	// after fetching controller details, simulate updated firmware
	// version being reported
	params := &pb.UpdateNvmeParams{
		Pciaddr: c.Pciaddr, Path: "/foo/bar", Slot: 0}

	newC, err := cs.UpdateNvmeCtrlr(nil, params)
	if err != nil {
		t.Fatal(err)
	}

	AssertEqual(t, cs.nvme.controllers[0], cExpect, "unexpected Controller populated")
	AssertEqual(t, newC, cExpect, "unexpected Controller returned")
}

// TODO: this test should be moved to client, where the change in fwrev will be
// verified
//func TestUpdateNvmeCtrlrFail(t *testing.T) {
//	s := mockNvmeCS(t, newMockNvmeStorage("1.0.0", "1.0.0", false))
//
//	if err := s.nvme.Discover(); err != nil {
//		t.Fatal(err)
//	}
//
//	c := s.nvme.controllers[0]
//
//	// after fetching controller details, simulate the same firmware
//	// version being reported
//	params := &pb.UpdateNvmeParams{
//		Pciaddr: c.Pciaddr, Path: "/foo/bar", Slot: 0}
//
//	_, err := s.UpdateNvmeCtrlr(nil, params)
//	ExpectError(t, err, "update failed, firmware revision unchanged", "")
//}
