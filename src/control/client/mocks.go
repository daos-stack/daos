//
// (C) Copyright 2018-2020 Intel Corporation.
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

package client

import (
	"io"
	"sort"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	grpc "google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"

	. "github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

var (
	MockUUID         = "00000000-0000-0000-0000-000000000000"
	MockServers      = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	MockCtrlrs       = NvmeControllers{MockNvmeController()}
	MockSuccessState = ctlpb.ResponseState{Status: ctlpb.ResponseStatus_CTL_SUCCESS}
	MockState        = ctlpb.ResponseState{
		Status: ctlpb.ResponseStatus_CTL_ERR_APP,
		Error:  "example application error",
	}
	MockCtrlrResults = NvmeControllerResults{
		&ctlpb.NvmeControllerResult{
			Pciaddr: "0000:81:00.0",
			State:   &MockState,
		},
	}
	MockScmModules    = ScmModules{MockScmModule()}
	MockModuleResults = ScmModuleResults{
		&ctlpb.ScmModuleResult{
			Loc:   &ctlpb.ScmModule_Location{},
			State: &MockState,
		},
	}
	MockScmNamespaces = ScmNamespaces{MockPmemDevice()}
	MockMounts        = ScmMounts{MockScmMount()}
	MockMountResults  = ScmMountResults{
		&ctlpb.ScmMountResult{
			Mntpoint: "/mnt/daos",
			State:    &MockState,
		},
	}
	MockACL = &mockACLResult{
		acl: []string{
			"A::OWNER@:rw",
			"A::GROUP@:r",
		},
	}
	MockPoolList = []*mgmtpb.ListPoolsResp_Pool{
		{Uuid: "12345678-1234-1234-1234-123456789abc", Svcreps: []uint32{1, 2}},
		{Uuid: "12345678-1234-1234-1234-cba987654321", Svcreps: []uint32{0}},
	}
	MockErr = errors.New("unknown failure")
)

type mgmtCtlStorageFormatClient struct {
	grpc.ClientStream
	ctrlrResults  NvmeControllerResults
	mountResults  ScmMountResults
	alreadyCalled bool
}

func (m *mgmtCtlStorageFormatClient) Recv() (*ctlpb.StorageFormatResp, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &ctlpb.StorageFormatResp{
		Crets: m.ctrlrResults,
		Mrets: m.mountResults,
	}, nil
}

type mockMgmtCtlClientConfig struct {
	nvmeControllers       NvmeControllers
	nvmeControllerResults NvmeControllerResults
	scmModules            ScmModules
	scmModuleResults      ScmModuleResults
	scmNamespaces         ScmNamespaces
	scmMountResults       ScmMountResults
	scanRet               error
	formatRet             error
}

type mockMgmtCtlClient struct {
	cfg mockMgmtCtlClientConfig
}

func (m *mockMgmtCtlClient) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq, o ...grpc.CallOption) (*ctlpb.StoragePrepareResp, error) {
	// return successful prepare results, state member messages
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &ctlpb.StoragePrepareResp{
		Nvme: &ctlpb.PrepareNvmeResp{
			State: &MockSuccessState,
		},
		Scm: &ctlpb.PrepareScmResp{
			State: &MockSuccessState,
		},
	}, m.cfg.scanRet
}

func (m *mockMgmtCtlClient) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq, o ...grpc.CallOption) (*ctlpb.StorageScanResp, error) {
	// return successful query results, state member messages
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &ctlpb.StorageScanResp{
		Nvme: &ctlpb.ScanNvmeResp{
			State:  &MockSuccessState,
			Ctrlrs: m.cfg.nvmeControllers,
		},
		Scm: &ctlpb.ScanScmResp{
			State:   &MockSuccessState,
			Modules: m.cfg.scmModules,
			Pmems:   m.cfg.scmNamespaces,
		},
	}, m.cfg.scanRet
}

func (m *mockMgmtCtlClient) StorageFormat(ctx context.Context, req *ctlpb.StorageFormatReq, o ...grpc.CallOption) (ctlpb.MgmtCtl_StorageFormatClient, error) {
	return &mgmtCtlStorageFormatClient{
			ctrlrResults: m.cfg.nvmeControllerResults,
			mountResults: m.cfg.scmMountResults},
		m.cfg.formatRet
}

type mgmtCtlNetworkScanDevicesClient struct {
	grpc.ClientStream
}

