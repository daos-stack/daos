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
	. "github.com/daos-stack/go-ipmctl/ipmctl"
)

// MockModule returns a mock SCM module of type exported from go-ipmctl.
func MockModule() DeviceDiscovery {
	m := MockModulePB()
	dd := DeviceDiscovery{}
	dd.Physical_id = uint16(m.Physicalid)
	dd.Channel_id = uint16(m.Channel)
	dd.Channel_pos = uint16(m.Channelpos)
	dd.Memory_controller_id = uint16(m.Memctrlr)
	dd.Socket_id = uint16(m.Socket)
	dd.Capacity = m.Capacity
	return dd
}

type mockIpmCtl struct {
	modules []DeviceDiscovery
}

func (m *mockIpmCtl) Discover() ([]DeviceDiscovery, error) {
	return m.modules, nil
}

// mockScmStorage factory
func newMockScmStorage(mms []DeviceDiscovery, inited bool) *scmStorage {
	return &scmStorage{
		ipmCtl:      &mockIpmCtl{modules: mms},
		initialized: inited,
	}
}

func TestDiscoveryScm(t *testing.T) {
	tests := []struct {
		inited bool
		errMsg string
	}{
		{
			true,
			"",
		},
		{
			false,
			"scm storage not initialized",
		},
	}

	mPB := MockModulePB()
	m := MockModule()

	for _, tt := range tests {
		ss := newMockScmStorage([]DeviceDiscovery{m}, tt.inited)

		if err := ss.Discover(); err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		AssertEqual(t, len(ss.modules), 1, "unexpected number of modules")
		AssertEqual(t, ss.modules[int32(mPB.Physicalid)], mPB, "unexpected module values")
	}
}
