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

package client

import (
	"fmt"
	"io"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	grpc "google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"
)

var (
	addresses    = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	features     = []*pb.Feature{common.MockFeaturePB()}
	ctrlrs       = NvmeControllers{common.MockControllerPB("")}
	exampleState = pb.ResponseState{
		Status: pb.ResponseStatus_CTRL_ERR_APP,
		Error:  "example application error",
	}
	ctrlrResults = NvmeControllerResults{
		&pb.NvmeControllerResult{
			Pciaddr: "0000:81:00.0",
			State:   &exampleState,
		},
	}
	modules       = ScmModules{common.MockModulePB()}
	moduleResults = ScmModuleResults{
		&pb.ScmModuleResult{
			Loc:   &pb.ScmModule_Location{},
			State: &exampleState,
		},
	}
	mountResults = ScmMountResults{
		&pb.ScmMountResult{
			Mntpoint: "/mnt/daos",
			State:    &exampleState,
		},
	}
	errExample = errors.New("unknown failure")
)

type mgmtCtlListFeaturesClient struct {
	grpc.ClientStream
	features      []*pb.Feature
	alreadyCalled bool
}

func (m *mgmtCtlListFeaturesClient) Recv() (*pb.Feature, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	// TODO: expand to return multiple features in stream
	return m.features[0], nil
}

type mgmtCtlFormatStorageClient struct {
	grpc.ClientStream
	ctrlrResults  NvmeControllerResults
	mountResults  ScmMountResults
	alreadyCalled bool
}

func (m *mgmtCtlFormatStorageClient) Recv() (*pb.FormatStorageResp, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &pb.FormatStorageResp{
		Crets: m.ctrlrResults,
		Mrets: m.mountResults,
	}, nil
}

type mgmtCtlUpdateStorageClient struct {
	grpc.ClientStream
	ctrlrResults  NvmeControllerResults
	moduleResults ScmModuleResults
	alreadyCalled bool
}

func (m *mgmtCtlUpdateStorageClient) Recv() (*pb.UpdateStorageResp, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &pb.UpdateStorageResp{
		Crets: m.ctrlrResults,
		Mrets: m.moduleResults,
	}, nil
}

type mgmtCtlBurninStorageClient struct {
	grpc.ClientStream
	ctrlrResults  NvmeControllerResults
	mountResults  ScmMountResults
	alreadyCalled bool
}

func (m *mgmtCtlBurninStorageClient) Recv() (*pb.BurninStorageResp, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &pb.BurninStorageResp{
		Crets: m.ctrlrResults,
		Mrets: m.mountResults,
	}, nil
}

type mgmtCtlFetchFioConfigPathsClient struct {
	grpc.ClientStream
	alreadyCalled bool
}

func (m *mgmtCtlFetchFioConfigPathsClient) Recv() (*pb.FilePath, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &pb.FilePath{Path: "/tmp/fioconf.test.example"}, nil
}

type mockMgmtCtlClient struct {
	features      []*pb.Feature
	ctrlrs        NvmeControllers
	ctrlrResults  NvmeControllerResults
	modules       ScmModules
	moduleResults ScmModuleResults
	mountResults  ScmMountResults
	scanRet       error
	formatRet     error
	updateRet     error
	burninRet     error
	killRet       error
}

func (m *mockMgmtCtlClient) ListFeatures(
	ctx context.Context, req *pb.EmptyReq, o ...grpc.CallOption) (
	pb.MgmtCtl_ListFeaturesClient, error) {

	return &mgmtCtlListFeaturesClient{features: m.features}, nil
}

func (m *mockMgmtCtlClient) ScanStorage(
	ctx context.Context, req *pb.ScanStorageReq, o ...grpc.CallOption) (
	*pb.ScanStorageResp, error) {
	// return successful query results, state member messages
	// initialise with zero values indicating mgmt.CTRL_SUCCESS
	return &pb.ScanStorageResp{
		Ctrlrs:  m.ctrlrs,
		Modules: m.modules,
	}, m.scanRet
}

