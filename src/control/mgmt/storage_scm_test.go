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

	. "github.com/daos-stack/go-ipmctl/ipmctl"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
	"github.com/daos-stack/daos/src/control/utils/log"
	. "github.com/daos-stack/daos/src/control/utils/test"
)

type mockIpmCtl struct {
	modules []DeviceDiscovery
}

func (m *mockIpmCtl) Discover() ([]DeviceDiscovery, error) {
	return m.modules, nil
}

// mockScmStorage factory
func newMockScmStorage(mms []DeviceDiscovery) *scmStorage {
	return &scmStorage{
		logger: log.NewLogger(),
		IpmCtl: &mockIpmCtl{modules: mms},
	}
}

func mockModule() DeviceDiscovery {
	return DeviceDiscovery{}
	// ID:      int32(12345),
	// Model:   "ABC",
	// Serial:  "123ABC",
	// FWRev:   fwrev,
	// }
}

func mockModulePB() *pb.ScmModule {
	c := mockModule()
	return &pb.ScmModule{
		Physicalid: uint32(c.Physical_id),
		Channel:    uint32(c.Channel_id),
		Channelpos: uint32(c.Channel_pos),
		Memctrlr:   uint32(c.Memory_controller_id),
		Socket:     uint32(c.Socket_id),
		Capacity:   c.Capacity,
	}
}

func TestDiscoveryScm(t *testing.T) {
	ss := newMockScmStorage([]DeviceDiscovery{mockModule()})
	m := mockModulePB()

	if err := ss.Discover(); err != nil {
		t.Fatal(err.Error())
	}

	AssertTrue(t, ss.initialised, "expected ScmStorage to have been initialised")
	AssertEqual(t, len(ss.Modules), 1, "unexpected number of modules")
	AssertEqual(t, ss.Modules, ScmmMap{0: m}, "unexpected list of modules")
}
