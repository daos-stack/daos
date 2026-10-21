//
// (C) Copyright 2019-2023 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

func TestSrv_mgmtModule_GetMethod(t *testing.T) {
	mod := newMgmtModule()

	_, err := mod.GetMethod(123)

	test.CmpErr(t, errors.New("implements no methods"), err)
}

func getTestNotifyReadyReqBytes(t *testing.T, sockPath string, idx uint32) []byte {
	req := getTestNotifyReadyReq(t, sockPath, idx)
	reqBytes, err := proto.Marshal(req)

	if err != nil {
		t.Fatalf("Couldn't create fake request: %v", err)
	}

	return reqBytes
}

func isEngineReady(instance *EngineInstance) bool {
	select {
	case <-instance.awaitDrpcReady():
		return true
	default:
		return false
	}
}

func addEngineInstances(mod *srvModule, numInstances int, log logging.Logger) {
	for i := 0; i < numInstances; i++ {
		mod.engines = append(mod.engines, getTestEngineInstance(log))
	}
}

func TestSrvModule_handleNotifyReady_Invalid(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expectedErr := drpc.UnmarshalingPayloadFailure()
	mod := &srvModule{}
	addEngineInstances(mod, 1, log)

	// Some arbitrary bytes, shouldn't translate to a request
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	err := mod.handleNotifyReady(badBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedErr.Error()) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedErr, err.Error())
	}
}

func TestSrvModule_handleNotifyReady_BadSockPath(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expectedErr := "check NotifyReady request socket path"
	mod := &srvModule{}
	addEngineInstances(mod, 1, log)

	reqBytes := getTestNotifyReadyReqBytes(t, "/some/bad/path", 0)

	err := mod.handleNotifyReady(reqBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedErr) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedErr, err.Error())
	}
}

func TestSrvModule_handleNotifyReady_Success_Single(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := &srvModule{}
	addEngineInstances(mod, 1, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := test.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath, 0)

	err := mod.handleNotifyReady(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}

	waitForEngineReady(t, mod.engines[0].(*EngineInstance))
}

func TestSrvModule_handleNotifyReady_Success_Multi(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := &srvModule{}
	numInstances := 5
	idx := uint32(numInstances - 1)

	addEngineInstances(mod, numInstances, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := test.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath, idx)

	err := mod.handleNotifyReady(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}

	// I/O Engine at idx should be marked ready
	waitForEngineReady(t, mod.engines[idx].(*EngineInstance))
	// None of the other IO engines should have gotten the message
	for i, e := range mod.engines {
		s := e.(*EngineInstance)
		if uint32(i) != idx && isEngineReady(s) {
			t.Errorf("Expected engine at idx %v to be NOT ready", i)
		}
	}
}

