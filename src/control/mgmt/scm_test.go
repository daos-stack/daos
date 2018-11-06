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
	"google.golang.org/grpc"

	. "github.com/daos-stack/daos/src/control/utils/test"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
)

type mockListScmModulesServer struct {
	grpc.ServerStream
	Results []*pb.ScmModule
}

func (m *mockListScmModulesServer) Send(module *pb.ScmModule) error {
	m.Results = append(m.Results, module)
	return nil
}

func mockScmCS(ss *scmStorage) *ControlService {
	return &ControlService{scm: ss}
}

func TestListScmModules(t *testing.T) {
	s := mockScmCS(newMockScmStorage([]DeviceDiscovery{mockModule()}))
	m := mockModulePB()

	mock := &mockListScmModulesServer{}
	s.ListScmModules(nil, mock)

	AssertTrue(t, s.scm.initialised, "expected ScmStorage to have been initialised")
	AssertEqual(t, len(s.scm.Modules), 1, "unexpected number of modules")
	AssertEqual(t, s.scm.Modules, ScmmMap{0: m}, "unexpected list of modules")
	AssertEqual(t, len(mock.Results), 1, "unexpected number of modules sent")
	AssertEqual(t, mock.Results, []*pb.ScmModule{m}, "unexpected list of modules sent")
}
