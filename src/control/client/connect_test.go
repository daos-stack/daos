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
	"os"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	. "google.golang.org/grpc/connectivity"
)

var (
	addresses    = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	features     = []*pb.Feature{MockFeaturePB()}
	ctrlrs       = NvmeControllers{MockControllerPB("")}
	ctrlrResults = NvmeControllerResults{
		&pb.NvmeControllerResult{
			Pciaddr: "0000:81:00.s",
			State: &pb.ResponseState{
				Status: pb.ResponseStatus_CTRL_ERR_APP,
				Error:  "example application error",
			},
		},
	}
	modules      = ScmModules{MockModulePB()}
	mountResults = ScmMountResults{
		&pb.ScmMountResult{
			Mntpoint: "/mnt/daos",
			State: &pb.ResponseState{
				Status: pb.ResponseStatus_CTRL_ERR_APP,
				Error:  "example application error",
			},
		},
	}
	errExample = errors.New("unknown failure")
)

func init() {
	log.NewDefaultLogger(log.Error, "connect_test: ", os.Stderr)
}

type mockControllerFactory struct {
	state        State
	features     []*pb.Feature
	ctrlrs       NvmeControllers
	ctrlrResults NvmeControllerResults
	modules      ScmModules
	mountResults ScmMountResults
	// to provide error injection into Control objects
	scanRet    error
	formatRet  error
	killRet    error
	connectRet error
}

func (m *mockControllerFactory) create(address string) (Control, error) {
	// returns controller with mock properties specified in constructor
	controller := newMockControl(
		address, m.state, m.features, m.ctrlrs, m.ctrlrResults,
		m.modules, m.mountResults,
		m.scanRet, m.formatRet, m.killRet, m.connectRet)

	err := controller.connect(address)

	return controller, err
}

func newMockConnect(
	state State, features []*pb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	mountResults ScmMountResults,
	scanRet error, formatRet error, killRet error,
	connectRet error) Connect {

	return &connList{
		factory: &mockControllerFactory{
			state, features, ctrlrs, ctrlrResults, modules,
			mountResults, scanRet, formatRet, killRet, connectRet,
		},
	}
}

func defaultMockConnect() Connect {
	return newMockConnect(
		Ready, features, ctrlrs, ctrlrResults, modules, mountResults,
		nil, nil, nil, nil)
}

func TestConnectClients(t *testing.T) {
	eMsg := "socket connection is not active (%s)"

	var conntests = []struct {
		addrsIn Addresses
		state   State
		connRet error
		errMsg  string
	}{
		{addresses, Idle, nil, ""},
		{addresses, Connecting, nil, fmt.Sprintf(eMsg, Connecting)},
		{addresses, Ready, nil, ""},
		{addresses, TransientFailure, nil, fmt.Sprintf(eMsg, TransientFailure)},
		{addresses, Shutdown, nil, fmt.Sprintf(eMsg, Shutdown)},
		{addresses, Idle, errExample, "unknown failure"},
		{addresses, Connecting, errExample, "unknown failure"},
		{addresses, Ready, errExample, "unknown failure"},
	}
	for _, tt := range conntests {
		cc := newMockConnect(
			tt.state, features, ctrlrs, ctrlrResults, modules, mountResults,
			nil, nil, nil, tt.connRet)

		results := cc.ConnectClients(tt.addrsIn)

		AssertEqual(
			t, len(results), len(tt.addrsIn), // assumes no duplicates
			"unexpected number of results")

		for _, res := range results {
			if tt.errMsg == "" {
				AssertEqual(
					t, res.Err, nil,
					"unexpected non-nil error value in results")
				continue
			}

			AssertEqual(
				t, res.Err.Error(), tt.errMsg,
				"unexpected error value in results")
		}
	}
}

func clientSetup(
	state State, features []*pb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	mountResults ScmMountResults,
	scanRet error, formatRet error, killRet error, connectRet error) Connect {

	cc := newMockConnect(
		state, features, ctrlrs, ctrlrResults, modules, mountResults,
		scanRet, formatRet, killRet, connectRet)

	_ = cc.ConnectClients(addresses)

	return cc
}

func defaultClientSetup() Connect {
	cc := defaultMockConnect()

	_ = cc.ConnectClients(addresses)

	return cc
}

func checkResults(t *testing.T, addrs Addresses, results ResultMap, e error) {
	AssertEqual(
		t, len(results), len(addrs), // duplicates ignored
		"unexpected number of results")

	for _, res := range results {
		AssertEqual(
			t, res.Err, e,
			"unexpected error value in results")
	}
}

func TestDuplicateConns(t *testing.T) {
	cc := defaultMockConnect()
	results := cc.ConnectClients(append(addresses, addresses...))

	checkResults(t, addresses, results, nil)
}

func TestGetClearConns(t *testing.T) {
	cc := defaultClientSetup()

	results := cc.GetActiveConns(ResultMap{})
	checkResults(t, addresses, results, nil)

	results = cc.ClearConns()
	checkResults(t, addresses, results, nil)

	results = cc.GetActiveConns(ResultMap{})
	AssertEqual(t, results, ResultMap{}, "unexpected result map")

	results = cc.ConnectClients(addresses)
	checkResults(t, addresses, results, nil)

	results = cc.GetActiveConns(results)
	checkResults(t, addresses, results, nil)
}

func TestListFeatures(t *testing.T) {
	cc := defaultClientSetup()

	clientFeatures := cc.ListFeatures()

	AssertEqual(
		t, clientFeatures, NewClientFM(features, addresses),
		"unexpected client features returned")
}

func TestScanStorage(t *testing.T) {
	cc := defaultClientSetup()

	clientNvme, clientScm := cc.ScanStorage()

	AssertEqual(
		t, clientNvme, NewClientNvme(ctrlrs, addresses),
		"unexpected client NVMe SSD controllers returned")

	AssertEqual(
		t, clientScm, NewClientScm(modules, addresses),
		"unexpected client SCM modules returned")
}

func TestFormatStorage(t *testing.T) {
	tests := []struct {
		formatRet error
	}{
		{
			nil,
		},
		{
			errExample,
		},
	}

	for _, tt := range tests {
		cc := clientSetup(
			Ready, features, ctrlrs, ctrlrResults, modules,
			mountResults, nil, tt.formatRet, nil, nil)

		cNvmeMap, cMountMap := cc.FormatStorage()

		if tt.formatRet != nil {
			for _, addr := range addresses {
				AssertEqual(
					t, cNvmeMap[addr],
					NvmeResult{Err: tt.formatRet},
					"unexpected error for nvme result")
				AssertEqual(
					t, cMountMap[addr],
					MountResult{Err: tt.formatRet},
					"unexpected error for scm mount result")
			}
			continue
		}

		AssertEqual(
			t, cNvmeMap, NewClientNvmeResults(
				ctrlrResults,
				addresses),
			"unexpected client NVMe SSD controller results returned")

		AssertEqual(
			t, cMountMap, NewClientMountResults(
				mountResults,
				addresses),
			"unexpected client SCM Mount results returned")
	}
}

func TestKillRank(t *testing.T) {
	tests := []struct {
		killRet error
	}{
		{
			nil,
		},
		{
			errExample,
		},
	}

	for _, tt := range tests {
		cc := clientSetup(
			Ready, features, ctrlrs, ctrlrResults, modules,
			mountResults, nil, nil, tt.killRet, nil)

		resultMap := cc.KillRank("acd", 0)

		checkResults(t, addresses, resultMap, tt.killRet)
	}
}
