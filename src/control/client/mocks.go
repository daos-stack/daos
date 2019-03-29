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

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"google.golang.org/grpc/connectivity"
)

// implement mock/stub behaviour for Control
type mockControl struct {
	address    string
	connState  connectivity.State
	features   []*pb.Feature
	ctrlrs     NvmeControllers
	modules    ScmModules
	formatRet  error
	killRet    error
	connectRet error
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
func (m *mockControl) listNvmeCtrlrs() (NvmeControllers, error) {
	return m.ctrlrs, nil
}
func (m *mockControl) listScmModules() (ScmModules, error) {
	return m.modules, nil
}
func (m *mockControl) formatStorage() error {
	return m.formatRet
}
func (m *mockControl) killRank(uuid string, rank uint32) error {
	return m.killRet
}

func newMockControl(
	address string, state connectivity.State, features []*pb.Feature,
	ctrlrs NvmeControllers, modules ScmModules,
	formatRet error, killRet error, connectRet error) Control {

	return &mockControl{
		address, state, features, ctrlrs, modules,
		formatRet, killRet, connectRet,
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

// NewClientNvme provides a mock ClientNvmeMap for testing.
func NewClientNvme(ctrlrs NvmeControllers, addrs Addresses) ClientNvmeMap {
	cMap := make(ClientNvmeMap)
	for _, addr := range addrs {
		cMap[addr] = NvmeResult{ctrlrs, nil}
	}
	return cMap
}

// NewClientScm provides a mock ClientScmMap for testing.
func NewClientScm(mms ScmModules, addrs Addresses) ClientScmMap {
	cMap := make(ClientScmMap)
	for _, addr := range addrs {
		cMap[addr] = ScmResult{mms, nil}
	}
	return cMap
}
