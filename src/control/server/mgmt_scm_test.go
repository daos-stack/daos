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
	. "github.com/daos-stack/go-ipmctl/ipmctl"
	"google.golang.org/grpc"
)

type mockListScmModulesServer struct {
	grpc.ServerStream
	Results []*pb.ScmModule
}

func (m *mockListScmModulesServer) Send(module *pb.ScmModule) error {
	m.Results = append(m.Results, module)
	return nil
}

func TestListScmModules(t *testing.T) {
	cs := defaultMockControlService(t)
	cs.scm = newMockScmStorage([]DeviceDiscovery{MockModule()}, true, cs.config)

	m := MockModulePB()
	mock := &mockListScmModulesServer{}

	cs.ListScmModules(nil, mock)

	AssertEqual(t, len(cs.scm.modules), 1, "unexpected number of modules")
	AssertEqual(t, len(mock.Results), 1, "unexpected number of modules sent")
	AssertEqual(t, mock.Results, []*pb.ScmModule{m}, "unexpected list of modules sent")
}