func (m *mgmtCtlNetworkScanDevicesClient) Recv() (*ctlpb.DeviceScanReply, error) {
	return &ctlpb.DeviceScanReply{}, nil
}

func (m *mockMgmtCtlClient) NetworkScanDevices(ctx context.Context, in *ctlpb.DeviceScanRequest, o ...grpc.CallOption) (ctlpb.MgmtCtl_NetworkScanDevicesClient, error) {
	return &mgmtCtlNetworkScanDevicesClient{}, nil
}

func (m *mockMgmtCtlClient) NetworkListProviders(ctx context.Context, in *ctlpb.ProviderListRequest, o ...grpc.CallOption) (*ctlpb.ProviderListReply, error) {
	return &ctlpb.ProviderListReply{}, nil
}

func (m *mockMgmtCtlClient) SystemQuery(ctx context.Context, req *ctlpb.SystemQueryReq, o ...grpc.CallOption) (*ctlpb.SystemQueryResp, error) {
	return &ctlpb.SystemQueryResp{}, nil
}

func (m *mockMgmtCtlClient) SystemStop(ctx context.Context, req *ctlpb.SystemStopReq, o ...grpc.CallOption) (*ctlpb.SystemStopResp, error) {
	return &ctlpb.SystemStopResp{}, nil
}

func (m *mockMgmtCtlClient) SystemStart(ctx context.Context, req *ctlpb.SystemStartReq, o ...grpc.CallOption) (*ctlpb.SystemStartResp, error) {
	return &ctlpb.SystemStartResp{}, nil
}

type mockACLResult struct {
	acl    []string
	status int32
	err    error
}

// ACL returns a properly formed AccessControlList from the mock data
func (m *mockACLResult) ACL() *AccessControlList {
	return &AccessControlList{
		Entries: m.acl,
	}
}

type mockListPoolsResult struct {
	status int32
	err    error
}

type mockMgmtSvcClientConfig struct {
	ACLRet            *mockACLResult
	ListPoolsRet      *mockListPoolsResult
	killErr           error
	poolQueryResult   *mgmtpb.PoolQueryResp
	poolQueryErr      error
	poolSetPropResult *mgmtpb.PoolSetPropResp
	poolSetPropErr    error
}

type mockMgmtSvcClient struct {
	cfg mockMgmtSvcClientConfig
}

func (m *mockMgmtSvcClient) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq, o ...grpc.CallOption) (*mgmtpb.PoolCreateResp, error) {
	// return successful pool creation results
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.PoolCreateResp{}, nil
}

func (m *mockMgmtSvcClient) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq, o ...grpc.CallOption) (*mgmtpb.PoolDestroyResp, error) {
	// return successful pool destroy results
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.PoolDestroyResp{}, nil
}

func (m *mockMgmtSvcClient) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq, _ ...grpc.CallOption) (*mgmtpb.PoolQueryResp, error) {
	if m.cfg.poolQueryErr != nil {
		return nil, m.cfg.poolQueryErr
	}
	return m.cfg.poolQueryResult, nil
}

func (m *mockMgmtSvcClient) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq, _ ...grpc.CallOption) (*mgmtpb.PoolSetPropResp, error) {
	if m.cfg.poolSetPropErr != nil {
		return nil, m.cfg.poolSetPropErr
	}
	return m.cfg.poolSetPropResult, nil
}

// returnACLResult returns the mock ACL results - either an error or an ACLResp
func (m *mockMgmtSvcClient) returnACLResult() (*mgmtpb.ACLResp, error) {
	if m.cfg.ACLRet.err != nil {
		return nil, m.cfg.ACLRet.err
	}
	return &mgmtpb.ACLResp{ACL: m.cfg.ACLRet.acl, Status: m.cfg.ACLRet.status}, nil
}

func (m *mockMgmtSvcClient) PoolGetACL(ctx context.Context, req *mgmtpb.GetACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *mockMgmtSvcClient) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *mockMgmtSvcClient) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *mockMgmtSvcClient) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq, o ...grpc.CallOption) (*mgmtpb.ACLResp, error) {
	return m.returnACLResult()
}

func (m *mockMgmtSvcClient) BioHealthQuery(ctx context.Context, req *mgmtpb.BioHealthReq, o ...grpc.CallOption) (*mgmtpb.BioHealthResp, error) {

	// return successful bio health results
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.BioHealthResp{}, nil
}

