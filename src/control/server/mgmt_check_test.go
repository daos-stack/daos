//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"net"
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto/chk"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/daos-stack/daos/src/control/system/raft"
)

var defaultPolicies = testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_DEFAULT)

func testPoliciesWithAction(action chkpb.CheckInconsistAction) []*mgmtpb.CheckInconsistPolicy {
	policies := make([]*mgmtpb.CheckInconsistPolicy, 0, len(chkpb.CheckInconsistClass_name))

	for cls := range chkpb.CheckInconsistClass_name {
		checkCls := chkpb.CheckInconsistClass(cls)
		if checkCls == chkpb.CheckInconsistClass_CIC_NONE || checkCls == chkpb.CheckInconsistClass_CIC_UNKNOWN {
			continue
		}
		policies = append(policies, &mgmtpb.CheckInconsistPolicy{
			InconsistCas: checkCls,
			InconsistAct: action,
		})
	}

	sort.Slice(policies, func(i, j int) bool { return policies[i].InconsistCas < policies[j].InconsistCas })
	return policies
}

func testSvcWithMemberState(t *testing.T, log logging.Logger, state system.MemberState, testPoolUUIDs []string) *mgmtSvc {
	t.Helper()

	t.Logf("creating a test MS with member state %s", state)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, testPoolUUIDs...)

	updateTestMemberState(t, svc, state)
	return svc
}

func updateTestMemberState(t *testing.T, svc *mgmtSvc, state system.MemberState) {
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
}

func testSvcCheckerEnabled(t *testing.T, log logging.Logger, state system.MemberState, testPoolUUIDs []string) *mgmtSvc {
	t.Helper()

	svc := testSvcWithMemberState(t, log, state, testPoolUUIDs)
	if err := svc.enableChecker(); err != nil {
		t.Fatal(err)
	}
	return svc
}

func testPoolUUIDs(numTestPools int) []string {
	uuids := []string{}
	for i := 0; i < numTestPools; i++ {
		uuids = append(uuids, test.MockPoolUUID(int32(i+1)).String())
	}
	return uuids
}

func mergeTestPolicies(current, merge []*mgmtpb.CheckInconsistPolicy) []*mgmtpb.CheckInconsistPolicy {
	polMap := make(policyMap)
	for _, cur := range current {
		polMap[cur.InconsistCas] = cur
	}
	for _, toMerge := range merge {
		polMap[toMerge.InconsistCas] = toMerge
	}
	return polMap.ToSlice()
}

