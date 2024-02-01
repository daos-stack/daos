//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

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

func TestSrvModule_HandleNotifyReady_Invalid(t *testing.T) {
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

func TestSrvModule_HandleNotifyReady_BadSockPath(t *testing.T) {
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

func TestSrvModule_HandleNotifyReady_Success_Single(t *testing.T) {
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

func TestSrvModule_HandleNotifyReady_Success_Multi(t *testing.T) {
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

func TestSrvModule_HandleNotifyReady_IdxOutOfRange(t *testing.T) {
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

func TestSrvModule_HandleClusterEvent_Invalid(t *testing.T) {
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

func TestSrvModule_handleGetPoolServiceRanks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		reqBytes []byte
		testPool *system.PoolService
		expResp  []byte
		expErr   error
	}{
		"bad request bytes": {
			reqBytes: []byte("bad bytes"),
			expErr:   drpc.UnmarshalingPayloadFailure(),
		},
		"bad pool uuid in request": {
			reqBytes: getTestBytes(t, &srvpb.GetPoolSvcReq{
				Uuid: "bad-uuid",
			}),
			expErr: errors.New("invalid pool uuid"),
		},
		"not found": {
			reqBytes: getTestBytes(t, &srvpb.GetPoolSvcReq{
				Uuid: test.MockUUID(),
			}),
			expResp: getTestBytes(t, &srvpb.GetPoolSvcResp{
				Status: int32(daos.Nonexistent),
			}),
		},
		"found, but not Ready": {
			reqBytes: getTestBytes(t, &srvpb.GetPoolSvcReq{
				Uuid: test.MockUUID(),
			}),
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateCreating,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: getTestBytes(t, &srvpb.GetPoolSvcResp{
				Status: int32(daos.Nonexistent),
			}),
		},
		"success": {
			reqBytes: getTestBytes(t, &srvpb.GetPoolSvcReq{
				Uuid: test.MockUUID(),
			}),
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateReady,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: getTestBytes(t, &srvpb.GetPoolSvcResp{
				Svcreps: []uint32{0, 1, 2},
			}),
		},
	} {
		t.Run(name, func(t *testing.T) {
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

			resp, err := mod.handleGetPoolServiceRanks(tc.reqBytes)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSrvModule_handlePoolFindByLabel(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		reqBytes []byte
		testPool *system.PoolService
		expResp  []byte
		expErr   error
	}{
		"bad request bytes": {
			reqBytes: []byte("bad bytes"),
			expErr:   drpc.UnmarshalingPayloadFailure(),
		},
		"not found": {
			reqBytes: getTestBytes(t, &srvpb.PoolFindByLabelReq{
				Label: "testlabel",
			}),
			expResp: getTestBytes(t, &srvpb.PoolFindByLabelResp{
				Status: int32(daos.Nonexistent),
			}),
		},
		"found, but not Ready": {
			reqBytes: getTestBytes(t, &srvpb.PoolFindByLabelReq{
				Label: "testlabel",
			}),
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateCreating,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: getTestBytes(t, &srvpb.PoolFindByLabelResp{
				Status: int32(daos.Nonexistent),
			}),
		},
		"success": {
			reqBytes: getTestBytes(t, &srvpb.PoolFindByLabelReq{
				Label: "testlabel",
			}),
			testPool: &system.PoolService{
				PoolUUID:  test.MockPoolUUID(),
				PoolLabel: "testlabel",
				State:     system.PoolServiceStateReady,
				Replicas:  []ranklist.Rank{0, 1, 2},
			},
			expResp: getTestBytes(t, &srvpb.PoolFindByLabelResp{
				Uuid:    test.MockPoolUUID().String(),
				Svcreps: []uint32{0, 1, 2},
			}),
		},
	} {
		t.Run(name, func(t *testing.T) {
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

			resp, err := mod.handlePoolFindByLabel(tc.reqBytes)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