func TestSrvModule_handleNotifyReady_IdxOutOfRange(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expectedError := "out of range"
	mod := &srvModule{}
	numInstances := 5

	addEngineInstances(mod, numInstances, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := test.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath,
		uint32(numInstances))

	err := mod.handleNotifyReady(reqBytes)

	if err == nil {
		t.Fatal("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedError) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedError, err.Error())
	}
}

func TestSrvModule_handleClusterEvent_Invalid(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expectedErr := errors.New("unmarshal method-specific payload")
	mod := &srvModule{}
	addEngineInstances(mod, 1, log)

	// Some arbitrary bytes, shouldn't translate to a request
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	_, err := mod.handleClusterEvent(badBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	test.CmpErr(t, expectedErr, err)
}

func getTestBytes(t *testing.T, msg proto.Message) []byte {
	t.Helper()

	testBytes, err := proto.Marshal(msg)
	if err != nil {
		t.Fatal(err)
	}

	return testBytes
}

func cmpTestResp(t *testing.T, respBytes []byte, resp, expResp proto.Message) {
	t.Helper()

	if err := proto.Unmarshal(respBytes, resp); err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(expResp, resp, protocmp.Transform()); diff != "" {
		t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
	}
}

func TestSrvModule_handleGetPoolServiceRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		req      *srvpb.GetPoolSvcReq
		badReq   bool
		testPool *system.PoolService
		expResp  *srvpb.GetPoolSvcResp
		expErr   error
	}{
		"bad request bytes": {
			badReq: true,
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"bad pool uuid in request": {
			req: &srvpb.GetPoolSvcReq{
				Uuid: "bad-uuid",
			},
			expErr: errors.New("invalid pool uuid"),
		},
		"not found": {
			req: &srvpb.GetPoolSvcReq{
				Uuid: test.MockUUID(),
			},
			expResp: &srvpb.GetPoolSvcResp{
				Status: int32(daos.Nonexistent),
			},
		},
		"found, but not Ready": {
			req: &srvpb.GetPoolSvcReq{
				Uuid: test.MockUUID(),
			},
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateCreating,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: &srvpb.GetPoolSvcResp{
				Status: int32(daos.Nonexistent),
			},
		},
		"success": {
			req: &srvpb.GetPoolSvcReq{
				Uuid: test.MockUUID(),
			},
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateReady,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: &srvpb.GetPoolSvcResp{
				Svcreps: []uint32{0, 1, 2},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)

			db := raft.MockDatabase(t, log)
			mod := &srvModule{
				log:    log,
				poolDB: db,
			}
			if tc.testPool != nil {
				lock, err := db.TakePoolLock(ctx, tc.testPool.PoolUUID)
				if err != nil {
					t.Fatal(err)
				}
				defer lock.Release()
				if err := db.AddPoolService(lock.InContext(ctx), tc.testPool); err != nil {
					t.Fatal(err)
				}
			}

			reqBytes := []byte("bad bytes")
			if !tc.badReq {
				reqBytes = getTestBytes(t, tc.req)
			}

			respBytes, err := mod.handleGetPoolServiceRanks(reqBytes)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			cmpTestResp(t, respBytes, new(srvpb.GetPoolSvcResp), tc.expResp)
		})
	}
}

func TestSrvModule_handlePoolFindByLabel(t *testing.T) {
	for name, tc := range map[string]struct {
		req      *srvpb.PoolFindByLabelReq
		badReq   bool
		testPool *system.PoolService
		expResp  *srvpb.PoolFindByLabelResp
		expErr   error
	}{
		"bad request bytes": {
			badReq: true,
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"not found": {
			req: &srvpb.PoolFindByLabelReq{
				Label: "testlabel",
			},
			expResp: &srvpb.PoolFindByLabelResp{
				Status: int32(daos.Nonexistent),
			},
		},
		"found, but not Ready": {
			req: &srvpb.PoolFindByLabelReq{
				Label: "testlabel",
			},
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateCreating,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: &srvpb.PoolFindByLabelResp{
				Status: int32(daos.Nonexistent),
			},
		},
		"success": {
			req: &srvpb.PoolFindByLabelReq{
				Label: "testlabel",
			},
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateReady,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: &srvpb.PoolFindByLabelResp{
				Uuid:    test.MockPoolUUID().String(),
				Svcreps: []uint32{0, 1, 2},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)

			db := raft.MockDatabase(t, log)
			mod := &srvModule{
				log:    log,
				poolDB: db,
			}
			if tc.testPool != nil {
				lock, err := db.TakePoolLock(ctx, tc.testPool.PoolUUID)
				if err != nil {
					t.Fatal(err)
				}
				defer lock.Release()
				if err := db.AddPoolService(lock.InContext(ctx), tc.testPool); err != nil {
					t.Fatal(err)
				}
			}

			reqBytes := []byte("bad bytes")
			if !tc.badReq {
				reqBytes = getTestBytes(t, tc.req)
			}

			respBytes, err := mod.handlePoolFindByLabel(reqBytes)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			cmpTestResp(t, respBytes, new(srvpb.PoolFindByLabelResp), tc.expResp)
		})
	}
}