func (m *mockMgmtCtlClient) FormatStorage(
	ctx context.Context, req *pb.FormatStorageReq, o ...grpc.CallOption) (
	pb.MgmtCtl_FormatStorageClient, error) {

	return &mgmtCtlFormatStorageClient{
		ctrlrResults: m.ctrlrResults, mountResults: m.mountResults,
	}, m.formatRet
}

func (m *mockMgmtCtlClient) UpdateStorage(
	ctx context.Context, req *pb.UpdateStorageReq, o ...grpc.CallOption) (
	pb.MgmtCtl_UpdateStorageClient, error) {

	return &mgmtCtlUpdateStorageClient{
		ctrlrResults: m.ctrlrResults, moduleResults: m.moduleResults,
	}, m.updateRet
}

func (m *mockMgmtCtlClient) BurninStorage(
	ctx context.Context, req *pb.BurninStorageReq, o ...grpc.CallOption) (
	pb.MgmtCtl_BurninStorageClient, error) {

	return &mgmtCtlBurninStorageClient{
		ctrlrResults: m.ctrlrResults, mountResults: m.mountResults,
	}, m.burninRet
}

func (m *mockMgmtCtlClient) FetchFioConfigPaths(
	ctx context.Context, req *pb.EmptyReq, o ...grpc.CallOption) (
	pb.MgmtCtl_FetchFioConfigPathsClient, error) {

	return &mgmtCtlFetchFioConfigPathsClient{}, nil
}

func (m *mockMgmtCtlClient) KillRank(
	ctx context.Context, req *pb.DaosRank, o ...grpc.CallOption) (
	*pb.DaosResponse, error) {

	return &pb.DaosResponse{}, m.killRet
}

func newMockMgmtCtlClient(
	features []*pb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, mountResults ScmMountResults,
	scanRet error, formatRet error, updateRet error, burninRet error,
	killRet error) pb.MgmtCtlClient {

	return &mockMgmtCtlClient{
		features, ctrlrs, ctrlrResults, modules, moduleResults,
		mountResults, scanRet, formatRet, updateRet, burninRet, killRet,
	}
}

type mockMgmtSvcClient struct{}

func (m *mockMgmtSvcClient) CreatePool(
	ctx context.Context, req *pb.CreatePoolReq, o ...grpc.CallOption) (
	*pb.CreatePoolResp, error) {

	// return successful pool creation results
	// initialise with zero values indicating mgmt.CTRL_SUCCESS
	return &pb.CreatePoolResp{}, nil
}

func (m *mockMgmtSvcClient) Join(
	ctx context.Context, req *pb.JoinReq, o ...grpc.CallOption) (
	*pb.JoinResp, error) {

	return &pb.JoinResp{}, nil
}

func newMockMgmtSvcClient() pb.MgmtSvcClient {
	return &mockMgmtSvcClient{}
}

// implement mock/stub behaviour for Control
type mockControl struct {
	address    string
	connState  connectivity.State
	connectRet error
	ctlClient  pb.MgmtCtlClient
	svcClient  pb.MgmtSvcClient
}

func (m *mockControl) connect(addr string) error {
	if m.connectRet == nil {
		m.address = addr
	}

	return m.connectRet
}

func (m *mockControl) disconnect() error { return nil }

func (m *mockControl) connected() (connectivity.State, bool) {
	return m.connState, checkState(m.connState)
}

func (m *mockControl) getAddress() string { return m.address }

func (m *mockControl) getCtlClient() pb.MgmtCtlClient {
	return m.ctlClient
}

func (m *mockControl) getSvcClient() pb.MgmtSvcClient {
	return m.svcClient
}

func newMockControl(
	address string, state connectivity.State, connectRet error,
	cClient pb.MgmtCtlClient, sClient pb.MgmtSvcClient) Control {

	return &mockControl{address, state, connectRet, cClient, sClient}
}

