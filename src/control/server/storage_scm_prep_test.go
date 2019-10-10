//
// (C) Copyright 2019 Intel Corporation.
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
	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// MockPmemDevice returns a mock pmem device file.
func MockPmemDevice() scm.Namespace {
	pmdPB := MockPmemDevicePB()

	return scm.Namespace{pmdPB.Uuid, pmdPB.Blockdev, pmdPB.Dev, pmdPB.Numanode}
}

// mock implementation of PrepScm interface for external testing
type mockPrepScm struct {
	pmemDevs         []scm.Namespace
	prepNeedsReboot  bool
	resetNeedsReboot bool
	prepRet          error
	resetRet         error
	currentState     ScmState
	getStateRet      error
	getNamespacesRet error
}

func (mp *mockPrepScm) Prep(ScmState) (bool, []scm.Namespace, error) {
	return mp.prepNeedsReboot, mp.pmemDevs, mp.prepRet
}
func (mp *mockPrepScm) PrepReset(ScmState) (bool, error) {
	return mp.resetNeedsReboot, mp.resetRet
}
func (mp *mockPrepScm) GetState() (ScmState, error) {
	return mp.currentState, mp.getStateRet
}
func (mp *mockPrepScm) GetNamespaces() ([]scm.Namespace, error) {
	return mp.pmemDevs, mp.getNamespacesRet
}

func newMockPrepScm() PrepScm {
	return &mockPrepScm{}
}

// tests moved to storage/scm/ipmctl_test.go