func (m *mockMgmtSvcClient) SmdListDevs(ctx context.Context, req *mgmtpb.SmdDevReq, o ...grpc.CallOption) (*mgmtpb.SmdDevResp, error) {

	// return successful SMD device list
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.SmdDevResp{}, nil
}

func (m *mockMgmtSvcClient) SmdListPools(ctx context.Context, req *mgmtpb.SmdPoolReq, o ...grpc.CallOption) (*mgmtpb.SmdPoolResp, error) {

	// return successful SMD pool list
	// initialise with zero values indicating mgmt.CTL_SUCCESS
	return &mgmtpb.SmdPoolResp{}, nil
}

func (m *mockMgmtSvcClient) DevStateQuery(ctx context.Context, req *mgmtpb.DevStateReq, o ...grpc.CallOption) (*mgmtpb.DevStateResp, error) {

	// return successful device state
	// initialise with zero values indicating mgmt.CTRL_SUCCESS
	return &mgmtpb.DevStateResp{}, nil
}

func (m *mockMgmtSvcClient) StorageSetFaulty(ctx context.Context, req *mgmtpb.DevStateReq, o ...grpc.CallOption) (*mgmtpb.DevStateResp, error) {

	// return suscessful FAULTY device state
	// initialise with zero values indicating mgmt.CTRL_SUCCESS
	return &mgmtpb.DevStateResp{}, nil
}

func (m *mockMgmtSvcClient) Join(ctx context.Context, req *mgmtpb.JoinReq, o ...grpc.CallOption) (*mgmtpb.JoinResp, error) {

	return &mgmtpb.JoinResp{}, nil
}

func (m *mockMgmtSvcClient) GetAttachInfo(ctx context.Context, in *mgmtpb.GetAttachInfoReq, opts ...grpc.CallOption) (*mgmtpb.GetAttachInfoResp, error) {
	return &mgmtpb.GetAttachInfoResp{}, nil
}

func (m *mockMgmtSvcClient) PrepShutdown(ctx context.Context, req *mgmtpb.PrepShutdownReq, o ...grpc.CallOption) (*mgmtpb.DaosResp, error) {
	return &mgmtpb.DaosResp{}, nil
}

func (m *mockMgmtSvcClient) KillRank(ctx context.Context, req *mgmtpb.KillRankReq, o ...grpc.CallOption) (*mgmtpb.DaosResp, error) {
	return &mgmtpb.DaosResp{}, nil
}

func (m *mockMgmtSvcClient) StartRanks(ctx context.Context, req *mgmtpb.StartRanksReq, o ...grpc.CallOption) (*mgmtpb.StartRanksResp, error) {
	return &mgmtpb.StartRanksResp{}, nil
}

func (m *mockMgmtSvcClient) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq, o ...grpc.CallOption) (*mgmtpb.ListPoolsResp, error) {
	if m.cfg.ListPoolsRet.err != nil {
		return nil, m.cfg.ListPoolsRet.err
	}
	return &mgmtpb.ListPoolsResp{Pools: MockPoolList, Status: m.cfg.ListPoolsRet.status}, nil
}

func (m *mockMgmtSvcClient) LeaderQuery(ctx context.Context, req *mgmtpb.LeaderQueryReq, _ ...grpc.CallOption) (*mgmtpb.LeaderQueryResp, error) {
	return &mgmtpb.LeaderQueryResp{}, nil
}

func (m *mockMgmtSvcClient) ListContainers(ctx context.Context, req *mgmtpb.ListContReq, o ...grpc.CallOption) (*mgmtpb.ListContResp, error) {
	// return successful list containers results
	return &mgmtpb.ListContResp{}, nil
}

type mockControlConfig struct {
	connectedState connectivity.State
	connectErr     error
}

// implement mock/stub behaviour for Control
type mockControl struct {
	address   string
	cfg       mockControlConfig
	ctlClient ctlpb.MgmtCtlClient
	svcClient mgmtpb.MgmtSvcClient
	log       logging.Logger
}

func (m *mockControl) connect(addr string, cfg *security.TransportConfig) error {
	if m.cfg.connectErr == nil {
		m.address = addr
	}

	return m.cfg.connectErr
}

func (m *mockControl) disconnect() error { return nil }

func (m *mockControl) connected() (connectivity.State, bool) {
	return m.cfg.connectedState, checkState(m.cfg.connectedState)
}

