//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"fmt"
	"reflect"
	"strings"
	"testing"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
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
func inList(c Component, compList []Component) bool {
	for _, comp := range compList {
		if c == comp {
			return true
		}
	}
	return false
}
func TestSecurity_ComponentHasAccess(t *testing.T) {
	allComponents := []Component{ComponentUndefined, ComponentAdmin, ComponentAgent, ComponentServer}
	testCases := map[string][]Component{
		"/ctl.CtlSvc/StorageScan":              {ComponentAdmin},
		"/ctl.CtlSvc/StorageFormat":            {ComponentAdmin},
		"/ctl.CtlSvc/StorageNvmeRebind":        {ComponentAdmin},
		"/ctl.CtlSvc/StorageNvmeAddDevice":     {ComponentAdmin},
		"/ctl.CtlSvc/NetworkScan":              {ComponentAdmin},
		"/ctl.CtlSvc/CollectLog":               {ComponentAdmin},
		"/ctl.CtlSvc/FirmwareQuery":            {ComponentAdmin},
		"/ctl.CtlSvc/FirmwareUpdate":           {ComponentAdmin},
		"/ctl.CtlSvc/SmdQuery":                 {ComponentAdmin},
		"/ctl.CtlSvc/SmdManage":                {ComponentAdmin},
		"/ctl.CtlSvc/SetEngineLogMasks":        {ComponentAdmin},
		"/ctl.CtlSvc/PrepShutdownRanks":        {ComponentServer},
		"/ctl.CtlSvc/StopRanks":                {ComponentServer},
		"/ctl.CtlSvc/ResetFormatRanks":         {ComponentServer},
		"/ctl.CtlSvc/StartRanks":               {ComponentServer},
		"/mgmt.MgmtSvc/Join":                   {ComponentServer},
		"/mgmt.MgmtSvc/ClusterEvent":           {ComponentServer},
		"/mgmt.MgmtSvc/LeaderQuery":            {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemQuery":            {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemStop":             {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemErase":            {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemStart":            {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemExclude":          {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolCreate":             {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolDestroy":            {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolQuery":              {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolQueryTarget":        {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolSetProp":            {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolGetProp":            {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolGetACL":             {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolOverwriteACL":       {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolUpdateACL":          {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolDeleteACL":          {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolExclude":            {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolDrain":              {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolReintegrate":        {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolEvict":              {ComponentAdmin, ComponentAgent},
		"/mgmt.MgmtSvc/PoolExtend":             {ComponentAdmin},
		"/mgmt.MgmtSvc/GetAttachInfo":          {ComponentAgent},
		"/mgmt.MgmtSvc/ListPools":              {ComponentAdmin},
		"/mgmt.MgmtSvc/ListContainers":         {ComponentAdmin},
		"/mgmt.MgmtSvc/ContSetOwner":           {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemCleanup":          {ComponentAdmin},
		"/mgmt.MgmtSvc/PoolUpgrade":            {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemSetAttr":          {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemGetAttr":          {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemSetProp":          {ComponentAdmin},
		"/mgmt.MgmtSvc/SystemGetProp":          {ComponentAdmin},
		"/RaftTransport/AppendEntries":         {ComponentServer},
		"/RaftTransport/AppendEntriesPipeline": {ComponentServer},
		"/RaftTransport/RequestVote":           {ComponentServer},
		"/RaftTransport/TimeoutNow":            {ComponentServer},
		"/RaftTransport/InstallSnapshot":       {ComponentServer},
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
				if inList(comp, correctComponent) {
					test.AssertTrue(t, comp.HasAccess(method), fmt.Sprintf("%s should have access to %s but does not", comp, methodName))
				} else {
					test.AssertFalse(t, comp.HasAccess(method), fmt.Sprintf("%s should not have access to %s but does", comp, methodName))
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
			service: (*ctlpb.CtlSvcServer)(nil),
			prefix:  "/ctl.CtlSvc/",
		},
	} {
		t.Run(name, func(t *testing.T) {
			svcType := reflect.TypeOf(tc.service).Elem()

			var rpcNames []string
			for i := 0; i < svcType.NumMethod(); i++ {
				if strings.HasPrefix(svcType.Method(i).Name, "mustEmbedUnimplemented") {
					continue
				}
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
			service: (*ctlpb.CtlSvcServer)(nil),
			prefix:  "/ctl.CtlSvc/",
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
