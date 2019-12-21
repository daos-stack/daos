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

package security

import (
	"testing"
)

func TestCommonNameToComponent(t *testing.T) {
	testCases := []struct {
		testname   string
		commonname string
		expected   Component
	}{
		{"AdminCN", "admin", ComponentAdmin},
		{"AgentCN", "agent", ComponentAgent},
		{"ServerCN", "server", ComponentServer},
		{"UnknownCN", "knownbadvalue", ComponentUndefined},
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			comp := CommonNameToComponent(tc.commonname)
			if comp != tc.expected {
				t.Errorf("result %s; expected %s", comp.String(), tc.expected.String())
			}
		})
	}
}

func TestHasAccess(t *testing.T) {
	var negativeCases = 1
	testCases := []struct {
		testname string
		comp     Component
		method   string
		expected bool
	}{
		{"/ctl.MgmtCtl/StoragePrepare", ComponentAdmin, "/ctl.MgmtCtl/StoragePrepare", true},
		{"/ctl.MgmtCtl/StorageScan", ComponentAdmin, "/ctl.MgmtCtl/StorageScan", true},
		{"/ctl.MgmtCtl/SystemMemberQuery", ComponentAdmin, "/ctl.MgmtCtl/SystemMemberQuery", true},
		{"/ctl.MgmtCtl/SystemStop", ComponentAdmin, "/ctl.MgmtCtl/SystemStop", true},
		{"/ctl.MgmtCtl/NetworkListProviders", ComponentAdmin, "/ctl.MgmtCtl/NetworkListProviders", true},
		{"/ctl.MgmtCtl/StorageFormat", ComponentAdmin, "/ctl.MgmtCtl/StorageFormat", true},
		{"/ctl.MgmtCtl/NetworkScanDevices", ComponentAdmin, "/ctl.MgmtCtl/NetworkScanDevices", true},
		{"/mgmt.MgmtSvc/Join", ComponentServer, "/mgmt.MgmtSvc/Join", true},
		{"/mgmt.MgmtSvc/PoolCreate", ComponentAdmin, "/mgmt.MgmtSvc/PoolCreate", true},
		{"/mgmt.MgmtSvc/PoolDestroy", ComponentAdmin, "/mgmt.MgmtSvc/PoolDestroy", true},
		{"/mgmt.MgmtSvc/PoolGetACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolGetACL", true},
		{"/mgmt.MgmtSvc/PoolOverwriteACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolOverwriteACL", true},
		{"/mgmt.MgmtSvc/GetAttachInfo", ComponentAgent, "/mgmt.MgmtSvc/GetAttachInfo", true},
		{"/mgmt.MgmtSvc/BioHealthQuery", ComponentAdmin, "/mgmt.MgmtSvc/BioHealthQuery", true},
		{"/mgmt.MgmtSvc/SmdListDevs", ComponentAdmin, "/mgmt.MgmtSvc/SmdListDevs", true},
		{"/mgmt.MgmtSvc/SmdListPools", ComponentAdmin, "/mgmt.MgmtSvc/SmdListPools", true},
		{"/mgmt.MgmtSvc/KillRank", ComponentAdmin, "/mgmt.MgmtSvc/KillRank", true},
		{"/mgmt.MgmtSvc/ListPools", ComponentAdmin, "/mgmt.MgmtSvc/ListPools", true},
		{"WrongComponent", ComponentAdmin, "/mgmt.MgmtSvc/Join", false},
	}

	if len(testCases) != (len(methodAuthorizations) + negativeCases) {
		t.Errorf("component access tests is missing a case for a newly added method")
	}
	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			result := tc.comp.HasAccess(tc.method)
			if result != tc.expected {
				t.Errorf("result %t; expected %t", result, tc.expected)
			}
		})
	}
}
