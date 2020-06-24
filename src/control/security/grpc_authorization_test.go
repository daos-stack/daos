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
		{"StorageFormat", ComponentAdmin, "/ctl.MgmtCtl/StorageFormat"},
		{"SystemQuery", ComponentAdmin, "/ctl.MgmtCtl/SystemQuery"},
		{"SystemStop", ComponentAdmin, "/ctl.MgmtCtl/SystemStop"},
		{"SystemResetFormat", ComponentAdmin, "/ctl.MgmtCtl/SystemResetFormat"},
		{"SystemStart", ComponentAdmin, "/ctl.MgmtCtl/SystemStart"},
		{"NetworkScan", ComponentAdmin, "/ctl.MgmtCtl/NetworkScan"},
		{"Join", ComponentServer, "/mgmt.MgmtSvc/Join"},
		{"LeaderQuery", ComponentAdmin, "/mgmt.MgmtSvc/LeaderQuery"},
		{"PoolCreate", ComponentAdmin, "/mgmt.MgmtSvc/PoolCreate"},
		{"PoolDestroy", ComponentAdmin, "/mgmt.MgmtSvc/PoolDestroy"},
		{"PoolQuery", ComponentAdmin, "/mgmt.MgmtSvc/PoolQuery"},
		{"PoolSetProp", ComponentAdmin, "/mgmt.MgmtSvc/PoolSetProp"},
		{"PoolGetACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolGetACL"},
		{"PoolOverwriteACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolOverwriteACL"},
		{"PoolUpdateACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolUpdateACL"},
		{"PoolDeleteACL", ComponentAdmin, "/mgmt.MgmtSvc/PoolDeleteACL"},
		{"PoolExclude", ComponentAdmin, "/mgmt.MgmtSvc/PoolExclude"},
		{"PoolReintegrate", ComponentAdmin, "/mgmt.MgmtSvc/PoolReintegrate"},
		{"PoolEvict", ComponentAdmin, "/mgmt.MgmtSvc/PoolEvict"},
		{"PoolExtend", ComponentAdmin, "/mgmt.MgmtSvc/PoolExtend"},
		{"GetAttachInfo", ComponentAgent, "/mgmt.MgmtSvc/GetAttachInfo"},
		{"BioHealthQuery", ComponentAdmin, "/mgmt.MgmtSvc/BioHealthQuery"},
		{"SmdListDevs", ComponentAdmin, "/mgmt.MgmtSvc/SmdListDevs"},
		{"SmdListPools", ComponentAdmin, "/mgmt.MgmtSvc/SmdListPools"},
		{"ListPools", ComponentAdmin, "/mgmt.MgmtSvc/ListPools"},
		{"DevStateQuery", ComponentAdmin, "/mgmt.MgmtSvc/DevStateQuery"},
		{"StorageSetFaulty", ComponentAdmin, "/mgmt.MgmtSvc/StorageSetFaulty"},
		{"ListContainers", ComponentAdmin, "/mgmt.MgmtSvc/ListContainers"},
		{"PrepShutdownRanks", ComponentServer, "/mgmt.MgmtSvc/PrepShutdownRanks"},
		{"StopRanks", ComponentServer, "/mgmt.MgmtSvc/StopRanks"},
		{"PingRanks", ComponentServer, "/mgmt.MgmtSvc/PingRanks"},
		{"ResetFormatRanks", ComponentServer, "/mgmt.MgmtSvc/ResetFormatRanks"},
		{"StartRanks", ComponentServer, "/mgmt.MgmtSvc/StartRanks"},
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
