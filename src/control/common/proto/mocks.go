//
// (C) Copyright 2019-2020 Intel Corporation.
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

package proto

import (
	"context"
	"fmt"

	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/pkg/errors"
)

// MockNvmeNamespace is a mock protobuf Namespace message used in tests for
// multiple packages.
func MockNvmeNamespace(varIdx ...int32) *ctlpb.NvmeController_Namespace {
	native := storage.MockNvmeNamespace(varIdx...)
	pb := new(NvmeNamespace)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockSmdDevice is a mock protobuf SmdDevice message used in tests for
// multiple packages.
func MockSmdDevice(varIdx ...int32) *ctlpb.NvmeController_SmdDevice {
	native := storage.MockSmdDevice(varIdx...)
	pb := new(SmdDevice)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockNvmeControllerHealth is a mock protobuf Health message used in tests for
// multiple packages.
func MockNvmeControllerHealth(varIdx ...int32) *ctlpb.NvmeController_Health {
	native := storage.MockNvmeControllerHealth(varIdx...)
	pb := new(NvmeControllerHealth)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockNvmeController is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockNvmeController(varIdx ...int32) *ctlpb.NvmeController {
	native := storage.MockNvmeController(varIdx...)
	pb := new(NvmeController)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockScmModule generates specific protobuf SCM module message used in tests
// for multiple packages.
func MockScmModule(varIdx ...int32) *ctlpb.ScmModule {
	native := storage.MockScmModule(varIdx...)
	pb := new(ScmModule)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockScmNamespace generates specific protobuf SCM namespace message used in tests
// for multiple packages.
func MockScmNamespace(varIdx ...int32) *ctlpb.ScmNamespace {
	native := storage.MockScmNamespace(varIdx...)
	pb := new(ScmNamespace)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockScmMount is a mock protobuf Mount message used in tests for
// multiple packages.
func MockScmMount() *ctlpb.ScmMount {
	return &ctlpb.ScmMount{Mntpoint: "/mnt/daos"}
}

// MockPoolList returns a slice of mock protobuf Pool messages used in tests for
// multiple packages.
var MockPoolList = []*mgmtpb.ListPoolsResp_Pool{
	{Uuid: "12345678-1234-1234-1234-123456789abc", Svcreps: []uint32{1, 2}},
	{Uuid: "12345678-1234-1234-1234-cba987654321", Svcreps: []uint32{0}},
}

type MockMgmtSvcClientConfig struct {
	ListPoolsRet      *common.MockListPoolsResult
	KillErr           error
	PoolQueryResult   *mgmtpb.PoolQueryResp
	PoolQueryErr      error
	PoolSetPropResult *mgmtpb.PoolSetPropResp
	PoolSetPropErr    error
}

type MockMgmtSvcClient struct {
	Cfg   MockMgmtSvcClientConfig
	Calls []string
}

func NewMockMgmtSvcClient(cfg MockMgmtSvcClientConfig) mgmtpb.MgmtSvcClient {
	return &MockMgmtSvcClient{Cfg: cfg}
}

func (m *MockMgmtSvcClient) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq, o ...grpc.CallOption) (*mgmtpb.PoolCreateResp, error) {
	// return successful pool creation results
	// initialize with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.PoolCreateResp{}, nil
}

func (m *MockMgmtSvcClient) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq, o ...grpc.CallOption) (*mgmtpb.PoolDestroyResp, error) {
	// return successful pool destroy results
	// initialize with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.PoolDestroyResp{}, nil
}

func (m *MockMgmtSvcClient) PoolExtend(ctx context.Context, req *mgmtpb.PoolExtendReq, o ...grpc.CallOption) (*mgmtpb.PoolExtendResp, error) {
	return &mgmtpb.PoolExtendResp{}, nil
}

func (m *MockMgmtSvcClient) PoolEvict(ctx context.Context, req *mgmtpb.PoolEvictReq, o ...grpc.CallOption) (*mgmtpb.PoolEvictResp, error) {
	return &mgmtpb.PoolEvictResp{}, nil
}

func (m *MockMgmtSvcClient) PoolReintegrate(ctx context.Context, req *mgmtpb.PoolReintegrateReq, o ...grpc.CallOption) (*mgmtpb.PoolReintegrateResp, error) {
	// return successful pool reintegrate results
	// initialize with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.PoolReintegrateResp{}, nil
}

func (m *MockMgmtSvcClient) PoolExclude(ctx context.Context, req *mgmtpb.PoolExcludeReq, o ...grpc.CallOption) (*mgmtpb.PoolExcludeResp, error) {
	// return successful pool Exclude results
	// initialize with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.PoolExcludeResp{}, nil
}

func (m *MockMgmtSvcClient) PoolDrain(ctx context.Context, req *mgmtpb.PoolDrainReq, o ...grpc.CallOption) (*mgmtpb.PoolDrainResp, error) {
	return &mgmtpb.PoolDrainResp{}, nil
}

func (m *MockMgmtSvcClient) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq, _ ...grpc.CallOption) (*mgmtpb.PoolQueryResp, error) {
	if m.Cfg.PoolQueryErr != nil {
		return nil, m.Cfg.PoolQueryErr
	}
	return m.Cfg.PoolQueryResult, nil
}

func (m *MockMgmtSvcClient) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq, _ ...grpc.CallOption) (*mgmtpb.PoolSetPropResp, error) {
	if m.Cfg.PoolSetPropErr != nil {
		return nil, m.Cfg.PoolSetPropErr
	}
	return m.Cfg.PoolSetPropResult, nil
}

// returnACLResult returns the mock ACL results - either an error or an ACLResp
func (m *MockMgmtSvcClient) returnACLResult() (*mgmtpb.ACLResp, error) {
	return nil, nil
}

func (m *MockMgmtSvcClient) PoolGetACL(ctx context.Context, req *mgmtpb.GetACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *MockMgmtSvcClient) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *MockMgmtSvcClient) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *MockMgmtSvcClient) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *MockMgmtSvcClient) BioHealthQuery(ctx context.Context, req *mgmtpb.BioHealthReq, o ...grpc.CallOption) (*mgmtpb.BioHealthResp, error) {

	// return successful bio health results
	// initialize with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.BioHealthResp{}, nil
}

