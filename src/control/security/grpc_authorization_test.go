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

package security

import (
	"fmt"
	"reflect"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestSecurity_CommonNameToComponent(t *testing.T) {
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

func TestSecurity_ComponentHasAccess(t *testing.T) {
	allComponents := []Component{ComponentUndefined, ComponentAdmin, ComponentAgent, ComponentServer}
	testCases := map[string]Component{
		"/ctl.MgmtCtl/StoragePrepare":     ComponentAdmin,
		"/ctl.MgmtCtl/StorageScan":        ComponentAdmin,
		"/ctl.MgmtCtl/StorageFormat":      ComponentAdmin,
		"/ctl.MgmtCtl/SystemQuery":        ComponentAdmin,
		"/ctl.MgmtCtl/SystemStop":         ComponentAdmin,
		"/ctl.MgmtCtl/SystemResetFormat":  ComponentAdmin,
		"/ctl.MgmtCtl/SystemStart":        ComponentAdmin,
		"/ctl.MgmtCtl/NetworkScan":        ComponentAdmin,
		"/ctl.MgmtCtl/FirmwareQuery":      ComponentAdmin,
		"/ctl.MgmtCtl/FirmwareUpdate":     ComponentAdmin,
		"/mgmt.MgmtSvc/Join":              ComponentServer,
		"/mgmt.MgmtSvc/LeaderQuery":       ComponentAdmin,
		"/mgmt.MgmtSvc/PoolCreate":        ComponentAdmin,
		"/mgmt.MgmtSvc/PoolDestroy":       ComponentAdmin,
		"/mgmt.MgmtSvc/PoolResolveID":     ComponentAdmin,
		"/mgmt.MgmtSvc/PoolQuery":         ComponentAdmin,
		"/mgmt.MgmtSvc/PoolSetProp":       ComponentAdmin,
		"/mgmt.MgmtSvc/PoolGetACL":        ComponentAdmin,
		"/mgmt.MgmtSvc/PoolOverwriteACL":  ComponentAdmin,
		"/mgmt.MgmtSvc/PoolUpdateACL":     ComponentAdmin,
		"/mgmt.MgmtSvc/PoolDeleteACL":     ComponentAdmin,
		"/mgmt.MgmtSvc/PoolExclude":       ComponentAdmin,
		"/mgmt.MgmtSvc/PoolDrain":         ComponentAdmin,
		"/mgmt.MgmtSvc/PoolReintegrate":   ComponentAdmin,
		"/mgmt.MgmtSvc/PoolEvict":         ComponentAdmin,
		"/mgmt.MgmtSvc/PoolExtend":        ComponentAdmin,
		"/mgmt.MgmtSvc/GetAttachInfo":     ComponentAgent,
		"/mgmt.MgmtSvc/SmdQuery":          ComponentAdmin,
		"/mgmt.MgmtSvc/ListPools":         ComponentAdmin,
		"/mgmt.MgmtSvc/ListContainers":    ComponentAdmin,
		"/mgmt.MgmtSvc/ContSetOwner":      ComponentAdmin,
		"/mgmt.MgmtSvc/PrepShutdownRanks": ComponentServer,
		"/mgmt.MgmtSvc/StopRanks":         ComponentServer,
		"/mgmt.MgmtSvc/PingRanks":         ComponentServer,
		"/mgmt.MgmtSvc/ResetFormatRanks":  ComponentServer,
		"/mgmt.MgmtSvc/StartRanks":        ComponentServer,
	}

	var missing []string
	for method := range methodAuthorizations {
		if _, found := testCases[method]; !found {
			missing = append(missing, method)
		}
	}
	if len(missing) > 0 {
		t.Fatalf("%s is missing test cases for the following methods:\n%s", t.Name(), strings.Join(missing, "\n"))
	}

	var invalid []string
	for testMethod := range testCases {
		if _, found := methodAuthorizations[testMethod]; !found {
			invalid = append(invalid, testMethod)
		}
	}
	if len(invalid) > 0 {
		t.Fatalf("%s has test cases for the following methods that are not in methodAuthorizations:\n%s", t.Name(), strings.Join(invalid, "\n"))
	}

	for method, correctComponent := range testCases {
		methodName := strings.SplitAfterN(method, "/", 3)[2]
		t.Run(methodName, func(t *testing.T) {
			for _, comp := range allComponents {
				if comp == correctComponent {
					common.AssertTrue(t, comp.HasAccess(method), fmt.Sprintf("%s should have access to %s but does not", comp, methodName))
				} else {
					common.AssertFalse(t, comp.HasAccess(method), fmt.Sprintf("%s should not have access to %s but does", comp, methodName))
				}
			}
		})
	}
}

func TestSecurity_AllRpcsAreAuthorized(t *testing.T) {
	for name, tc := range map[string]struct {
		service interface{}
		prefix  string
	}{
		"mgmt rpcs": {
			service: (*mgmtpb.MgmtSvcServer)(nil),
			prefix:  "/mgmt.MgmtSvc/",
		},
		"ctl rpcs": {
			service: (*ctlpb.MgmtCtlServer)(nil),
			prefix:  "/ctl.MgmtCtl/",
		},
	} {
		t.Run(name, func(t *testing.T) {
			svcType := reflect.TypeOf(tc.service).Elem()

			var rpcNames []string
			for i := 0; i < svcType.NumMethod(); i++ {
				fullName := tc.prefix + svcType.Method(i).Name
				rpcNames = append(rpcNames, fullName)
			}

			var missing []string
			for _, rpcName := range rpcNames {
				if _, ok := methodAuthorizations[rpcName]; !ok {
					missing = append(missing, rpcName)
				}
			}

			if len(missing) > 0 {
				t.Fatalf("unauthorized RPCs (add to methodAuthorizations):\n%s", strings.Join(missing, "\n"))
			}
		})
	}
}

func TestSecurity_AuthorizedRpcsAreValid(t *testing.T) {
	for name, tc := range map[string]struct {
		service interface{}
		prefix  string
	}{
		"mgmt rpcs": {
			service: (*mgmtpb.MgmtSvcServer)(nil),
			prefix:  "/mgmt.MgmtSvc/",
		},
		"ctl rpcs": {
			service: (*ctlpb.MgmtCtlServer)(nil),
			prefix:  "/ctl.MgmtCtl/",
		},
	} {
		t.Run(name, func(t *testing.T) {
			svcType := reflect.TypeOf(tc.service).Elem()

			rpcNames := make(map[string]struct{})
			for i := 0; i < svcType.NumMethod(); i++ {
				fullName := tc.prefix + svcType.Method(i).Name
				rpcNames[fullName] = struct{}{}
			}

			var invalid []string
			for registeredName := range methodAuthorizations {
				if !strings.HasPrefix(registeredName, tc.prefix) {
					continue
				}
				if _, ok := rpcNames[registeredName]; !ok {
					invalid = append(invalid, registeredName)
				}
			}

			if len(invalid) > 0 {
				t.Fatalf("authorized RPCs without server methods (remove from methodAuthorizations):\n%s", strings.Join(invalid, "\n"))
			}
		})
	}
}