func TestSrvModule_handleListPools(t *testing.T) {
	for name, tc := range map[string]struct {
		req       *srvpb.ListPoolsReq
		badReq    bool
		testPools []*system.PoolService
		expResp   *srvpb.ListPoolsResp
		expErr    error
	}{
		"bad request bytes": {
			badReq: true,
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"no pools": {
			req: &srvpb.ListPoolsReq{
				IncludeAll: false,
			},
			expResp: &srvpb.ListPoolsResp{
				Pools: []*srvpb.ListPoolsResp_Pool{},
			},
		},
		"single pool": {
			req: &srvpb.ListPoolsReq{
				IncludeAll: false,
			},
			testPools: []*system.PoolService{
				{
					PoolUUID:  test.MockPoolUUID(1),
					PoolLabel: "pool1",
					State:     system.PoolServiceStateReady,
					Replicas:  []ranklist.Rank{0, 1, 2},
				},
			},
			expResp: &srvpb.ListPoolsResp{
				Pools: []*srvpb.ListPoolsResp_Pool{
					{
						Uuid:    test.MockPoolUUID(1).String(),
						Label:   "pool1",
						Svcreps: []uint32{0, 1, 2},
					},
				},
			},
		},
		"multiple pools": {
			req: &srvpb.ListPoolsReq{
				IncludeAll: true,
			},
			testPools: []*system.PoolService{
				{
					PoolUUID:  test.MockPoolUUID(1),
					PoolLabel: "pool1",
					State:     system.PoolServiceStateReady,
					Replicas:  []ranklist.Rank{0, 1, 2},
				},
				{
					PoolUUID:  test.MockPoolUUID(2),
					PoolLabel: "pool2",
					State:     system.PoolServiceStateCreating,
					Replicas:  []ranklist.Rank{3, 4, 5},
				},
			},
			expResp: &srvpb.ListPoolsResp{
				Pools: []*srvpb.ListPoolsResp_Pool{
					{
						Uuid:    test.MockPoolUUID(1).String(),
						Label:   "pool1",
						Svcreps: []uint32{0, 1, 2},
					},
					{
						Uuid:    test.MockPoolUUID(2).String(),
						Label:   "pool2",
						Svcreps: []uint32{3, 4, 5},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)

			db := raft.MockDatabase(t, log)
			mod := &srvModule{
				log:    log,
				poolDB: db,
			}
			for _, pool := range tc.testPools {
				lock, err := db.TakePoolLock(ctx, pool.PoolUUID)
				if err != nil {
					t.Fatal(err)
				}
				if err := db.AddPoolService(lock.InContext(ctx), pool); err != nil {
					lock.Release()
					t.Fatal(err)
				}
				lock.Release()
			}

			reqBytes := []byte("bad bytes")
			if !tc.badReq {
				reqBytes = getTestBytes(t, tc.req)
			}

			respBytes, err := mod.handleListPools(reqBytes)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			resp := new(srvpb.ListPoolsResp)
			if err := proto.Unmarshal(respBytes, resp); err != nil {
				t.Fatal(err)
			}

			if len(tc.expResp.Pools) != len(resp.Pools) {
				t.Fatal("unexpected number of pools returned")
			}
			for _, pool := range tc.expResp.Pools {
				found := false
				for _, expPool := range resp.Pools {
					if pool.Uuid != expPool.Uuid {
						continue
					}
					if diff := cmp.Diff(expPool, pool, protocmp.Transform()); diff != "" {
						t.Fatalf("unexpected pool in response (-want, +got):\n%s\n", diff)
					}
					found = true
					break
				}
				if !found {
					t.Fatalf("pool %v not found", pool)
				}
			}
		})
	}
}

func TestSrvModule_handleGetSysProps(t *testing.T) {
	mockMSReplicas := []string{"host1:10001"}

	for name, tc := range map[string]struct {
		req        *mgmtpb.SystemGetPropReq
		badReq     bool
		mic        *control.MockInvokerConfig // For control-API SystemGetProp
		expCtlCall *control.SystemGetPropReq
		expResp    *mgmtpb.SystemGetPropResp
		expErr     error
	}{
		"bad request bytes": {
			badReq: true,
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"invalid system property key": {
			req: &mgmtpb.SystemGetPropReq{
				Sys:  "daos_server",
				Keys: []string{"invalid-key"},
			},
			expErr: errors.New("invalid system property key"),
		},
		"control API error": {
			req: &mgmtpb.SystemGetPropReq{
				Sys:  "daos_server",
				Keys: []string{"self_heal"},
			},
			mic: &control.MockInvokerConfig{
				UnaryError: errors.New("control API failed"),
			},
			expCtlCall: &control.SystemGetPropReq{},
			expErr:     errors.New("failed to get system properties from MS"),
		},
		"success with single property": {
			req: &mgmtpb.SystemGetPropReq{
				Sys:  "daos_server",
				Keys: []string{"self_heal"},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1:10001", nil,
					&mgmtpb.SystemGetPropResp{
						Properties: map[string]string{
							"self_heal": "exclude",
						},
					}),
			},
			expCtlCall: &control.SystemGetPropReq{
				Keys: []daos.SystemPropertyKey{
					daos.SystemPropertySelfHeal,
				},
			},
			expResp: &mgmtpb.SystemGetPropResp{
				Properties: map[string]string{
					"self_heal": "exclude",
				},
			},
		},
		"success with multiple properties": {
			req: &mgmtpb.SystemGetPropReq{
				Sys:  "marigolds",
				Keys: []string{"self_heal", "pool_scrub_thresh"},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1:10001", nil,
					&mgmtpb.SystemGetPropResp{
						Properties: map[string]string{
							"self_heal":         "exclude",
							"pool_scrub_thresh": "0",
						},
					}),
			},
			expCtlCall: &control.SystemGetPropReq{
				Keys: []daos.SystemPropertyKey{
					daos.SystemPropertySelfHeal,
					daos.SystemPropertyPoolScrubThresh,
				},
			},
			expResp: &mgmtpb.SystemGetPropResp{
				Properties: map[string]string{
					"self_heal":         "exclude",
					"pool_scrub_thresh": "0",
				},
			},
		},
		"empty request returns empty response": {
			req: &mgmtpb.SystemGetPropReq{
				Sys:  "daos_server",
				Keys: []string{},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1:10001", nil,
					&mgmtpb.SystemGetPropResp{
						Properties: map[string]string{},
					}),
			},
			expCtlCall: &control.SystemGetPropReq{
				Keys: []daos.SystemPropertyKey{},
			},
			expResp: &mgmtpb.SystemGetPropResp{
				Properties: map[string]string{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			mod := &srvModule{
				log:        log,
				rpcClient:  mi,
				msReplicas: mockMSReplicas,
			}

			reqBytes := []byte("bad bytes")
			if !tc.badReq {
				reqBytes = getTestBytes(t, tc.req)
			}

			respBytes, err := mod.handleGetSysProps(reqBytes)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			cmpTestResp(t, respBytes, new(mgmtpb.SystemGetPropResp), tc.expResp)

			switch mi.GetInvokeCount() {
			case 0:
				if tc.expCtlCall != nil {
					t.Fatal("expected control API call but got none")
				}
			case 1:
				if tc.expCtlCall == nil {
					t.Fatal("unexpected control API call")
				}
				getPropReqSent := mi.SentReqs[0].(*control.SystemGetPropReq)
				cmpOpt := cmpopts.IgnoreFields(control.SystemGetPropReq{},
					"unaryRequest", "msRequest")
				if diff := cmp.Diff(tc.expCtlCall, getPropReqSent, cmpOpt); diff != "" {
					t.Fatalf("unexpected control API call (-want, +got):\n%s\n",
						diff)
				}
				test.AssertEqual(t, tc.req.Sys, getPropReqSent.Sys,
					"system name mismatch")
			default:
				t.Fatalf("unexpected number of control API calls: %d",
					mi.GetInvokeCount())
			}
		})
	}
}