func (m *MockMgmtSvcClient) SmdQuery(ctx context.Context, req *mgmtpb.SmdQueryReq, o ...grpc.CallOption) (*mgmtpb.SmdQueryResp, error) {
	// return successful device state
	// initialize with zero values indicating mgmt.CTRL_SUCCESS
	return &mgmtpb.SmdQueryResp{}, nil
}

func (m *MockMgmtSvcClient) StorageSetFaulty(ctx context.Context, req *mgmtpb.DevStateReq, o ...grpc.CallOption) (*mgmtpb.DevStateResp, error) {

	// return suscessful FAULTY device state
	// initialize with zero values indicating mgmt.CTRL_SUCCESS
	return &mgmtpb.DevStateResp{}, nil
}

func (m *MockMgmtSvcClient) Join(ctx context.Context, req *mgmtpb.JoinReq, o ...grpc.CallOption) (*mgmtpb.JoinResp, error) {
	m.Calls = append(m.Calls, fmt.Sprintf("Join %d", req.GetRank()))

	// if req rank == NilRank, map uuid to rank
	// otherwise, simulate join where requested rank is valid and returned
	rank := req.GetRank()
	nilRank := system.NilRank
	if nilRank.Uint32() == rank {
		switch req.GetUuid() {
		case common.MockUUID(0):
			rank = 0
		case common.MockUUID(1):
			rank = 1
		default:
			return nil, errors.New("Mock Join doesn't recognise UUID")
		}
	}

	return &mgmtpb.JoinResp{Rank: rank}, nil
}

func (m *MockMgmtSvcClient) GetAttachInfo(ctx context.Context, in *mgmtpb.GetAttachInfoReq, opts ...grpc.CallOption) (*mgmtpb.GetAttachInfoResp, error) {
	return &mgmtpb.GetAttachInfoResp{}, nil
}

func (m *MockMgmtSvcClient) PrepShutdownRanks(ctx context.Context, req *mgmtpb.RanksReq, o ...grpc.CallOption) (*mgmtpb.RanksResp, error) {
	return &mgmtpb.RanksResp{}, nil
}

func (m *MockMgmtSvcClient) StopRanks(ctx context.Context, req *mgmtpb.RanksReq, o ...grpc.CallOption) (*mgmtpb.RanksResp, error) {
	return &mgmtpb.RanksResp{}, nil
}

func (m *MockMgmtSvcClient) PingRanks(ctx context.Context, req *mgmtpb.RanksReq, o ...grpc.CallOption) (*mgmtpb.RanksResp, error) {
	return &mgmtpb.RanksResp{}, nil
}

func (m *MockMgmtSvcClient) ResetFormatRanks(ctx context.Context, req *mgmtpb.RanksReq, o ...grpc.CallOption) (*mgmtpb.RanksResp, error) {
	return &mgmtpb.RanksResp{}, nil
}

func (m *MockMgmtSvcClient) StartRanks(ctx context.Context, req *mgmtpb.RanksReq, o ...grpc.CallOption) (*mgmtpb.RanksResp, error) {
	return &mgmtpb.RanksResp{}, nil
}

func (m *MockMgmtSvcClient) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq, o ...grpc.CallOption) (*mgmtpb.ListPoolsResp, error) {
	if m.Cfg.ListPoolsRet.Err != nil {
		return nil, m.Cfg.ListPoolsRet.Err
	}
	return &mgmtpb.ListPoolsResp{Pools: MockPoolList, Status: m.Cfg.ListPoolsRet.Status}, nil
}

func (m *MockMgmtSvcClient) LeaderQuery(ctx context.Context, req *mgmtpb.LeaderQueryReq, _ ...grpc.CallOption) (*mgmtpb.LeaderQueryResp, error) {
	return &mgmtpb.LeaderQueryResp{}, nil
}

func (m *MockMgmtSvcClient) ListContainers(ctx context.Context, req *mgmtpb.ListContReq, o ...grpc.CallOption) (*mgmtpb.ListContResp, error) {
	// return successful list containers results
	return &mgmtpb.ListContResp{}, nil
}

func (m *MockMgmtSvcClient) ContSetOwner(ctx context.Context, req *mgmtpb.ContSetOwnerReq, o ...grpc.CallOption) (*mgmtpb.ContSetOwnerResp, error) {
	return nil, nil
}
