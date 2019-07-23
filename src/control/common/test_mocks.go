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

package common

import (
	"os/user"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// MockFeaturePB is a mock protobuf Feature message used in tests for multiple
// packages.
func MockFeaturePB() *pb.Feature {
	return &pb.Feature{
		Category:    &pb.Category{Category: "nvme"},
		Fname:       &pb.FeatureName{Name: "burn-name"},
		Description: "run workloads on device to test",
	}
}

// MockNamespacePB is a mock protobuf Namespace message used in tests for
// multiple packages.
func MockNamespacePB() *pb.NvmeController_Namespace {
	return &pb.NvmeController_Namespace{
		Id:       int32(12345),
		Capacity: int32(99999),
	}
}

// MockControllerPB is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockControllerPB(fwRev string) *pb.NvmeController {
	return &pb.NvmeController{
		Model:      "ABC",
		Serial:     "123ABC",
		Pciaddr:    "0000:81:00.0",
		Fwrev:      fwRev,
		Namespaces: []*pb.NvmeController_Namespace{MockNamespacePB()},
	}
}

// NewMockControllerPB generates specific protobuf controller message
func NewMockControllerPB(
	pciAddr string, fwRev string, model string, serial string,
	nss []*pb.NvmeController_Namespace) *pb.NvmeController {

	return &pb.NvmeController{
		Model:      model,
		Serial:     serial,
		Pciaddr:    pciAddr,
		Fwrev:      fwRev,
		Namespaces: nss,
	}
}

// multiple packages.
func MockModulePB() *pb.ScmModule {
	return &pb.ScmModule{
		Physicalid: uint32(12345),
		Capacity:   12345,
		Loc: &pb.ScmModule_Location{
			Channel:    uint32(1),
			Channelpos: uint32(2),
			Memctrlr:   uint32(3),
			Socket:     uint32(4),
		},
	}
}

// MockMountPB is a mock protobuf Mount message used in tests for
// multiple packages.
func MockMountPB() *pb.ScmMount {
	// MockModulePB is a mock protobuf Module message used in tests for
	return &pb.ScmMount{Mntpoint: "/mnt/daos"}
}

// MockCheckMountOk mocks CheckMount and always returns nil error.
func MockCheckMountOk(path string) error {
	return nil
}

// Mocking Users and Groups

// MockUser mimics the user.User type
type MockUser struct {
	Usrname  string
	GrpIDs   []uint32
	GrpIDErr error
}

func (u *MockUser) Username() string {
	return u.Usrname
}

func (u *MockUser) GroupIDs() ([]uint32, error) {
	return u.GrpIDs, u.GrpIDErr
}

func (u *MockUser) GID() uint32 {
	return u.GrpIDs[0]
}

type MockUsers struct {
	lookupUserIDUid        uint32
	LookupUserIDResult     User
	LookupUserIDErr        error
	lookupGroupIDGid       uint32
	LookupGroupIDResults   []*user.Group
	lookupGroupIDCallCount uint32
	LookupGroupIDErr       error
}

func (m *MockUsers) LookupUserID(uid uint32) (User, error) {
	m.lookupUserIDUid = uid
	return m.LookupUserIDResult, m.LookupUserIDErr
}

func (m *MockUsers) LookupGroupID(gid uint32) (*user.Group, error) {
	m.lookupGroupIDGid = gid
	var result *user.Group
	if len(m.LookupGroupIDResults) > 0 {
		result = m.LookupGroupIDResults[m.lookupGroupIDCallCount]
	}
	m.lookupGroupIDCallCount++
	return result, m.LookupGroupIDErr
}

func (m *MockUsers) Current() (User, error) {
	return m.LookupUserIDResult, m.LookupUserIDErr
}
