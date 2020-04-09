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

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	grpc "google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

var (
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
			Physicalid: 1234,
			State:      &MockState,
		},
	}
	MockScmNamespaces = ScmNamespaces{MockScmNamespace()}
	MockMounts        = ScmMounts{MockScmMount()}
	MockMountResults  = ScmMountResults{
		&ctlpb.ScmMountResult{
			Mntpoint: "/mnt/daos",
			State:    &MockState,
		},
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
			State:      &MockSuccessState,
			Modules:    m.cfg.scmModules,
			Namespaces: m.cfg.scmNamespaces,
		},
	}, m.cfg.scanRet
}

func (m *mockMgmtCtlClient) StorageFormat(ctx context.Context, req *ctlpb.StorageFormatReq, o ...grpc.CallOption) (*ctlpb.StorageFormatResp, error) {
	return nil, m.cfg.formatRet
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
	svcClientCfg MockMgmtSvcClientConfig
}

func (m *mockControllerFactory) create(address string, cfg *security.TransportConfig) (Control, error) {
	// returns controller with mock properties specified in constructor
	cClient := &mockMgmtCtlClient{cfg: m.ctlClientCfg}

	sClient := &MockMgmtSvcClient{Cfg: m.svcClientCfg}

	controller := newMockControl(m.log, address, m.controlCfg, cClient, sClient)

	err := controller.connect(address, cfg)

	return controller, err
}

// TODO: switch everything over to using this
type mockConnectConfig struct {
	addresses     Addresses
	controlConfig mockControlConfig
	ctlClientCfg  mockMgmtCtlClientConfig
	svcClientCfg  MockMgmtSvcClientConfig
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
	scanRet error, formatRet error, killRet error, connectRet error,
	listPoolsRet *common.MockListPoolsResult) *connList {

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
			svcClientCfg: MockMgmtSvcClientConfig{
				ListPoolsRet: listPoolsRet,
				KillErr:      killRet,
			},
		},
	}
}

func defaultMockConnect(log logging.Logger) Connect {
	return newMockConnect(
		log, connectivity.Ready, MockCtrlrs, MockCtrlrResults, MockScmModules,
		MockModuleResults, MockScmNamespaces, MockMountResults,
		nil, nil, nil, nil, nil)
}
