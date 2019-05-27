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

package client

import (
	"fmt"
	"io"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"golang.org/x/net/context"
	grpc "google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"
)

type mgmtControlFormatStorageClient struct {
	grpc.ClientStream
	ctrlrResults  NvmeControllerResults
	mountResults  ScmMountResults
	alreadyCalled bool
}

func (m mgmtControlFormatStorageClient) Recv() (*pb.FormatStorageResp, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &pb.FormatStorageResp{
		Crets: m.ctrlrResults,
		Mrets: m.mountResults,
	}, nil
}

type mgmtControlUpdateStorageClient struct {
	grpc.ClientStream
	ctrlrResults  NvmeControllerResults
	moduleResults ScmModuleResults
	alreadyCalled bool
}

func (m mgmtControlUpdateStorageClient) Recv() (*pb.UpdateStorageResp, error) {
	if m.alreadyCalled {
		return nil, io.EOF
	}
	m.alreadyCalled = true

	return &pb.UpdateStorageResp{
		Crets: m.ctrlrResults,
		Mrets: m.moduleResults,
	}, nil
}

// implement mock/stub behaviour for Control
type mockControl struct {
	address       string
	connState     connectivity.State
	features      []*pb.Feature
	ctrlrs        NvmeControllers
	ctrlrResults  NvmeControllerResults
	modules       ScmModules
	moduleResults ScmModuleResults
	mountResults  ScmMountResults
	scanRet       error
	formatRet     error
	updateRet     error
	killRet       error
	connectRet    error
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
func (m *mockControl) listAllFeatures() (FeatureMap, error) {
	fm := make(FeatureMap)
	for _, f := range m.features {
		fm[f.Fname.Name] = fmt.Sprintf(
			"category %s, %s", f.Category.Category, f.Description)
	}
	return fm, nil
}
func (m *mockControl) scanStorage() (*pb.ScanStorageResp, error) {
	// return successful query results, state member messages
	// initialise with zero values indicating mgmt.CTRL_SUCCESS
	return &pb.ScanStorageResp{
		Ctrlrs:  m.ctrlrs,
		Modules: m.modules,
	}, m.scanRet
}
func (m *mockControl) formatStorage(ctx context.Context) (
	pb.MgmtControl_FormatStorageClient, error) {

	return mgmtControlFormatStorageClient{
		ctrlrResults: m.ctrlrResults, mountResults: m.mountResults,
	}, m.formatRet
}
func (m *mockControl) updateStorage(
	ctx context.Context, params *pb.UpdateStorageParams) (
	pb.MgmtControl_UpdateStorageClient, error) {

	return mgmtControlUpdateStorageClient{
		ctrlrResults: m.ctrlrResults, moduleResults: m.moduleResults,
	}, m.updateRet
}
func (m *mockControl) killRank(uuid string, rank uint32) error {
	return m.killRet
}
func newMockControl(
	address string, state connectivity.State, features []*pb.Feature,
	ctrlrs NvmeControllers, ctrlrResults NvmeControllerResults,
	modules ScmModules, moduleResults ScmModuleResults,
	mountResults ScmMountResults, scanRet error, formatRet error,
	updateRet error, killRet error, connectRet error) Control {

	return &mockControl{
		address, state, features, ctrlrs, ctrlrResults, modules,
		moduleResults, mountResults, scanRet, formatRet, updateRet,
		killRet, connectRet,
	}
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
