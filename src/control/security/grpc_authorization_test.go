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
	"fmt"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
)

func TestCommonNameToComponent(t *testing.T) {
	testCases := []struct {
		testname   string
		commonname string
		expected   Component
	}{
		{"AdminCN", "admin", ComponentAdmin},
		{"AdminPrefix", "administrator", ComponentUndefined},
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
	allComponents := [4]Component{ComponentUndefined, ComponentAdmin, ComponentAgent, ComponentServer}
	testCases := []struct {
		testname         string
		correctComponent Component
		method           string
	}{
		{"StoragePrepare", ComponentAdmin, "/ctl.MgmtCtl/StoragePrepare"},
		{"StorageScan", ComponentAdmin, "/ctl.MgmtCtl/StorageScan"},
		{"SystemMemberQuery", ComponentAdmin, "/ctl.MgmtCtl/SystemMemberQuery"},
		{"SystemStop", ComponentAdmin, "/ctl.MgmtCtl/SystemStop"},
		{"NetworkListProviders", ComponentAdmin, "/ctl.MgmtCtl/NetworkListProviders"},
		{"StorageFormat", ComponentAdmin, "/ctl.MgmtCtl/StorageFormat"},
		{"NetworkScanDevices", ComponentAdmin, "/ctl.MgmtCtl/NetworkScanDevices"},
		{"Join", ComponentServer, "/mgmt.MgmtSvc/Join"},
		{"PoolCreate", ComponentAdmin, "/mgmt.MgmtSvc/PoolCreate"},
		{"PoolDestroy", ComponentAdmin, "/mgmt.MgmtSvc/PoolDestroy"},
		{"PoolGetACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolGetACL"},
		{"PoolOverwriteACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolOverwriteACL"},
		{"GetAttachInfo", ComponentAgent, "/mgmt.MgmtSvc/GetAttachInfo"},
		{"BioHealthQuery", ComponentAdmin, "/mgmt.MgmtSvc/BioHealthQuery"},
		{"SmdListDevs", ComponentAdmin, "/mgmt.MgmtSvc/SmdListDevs"},
		{"SmdListPools", ComponentAdmin, "/mgmt.MgmtSvc/SmdListPools"},
		{"KillRank", ComponentAdmin, "/mgmt.MgmtSvc/KillRank"},
		{"ListPools", ComponentAdmin, "/mgmt.MgmtSvc/ListPools"},
	}

	if len(testCases) != len(methodAuthorizations) {
		t.Errorf("component access tests is missing a case for a newly added method")
	}
	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			for _, comp := range allComponents {
				if comp == tc.correctComponent {
					common.AssertTrue(t, comp.HasAccess(tc.method), fmt.Sprintf("%s should have access to %s but does not", comp, tc.method))
				} else {
					common.AssertFalse(t, comp.HasAccess(tc.method), fmt.Sprintf("%s should not have access to %s but does", comp, tc.method))
				}
			}
		})
	}
}