type mockControllerFactory struct {
	state         connectivity.State
	features      []*pb.Feature
	ctrlrs        NvmeControllers
	ctrlrResults  NvmeControllerResults
	modules       ScmModules
	moduleResults ScmModuleResults
	mountResults  ScmMountResults
	// to provide error injection into Control objects
	scanRet    error
	formatRet  error
	updateRet  error
	burninRet  error
	killRet    error
	connectRet error
}

func (m *mockControllerFactory) create(address string) (Control, error) {
	// returns controller with mock properties specified in constructor
	cClient := newMockMgmtCtlClient(
		m.features, m.ctrlrs, m.ctrlrResults,
		m.modules, m.moduleResults, m.mountResults,
		m.scanRet, m.formatRet, m.updateRet, m.burninRet, m.killRet)

	sClient := newMockMgmtSvcClient()

	controller := newMockControl(
		address, m.state, m.connectRet, cClient, sClient)

	err := controller.connect(address)

	return controller, err
}

func newMockConnect(
	state connectivity.State, features []*pb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, mountResults ScmMountResults,
	scanRet error, formatRet error, updateRet error, burninRet error,
	killRet error, connectRet error) Connect {

	return &connList{
		factory: &mockControllerFactory{
			state, features, ctrlrs, ctrlrResults, modules,
			moduleResults, mountResults, scanRet, formatRet,
			updateRet, burninRet, killRet, connectRet,
		},
	}
}

func defaultMockConnect() Connect {
	return newMockConnect(
		connectivity.Ready, features, ctrlrs, ctrlrResults, modules,
		moduleResults, mountResults, nil, nil, nil, nil, nil, nil)
}

// NewClientFM provides a mock ClientFeatureMap for testing.
func NewClientFM(features []*pb.Feature, addrs Addresses) ClientFeatureMap {
	cf := make(ClientFeatureMap)
	for _, addr := range addrs {
		fMap := make(FeatureMap)
		for _, f := range features {
			fMap[f.Fname.Name] = fmt.Sprintf(
				"category %s, %s", f.Category.Category, f.Description)
		}
		cf[addr] = FeatureResult{fMap, nil}
	}
	return cf
}

// NewClientNvmeResults provides a mock ClientCtrlrMap populated with controller
// operation responses
func NewClientNvmeResults(
	results []*pb.NvmeControllerResult, addrs Addresses) ClientCtrlrMap {

	cMap := make(ClientCtrlrMap)
	for _, addr := range addrs {
		cMap[addr] = CtrlrResults{Responses: results}
	}
	return cMap
}

// NewClientNvme provides a mock ClientCtrlrMap populated with ctrlr details
func NewClientNvme(ctrlrs NvmeControllers, addrs Addresses) ClientCtrlrMap {
	cMap := make(ClientCtrlrMap)
	for _, addr := range addrs {
		cMap[addr] = CtrlrResults{Ctrlrs: ctrlrs}
	}
	return cMap
}

// NewClientScm provides a mock ClientModuleMap populated with scm module details
func NewClientScm(mms ScmModules, addrs Addresses) ClientModuleMap {
	cMap := make(ClientModuleMap)
	for _, addr := range addrs {
		cMap[addr] = ModuleResults{Modules: mms}
	}
	return cMap
}

// NewClientMountResults provides a mock ClientMountMap populated with scm mount
// operation responses
func NewClientMountResults(
	results []*pb.ScmMountResult, addrs Addresses) ClientMountMap {

	cMap := make(ClientMountMap)
	for _, addr := range addrs {
		cMap[addr] = MountResults{Responses: results}
	}
	return cMap
}

// NewClientModuleResults provides a mock ClientModuleMap populated with scm
// module operation responses
func NewClientModuleResults(
	results []*pb.ScmModuleResult, addrs Addresses) ClientModuleMap {

	cMap := make(ClientModuleMap)
	for _, addr := range addrs {
		cMap[addr] = ModuleResults{Responses: results}
	}
	return cMap
}
