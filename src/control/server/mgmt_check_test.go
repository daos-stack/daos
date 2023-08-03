//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/chk"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func TestServer_mgmtSvc_SystemCheckStart(t *testing.T) {
	numTestPools := 3
	testPoolUUIDs := []string{}
	for i := 0; i < numTestPools; i++ {
		testPoolUUIDs = append(testPoolUUIDs, test.MockPoolUUID(int32(i+1)).String())
	}

	testPolicies := []*mgmtpb.CheckInconsistPolicy{
		{
			InconsistCas: chk.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
			InconsistAct: chkpb.CheckInconsistAction_CIA_IGNORE,
		},
		{
			InconsistCas: chk.CheckInconsistClass_CIC_CONT_BAD_LABEL,
			InconsistAct: chkpb.CheckInconsistAction_CIA_INTERACT,
		},
	}

	testSvcWithMemberState := func(t *testing.T, log logging.Logger, state system.MemberState) *mgmtSvc {
		t.Helper()

		t.Logf("creating a test MS with member state %s", state)

		svc := newTestMgmtSvc(t, log)
		addTestPools(t, svc.sysdb, testPoolUUIDs...)

		members, err := svc.sysdb.AllMembers()
		if err != nil {
			t.Fatal(err)
		}
		for _, m := range members {
			m.State = state
			if err := svc.sysdb.UpdateMember(m); err != nil {
				t.Fatal(err)
			}
		}
		return svc
	}

	testSvcCheckerEnabled := func(t *testing.T, log logging.Logger, state system.MemberState) *mgmtSvc {
		t.Helper()

		svc := testSvcWithMemberState(t, log, state)
		if err := svc.enableChecker(); err != nil {
			t.Fatal(err)
		}
		return svc
	}

	testFindings := func() []*checker.Finding {
		findings := []*checker.Finding{}
		for i, uuid := range testPoolUUIDs {
			f := &checker.Finding{CheckReport: chkpb.CheckReport{
				Seq:      uint64(i + 1),
				PoolUuid: uuid,
			}}
			findings = append(findings, f)
		}
		return findings
	}

	for name, tc := range map[string]struct {
		createMS        func(*testing.T, logging.Logger) *mgmtSvc
		setupDrpc       func(*testing.T, *mgmtSvc)
		req             *mgmtpb.CheckStartReq
		expResp         *mgmtpb.CheckStartResp
		expErr          error
		expFindings     []*checker.Finding
		expDrpcPolicies []*mgmtpb.CheckInconsistPolicy
	}{
		"checker is not enabled": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcWithMemberState(t, log, system.MemberStateStopped)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr: checker.FaultCheckerNotEnabled,
		},
		"bad member states": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcCheckerEnabled(t, log, system.MemberStateJoined)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr: errors.New("expected states"),
		},
		"corrupted policy map": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted)
				if err := system.SetMgmtProperty(svc.sysdb, checkerPoliciesKey, "garbage"); err != nil {
					t.Fatal(err)
				}
				return svc
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr: errors.New("unmarshal checker policies"),
		},
		"dRPC fails": {
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				setupMockDrpcClient(ms, nil, errors.New("mock dRPC"))
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr:          errors.New("mock dRPC"),
			expFindings:     testFindings(),
			expDrpcPolicies: testPolicies,
		},
		"bad resp": {
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				setupMockDrpcClientBytes(ms, []byte("garbage"), nil)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr:          errors.New("unmarshal CheckStart response"),
			expFindings:     testFindings(),
			expDrpcPolicies: testPolicies,
		},
		"request failed": {
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				setupMockDrpcClient(ms, &mgmt.CheckStartResp{Status: int32(daos.MiscError)}, nil)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expResp:         &mgmt.CheckStartResp{Status: int32(daos.MiscError)},
			expFindings:     testFindings(),
			expDrpcPolicies: testPolicies,
		},
		"no reset": {
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expResp:         &mgmtpb.CheckStartResp{},
			expFindings:     testFindings(),
			expDrpcPolicies: testPolicies,
		},
		"reset": {
			req: &mgmtpb.CheckStartReq{
				Sys:   "daos_server",
				Flags: uint32(chkpb.CheckFlag_CF_RESET),
			},
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				// engine returns status > 0 to indicate reset
				setupMockDrpcClient(ms, &mgmt.CheckStartResp{Status: 1}, nil)
			},
			expResp:         &mgmtpb.CheckStartResp{},
			expFindings:     []*checker.Finding{},
			expDrpcPolicies: testPolicies,
		},
		"reset specific pools": {
			req: &mgmtpb.CheckStartReq{
				Sys:   "daos_server",
				Flags: uint32(chkpb.CheckFlag_CF_RESET),
				Uuids: []string{testPoolUUIDs[0], testPoolUUIDs[2]},
			},
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				// engine returns status > 0 to indicate reset
				setupMockDrpcClient(ms, &mgmt.CheckStartResp{Status: 1}, nil)
			},
			expResp: &mgmtpb.CheckStartResp{},
			expFindings: []*checker.Finding{
				{
					CheckReport: chkpb.CheckReport{
						Seq:      2,
						PoolUuid: testPoolUUIDs[1],
					},
				},
			},
			expDrpcPolicies: testPolicies,
		},
		"no policy map": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expResp: &mgmtpb.CheckStartResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.createMS == nil {
				tc.createMS = func(t *testing.T, log logging.Logger) *mgmtSvc {
					svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted)
					if err := svc.setCheckerPolicyMap(testPolicies); err != nil {
						t.Fatal(err)
					}
					for _, f := range testFindings() {
						if err := svc.sysdb.AddCheckerFinding(f); err != nil {
							t.Fatal(err)
						}
					}
					return svc
				}
			}
			svc := tc.createMS(t, log)

			if tc.setupDrpc == nil {
				tc.setupDrpc = func(t *testing.T, ms *mgmtSvc) {
					setupMockDrpcClient(ms, &mgmtpb.CheckStartResp{}, nil)
				}
			}
			tc.setupDrpc(t, svc)

			resp, err := svc.SystemCheckStart(test.Context(t), tc.req)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp, cmpopts.IgnoreUnexported(mgmtpb.CheckStartResp{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}

			if tc.expFindings != nil {
				findings, err := svc.sysdb.GetCheckerFindings()
				sort.Slice(findings, func(i, j int) bool {
					return findings[i].Seq < findings[j].Seq
				})
				test.CmpErr(t, nil, err)
				if diff := cmp.Diff(tc.expFindings, findings, cmpopts.IgnoreUnexported(chkpb.CheckReport{})); diff != "" {
					t.Fatalf("want-, got+:\n%s", diff)
				}
			}

			// Check contents of drpc payload
			ei, ok := svc.harness.instances[0].(*EngineInstance)
			if !ok {
				t.Fatalf("bad engine instance type %T", svc.harness.instances[0])
			}
			mockDrpc, ok := ei._drpcClient.(*mockDrpcClient)
			if !ok {
				t.Fatalf("bad drpc client type type %T", ei._drpcClient)
			}

			drpcInput := new(mgmtpb.CheckStartReq)
			calls := mockDrpc.calls.get()
			if len(calls) == 0 {
				return
			}

			if err := proto.Unmarshal(mockDrpc.calls.get()[0].Body, drpcInput); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expDrpcPolicies, drpcInput.Policies, cmpopts.IgnoreUnexported(mgmtpb.CheckInconsistPolicy{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}
