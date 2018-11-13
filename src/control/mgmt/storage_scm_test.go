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

func TestDiscoveryScm(t *testing.T) {
	ss := newMockScmStorage([]DeviceDiscovery{MockModule()})
	m := MockModulePB()

	if err := ss.Discover(); err != nil {
		t.Fatal(err.Error())
	}

	AssertTrue(t, ss.initialised, "expected ScmStorage to have been initialised")
	AssertEqual(t, len(ss.Modules), 1, "unexpected number of modules")
	AssertEqual(t, ss.Modules, ScmmMap{0: m}, "unexpected list of modules")
}