func (m *mockControl) getAddress() string { return m.address }

func (m *mockControl) getCtlClient() ctlpb.MgmtCtlClient {
	return m.ctlClient
}

func (m *mockControl) getSvcClient() mgmtpb.MgmtSvcClient {
	return m.svcClient
}

func (m *mockControl) logger() logging.Logger {
	return m.log
}

func newMockControl(
	log logging.Logger,
	address string, cfg mockControlConfig,
	cClient ctlpb.MgmtCtlClient, sClient mgmtpb.MgmtSvcClient) Control {

	return &mockControl{address, cfg, cClient, sClient, log}
}

type mockControllerFactory struct {
	log          logging.Logger
	controlCfg   mockControlConfig
	ctlClientCfg mockMgmtCtlClientConfig
	svcClientCfg mockMgmtSvcClientConfig
}

func (m *mockControllerFactory) create(address string, cfg *security.TransportConfig) (Control, error) {
	// returns controller with mock properties specified in constructor
	cClient := &mockMgmtCtlClient{cfg: m.ctlClientCfg}

	sClient := &mockMgmtSvcClient{cfg: m.svcClientCfg}

	controller := newMockControl(m.log, address, m.controlCfg, cClient, sClient)

	err := controller.connect(address, cfg)

	return controller, err
}

// TODO: switch everything over to using this
type mockConnectConfig struct {
	addresses     Addresses
	controlConfig mockControlConfig
	ctlClientCfg  mockMgmtCtlClientConfig
	svcClientCfg  mockMgmtSvcClientConfig
}

// newMockConnnectCfg is the config-based version of newMockConnect()
func newMockConnectCfg(log logging.Logger, cfg *mockConnectConfig) *connList {
	if cfg == nil {
		cfg = &mockConnectConfig{}
	}

	cl := &connList{
		log: log,
		factory: &mockControllerFactory{
			log:          log,
			controlCfg:   cfg.controlConfig,
			ctlClientCfg: cfg.ctlClientCfg,
			svcClientCfg: cfg.svcClientCfg,
		},
	}

	_ = cl.ConnectClients(cfg.addresses)

	return cl
}

func newMockConnect(log logging.Logger,
	state connectivity.State, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, pmems ScmNamespaces, mountResults ScmMountResults,
	scanRet error, formatRet error, killRet error, connectRet error, ACLRet *mockACLResult,
	listPoolsRet *mockListPoolsResult) *connList {

	return &connList{
		log: log,
		factory: &mockControllerFactory{
			log: log,
			controlCfg: mockControlConfig{
				connectedState: state,
				connectErr:     connectRet,
			},
			ctlClientCfg: mockMgmtCtlClientConfig{
				nvmeControllers:       ctrlrs,
				nvmeControllerResults: ctrlrResults,
				scmModules:            modules,
				scmModuleResults:      moduleResults,
				scmNamespaces:         pmems,
				scmMountResults:       mountResults,
				scanRet:               scanRet,
				formatRet:             formatRet,
			},
			svcClientCfg: mockMgmtSvcClientConfig{
				ACLRet:       ACLRet,
				ListPoolsRet: listPoolsRet,
				killErr:      killRet,
			},
		},
	}
}

func defaultMockConnect(log logging.Logger) Connect {
	return newMockConnect(
		log, connectivity.Ready, MockCtrlrs, MockCtrlrResults, MockScmModules,
		MockModuleResults, MockScmNamespaces, MockMountResults,
		nil, nil, nil, nil, MockACL, nil)
}

// MockScanResp mocks scan results from scm and nvme for multiple servers.
// Each result indicates success or failure through presence of Err.
func MockScanResp(cs NvmeControllers, ms ScmModules, nss ScmNamespaces, addrs Addresses) *StorageScanResp {
	nvmeResults := make(NvmeScanResults)
	scmResults := make(ScmScanResults)

	for _, addr := range addrs {
		nvmeResults[addr] = &NvmeScanResult{Ctrlrs: cs}

		scmResults[addr] = &ScmScanResult{
			Modules:    scmModulesFromPB(ms),
			Namespaces: scmNamespacesFromPB(nss),
		}
	}

	sort.Strings(addrs)

	return &StorageScanResp{Servers: addrs, Nvme: nvmeResults, Scm: scmResults}
}