func TestServer_mgmtSvc_SystemCheckStart(t *testing.T) {
	specificPolicies := []*mgmtpb.CheckInconsistPolicy{
		{
			InconsistCas: chk.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
			InconsistAct: chkpb.CheckInconsistAction_CIA_IGNORE,
		},
		{
			InconsistCas: chk.CheckInconsistClass_CIC_CONT_BAD_LABEL,
			InconsistAct: chkpb.CheckInconsistAction_CIA_INTERACT,
		},
	}
	testPolicies := testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_INTERACT)

	uuids := testPoolUUIDs(3)
	testFindings := func() []*checker.Finding {
		findings := []*checker.Finding{}
		for i, uuid := range uuids {
			f := &checker.Finding{CheckReport: chkpb.CheckReport{
				Seq:      uint64(i + 1),
				PoolUuid: uuid,
			}}
			findings = append(findings, f)
		}
		return findings
	}

	for name, tc := range map[string]struct {
		createMS    func(*testing.T, logging.Logger) *mgmtSvc
		setupDrpc   func(*testing.T, *mgmtSvc)
		req         *mgmtpb.CheckStartReq
		expResp     *mgmtpb.CheckStartResp
		expErr      error
		expFindings []*checker.Finding
		expPolicies []*mgmtpb.CheckInconsistPolicy
	}{
		"checker is not enabled": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcWithMemberState(t, log, system.MemberStateStopped, uuids)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr: checker.FaultCheckerNotEnabled,
		},
		"bad member states": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcCheckerEnabled(t, log, system.MemberStateJoined, uuids)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr: errors.New("expected states"),
		},
		"corrupted policy map": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
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
			expErr:      errors.New("mock dRPC"),
			expFindings: testFindings(),
			expPolicies: testPolicies,
		},
		"bad resp": {
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				setupMockDrpcClientBytes(ms, []byte("garbage"), nil)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expErr:      errors.New("unmarshal CheckStart response"),
			expFindings: testFindings(),
			expPolicies: testPolicies,
		},
		"request failed": {
			setupDrpc: func(t *testing.T, ms *mgmtSvc) {
				setupMockDrpcClient(ms, &mgmt.CheckStartResp{Status: int32(daos.MiscError)}, nil)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expResp:     &mgmt.CheckStartResp{Status: int32(daos.MiscError)},
			expFindings: testFindings(),
			expPolicies: testPolicies,
		},
		"no reset": {
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expResp:     &mgmtpb.CheckStartResp{},
			expFindings: testFindings(),
			expPolicies: testPolicies,
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
			expResp:     &mgmtpb.CheckStartResp{},
			expFindings: []*checker.Finding{},
			expPolicies: testPolicies,
		},
		"reset specific pools": {
			req: &mgmtpb.CheckStartReq{
				Sys:   "daos_server",
				Flags: uint32(chkpb.CheckFlag_CF_RESET),
				Uuids: []string{uuids[0], uuids[2]},
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
						PoolUuid: uuids[1],
					},
				},
			},
			expPolicies: testPolicies,
		},
		"no policy map": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
			},
			req: &mgmtpb.CheckStartReq{
				Sys: "daos_server",
			},
			expResp:     &mgmtpb.CheckStartResp{},
			expPolicies: defaultPolicies,
		},
		"req policies": {
			req: &mgmtpb.CheckStartReq{
				Sys:      "daos_server",
				Policies: specificPolicies,
			},
			expResp:     &mgmtpb.CheckStartResp{},
			expPolicies: mergeTestPolicies(testPolicies, specificPolicies),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.createMS == nil {
				tc.createMS = func(t *testing.T, log logging.Logger) *mgmtSvc {
					svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
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

			// ensure the slices are in the same order
			sort.Slice(tc.expPolicies, func(i, j int) bool { return tc.expPolicies[i].InconsistCas < tc.expPolicies[j].InconsistCas })
			sort.Slice(drpcInput.Policies, func(i, j int) bool { return drpcInput.Policies[i].InconsistCas < drpcInput.Policies[j].InconsistCas })
			if diff := cmp.Diff(tc.expPolicies, drpcInput.Policies, cmpopts.IgnoreUnexported(mgmtpb.CheckInconsistPolicy{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}

			// last used policies should be set
			lastPM, err := svc.getLastPoliciesUsed()
			if err != nil {
				t.Fatal(err)
			}
			lastPol := lastPM.ToSlice()
			if diff := cmp.Diff(tc.expPolicies, lastPol, cmpopts.IgnoreUnexported(mgmtpb.CheckInconsistPolicy{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}

func TestServer_mgmtSvc_SystemCheckGetPolicy(t *testing.T) {
	uuids := testPoolUUIDs(4)

	for name, tc := range map[string]struct {
		createMS func(*testing.T, logging.Logger) *mgmtSvc
		req      *mgmtpb.CheckGetPolicyReq
		expResp  *mgmtpb.CheckGetPolicyResp
		expErr   error
	}{
		"not MS replica": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvc(t, log)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
					Replicas:   []*net.TCPAddr{{IP: net.IP{111, 222, 1, 1}}},
				})
				return svc
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
			},
			expErr: errors.New("replica"),
		},
		"checker is not enabled": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcWithMemberState(t, log, system.MemberStateCheckerStarted, uuids)
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
			},
			expErr: checker.FaultCheckerNotEnabled,
		},
		"bad member states": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcCheckerEnabled(t, log, system.MemberStateJoined, uuids)
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
			},
			expErr: errors.New("expected states"),
		},
		"corrupted policy map": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				if err := system.SetMgmtProperty(svc.sysdb, checkerPoliciesKey, "garbage"); err != nil {
					t.Fatal(err)
				}
				return svc
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
			},
			expErr: errors.New("unmarshal checker policies"),
		},
		"default policies": {
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
			},
			expResp: &mgmtpb.CheckGetPolicyResp{
				Policies: defaultPolicies,
			},
		},
		"requested classes": {
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
				Classes: []chkpb.CheckInconsistClass{
					chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
					chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
				},
			},
			expResp: &mgmtpb.CheckGetPolicyResp{
				Policies: []*mgmtpb.CheckInconsistPolicy{
					{
						InconsistCas: chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
						InconsistAct: chkpb.CheckInconsistAction_CIA_DEFAULT,
					},
					{
						InconsistCas: chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
						InconsistAct: chkpb.CheckInconsistAction_CIA_DEFAULT,
					},
				},
			},
		},
		"non-default policies": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				if err := svc.setCheckerPolicyMap(testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_INTERACT)); err != nil {
					t.Fatal(err)
				}
				return svc
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
			},
			expResp: &mgmtpb.CheckGetPolicyResp{
				Policies: testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_INTERACT),
			},
		},
		"latest policy": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				latestPolicies := testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_TRUST_MS)
				if err := svc.setLastPoliciesUsed(latestPolicies); err != nil {
					t.Fatal(err)
				}
				return svc
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys:      "daos_server",
				LastUsed: true,
			},
			expResp: &mgmtpb.CheckGetPolicyResp{
				Policies: testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_TRUST_MS),
			},
		},
		"no latest policy saved": {
			req: &mgmtpb.CheckGetPolicyReq{
				Sys:      "daos_server",
				LastUsed: true,
			},
			expResp: &mgmtpb.CheckGetPolicyResp{
				Policies: testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_DEFAULT),
			},
		},
		"latest policy with requested classes": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				latestPolicies := testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_TRUST_MS)
				if err := svc.setLastPoliciesUsed(latestPolicies); err != nil {
					t.Fatal(err)
				}
				return svc
			},
			req: &mgmtpb.CheckGetPolicyReq{
				Sys: "daos_server",
				Classes: []chkpb.CheckInconsistClass{
					chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
					chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
				},
				LastUsed: true,
			},
			expResp: &mgmtpb.CheckGetPolicyResp{
				Policies: []*mgmtpb.CheckInconsistPolicy{
					{
						InconsistCas: chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
						InconsistAct: chkpb.CheckInconsistAction_CIA_TRUST_MS,
					},
					{
						InconsistCas: chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
						InconsistAct: chkpb.CheckInconsistAction_CIA_TRUST_MS,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.createMS == nil {
				tc.createMS = func(t *testing.T, log logging.Logger) *mgmtSvc {
					return testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				}
			}
			svc := tc.createMS(t, log)

			resp, err := svc.SystemCheckGetPolicy(test.Context(t), tc.req)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp, cmpopts.IgnoreUnexported(mgmtpb.CheckGetPolicyResp{}, mgmtpb.CheckInconsistPolicy{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}

func TestServer_mgmtSvc_SystemCheckSetPolicy(t *testing.T) {
	uuids := testPoolUUIDs(4)
	interactReq := &mgmtpb.CheckSetPolicyReq{
		Sys:      "daos_server",
		Policies: testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_INTERACT),
	}

	for name, tc := range map[string]struct {
		createMS    func(*testing.T, logging.Logger) *mgmtSvc
		req         *mgmtpb.CheckSetPolicyReq
		expResp     *mgmtpb.DaosResp
		expErr      error
		expPolicies []*mgmtpb.CheckInconsistPolicy
	}{
		"not MS replica": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvc(t, log)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
					Replicas:   []*net.TCPAddr{{IP: net.IP{111, 222, 1, 1}}},
				})
				return svc
			},
			req:    interactReq,
			expErr: errors.New("replica"),
		},
		"checker is not enabled": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcWithMemberState(t, log, system.MemberStateCheckerStarted, uuids)
			},
			req:    interactReq,
			expErr: checker.FaultCheckerNotEnabled,
		},
		"bad member states": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return testSvcCheckerEnabled(t, log, system.MemberStateJoined, uuids)
			},
			req:    interactReq,
			expErr: errors.New("expected states"),
		},
		"corrupted policy map": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				if err := system.SetMgmtProperty(svc.sysdb, checkerPoliciesKey, "garbage"); err != nil {
					t.Fatal(err)
				}
				return svc
			},
			req:    interactReq,
			expErr: errors.New("unmarshal checker policies"),
		},
		"no policies in request": {
			req: &mgmtpb.CheckSetPolicyReq{
				Sys: "daos_server",
			},
			expResp:     &mgmtpb.DaosResp{},
			expPolicies: testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_DEFAULT),
		},
		"set all policies": {
			req:         interactReq,
			expResp:     &mgmtpb.DaosResp{},
			expPolicies: interactReq.Policies,
		},
		"set single policy": {
			req: &mgmtpb.CheckSetPolicyReq{
				Sys: "daos_server",
				Policies: []*mgmtpb.CheckInconsistPolicy{
					{
						InconsistCas: chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
						InconsistAct: chkpb.CheckInconsistAction_CIA_TRUST_MS,
					},
				},
			},
			expResp: &mgmtpb.DaosResp{},
			expPolicies: mergeTestPolicies(testPoliciesWithAction(chkpb.CheckInconsistAction_CIA_DEFAULT),
				[]*mgmtpb.CheckInconsistPolicy{
					{
						InconsistCas: chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
						InconsistAct: chkpb.CheckInconsistAction_CIA_TRUST_MS,
					},
				}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.createMS == nil {
				tc.createMS = func(t *testing.T, log logging.Logger) *mgmtSvc {
					return testSvcCheckerEnabled(t, log, system.MemberStateCheckerStarted, uuids)
				}
			}
			svc := tc.createMS(t, log)

			resp, err := svc.SystemCheckSetPolicy(test.Context(t), tc.req)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp, cmpopts.IgnoreUnexported(mgmtpb.DaosResp{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}

			if tc.expPolicies == nil {
				return
			}

			policies, err := svc.getCheckerPolicyMap()
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expPolicies, policies.ToSlice(), cmpopts.IgnoreUnexported(mgmtpb.CheckInconsistPolicy{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}
