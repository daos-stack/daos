//
// (C) Copyright 2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/daos-stack/daos/src/control/system/raft"
)

func mockSrvModule(t *testing.T, log logging.Logger, ec int) *srvModule {
	srv := &srvModule{
		log:    log,
		poolDB: raft.MockDatabase(t, log),
	}
	addEngineInstances(srv, ec, log)

	return srv
}

func TestSrvModule_HandleCheckerListPools(t *testing.T) {
	testPool := &system.PoolService{
		PoolUUID:  uuid.New(),
		PoolLabel: "test-pool",
		Replicas:  []ranklist.Rank{0, 1, 2},
	}

	for name, tc := range map[string]struct {
		req        []byte
		notReplica bool
		expResp    *sharedpb.CheckListPoolResp
		expErr     error
	}{
		"bad payload": {
			req:    []byte{'b', 'a', 'd'},
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"not replica": {
			notReplica: true,
			expResp:    &sharedpb.CheckListPoolResp{Status: int32(daos.MiscError)},
		},
		"success": {
			expResp: &sharedpb.CheckListPoolResp{
				Pools: []*sharedpb.CheckListPoolResp_OnePool{
					{
						Uuid:    testPool.PoolUUID.String(),
						Label:   testPool.PoolLabel,
						Svcreps: ranklist.RanksToUint32(testPool.Replicas),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			parent, cancel := context.WithCancel(context.Background())
			defer cancel()

			mod := mockSrvModule(t, log, 1)
			if tc.notReplica {
				mod.poolDB = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{})
			} else {
				lock, ctx := getPoolLockCtx(t, parent, mod.poolDB, testPool.PoolUUID)
				if err := mod.poolDB.AddPoolService(ctx, testPool); err != nil {
					t.Fatal(err)
				}
				lock.Release()
			}

			gotMsg, gotErr := mod.handleCheckerListPools(parent, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotResp := new(sharedpb.CheckListPoolResp)
			if err := proto.Unmarshal(gotMsg, gotResp); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expResp, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected response (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSrvModule_HandleCheckerRegisterPool(t *testing.T) {
	existingPool := &system.PoolService{
		PoolUUID:  uuid.New(),
		PoolLabel: "test-pool",
		Replicas:  []ranklist.Rank{0, 1, 2},
	}
	otherPool := &system.PoolService{
		PoolUUID:  uuid.New(),
		PoolLabel: "test-pool2",
		Replicas:  []ranklist.Rank{0, 1, 2},
	}
	makeReqBytes := func(id, label string, replicas []ranklist.Rank) []byte {
		req := &sharedpb.CheckRegPoolReq{
			Uuid:    id,
			Label:   label,
			Svcreps: ranklist.RanksToUint32(replicas),
		}
		b, err := proto.Marshal(req)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	newUUID := uuid.New().String()

	for name, tc := range map[string]struct {
		req        []byte
		notReplica bool
		expResp    *sharedpb.CheckRegPoolResp
		expErr     error
	}{
		"bad payload": {
			req:    []byte{'b', 'a', 'd'},
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"bad uuid": {
			req:     makeReqBytes("ow", "new", []ranklist.Rank{0}),
			expResp: &sharedpb.CheckRegPoolResp{Status: int32(daos.InvalidInput)},
		},
		"bad label": {
			req:     makeReqBytes(newUUID, newUUID, []ranklist.Rank{0}),
			expResp: &sharedpb.CheckRegPoolResp{Status: int32(daos.InvalidInput)},
		},
		"empty label": {
			req:     makeReqBytes(newUUID, "", []ranklist.Rank{0}),
			expResp: &sharedpb.CheckRegPoolResp{Status: int32(daos.InvalidInput)},
		},
		"zero svcreps": {
			req:     makeReqBytes(newUUID, "new", []ranklist.Rank{}),
			expResp: &sharedpb.CheckRegPoolResp{Status: int32(daos.InvalidInput)},
		},
		"not replica on update": {
			req:        makeReqBytes(existingPool.PoolUUID.String(), "new-label", []ranklist.Rank{1}),
			notReplica: true,
			expResp:    &sharedpb.CheckRegPoolResp{Status: int32(daos.MiscError)},
		},
		"not replica on add": {
			req:        makeReqBytes(newUUID, "new", []ranklist.Rank{0}),
			notReplica: true,
			expResp:    &sharedpb.CheckRegPoolResp{Status: int32(daos.MiscError)},
		},
		"duplicate label on update": {
			req:     makeReqBytes(existingPool.PoolUUID.String(), otherPool.PoolLabel, []ranklist.Rank{0}),
			expResp: &sharedpb.CheckRegPoolResp{Status: int32(daos.Exists)},
		},
		"duplicate label on add": {
			req:     makeReqBytes(newUUID, existingPool.PoolLabel, []ranklist.Rank{0}),
			expResp: &sharedpb.CheckRegPoolResp{Status: int32(daos.Exists)},
		},
		"successful update": {
			req:     makeReqBytes(existingPool.PoolUUID.String(), "new-label", []ranklist.Rank{1}),
			expResp: &sharedpb.CheckRegPoolResp{},
		},
		"successful add": {
			req:     makeReqBytes(newUUID, "new", []ranklist.Rank{0}),
			expResp: &sharedpb.CheckRegPoolResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			parent, cancel := context.WithCancel(context.Background())
			defer cancel()

			mod := mockSrvModule(t, log, 1)
			if tc.notReplica {
				mod.poolDB = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{})
			} else {
				lock, ctx := getPoolLockCtx(t, parent, mod.poolDB, existingPool.PoolUUID)
				if err := mod.poolDB.AddPoolService(ctx, existingPool); err != nil {
					t.Fatal(err)
				}
				lock.Release()
				lock, ctx = getPoolLockCtx(t, parent, mod.poolDB, otherPool.PoolUUID)
				if err := mod.poolDB.AddPoolService(ctx, otherPool); err != nil {
					t.Fatal(err)
				}
				lock.Release()
			}

			gotMsg, gotErr := mod.handleCheckerRegisterPool(parent, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotResp := new(sharedpb.CheckRegPoolResp)
			if err := proto.Unmarshal(gotMsg, gotResp); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expResp, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected response (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSrvModule_HandleCheckerDeregisterPool(t *testing.T) {
	existingPool := &system.PoolService{
		PoolUUID:  uuid.New(),
		PoolLabel: "test-pool",
		Replicas:  []ranklist.Rank{0, 1, 2},
	}
	makeReqBytes := func(id string) []byte {
		req := &sharedpb.CheckDeregPoolReq{
			Uuid: id,
		}
		b, err := proto.Marshal(req)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	unkUUID := uuid.New().String()

	for name, tc := range map[string]struct {
		req        []byte
		notReplica bool
		expResp    *sharedpb.CheckDeregPoolResp
		expErr     error
	}{
		"bad payload": {
			req:    []byte{'b', 'a', 'd'},
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"not replica": {
			req:        makeReqBytes(existingPool.PoolUUID.String()),
			notReplica: true,
			expResp:    &sharedpb.CheckDeregPoolResp{Status: int32(daos.MiscError)},
		},
		"bad uuid": {
			req:     makeReqBytes("ow"),
			expResp: &sharedpb.CheckDeregPoolResp{Status: int32(daos.InvalidInput)},
		},
		"unknown uuid": {
			req:     makeReqBytes(unkUUID),
			expResp: &sharedpb.CheckDeregPoolResp{Status: int32(daos.Nonexistent)},
		},
		"success": {
			req:     makeReqBytes(existingPool.PoolUUID.String()),
			expResp: &sharedpb.CheckDeregPoolResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			parent, cancel := context.WithCancel(context.Background())
			defer cancel()

			mod := mockSrvModule(t, log, 1)
			if tc.notReplica {
				mod.poolDB = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{})
			} else {
				lock, ctx := getPoolLockCtx(t, parent, mod.poolDB, existingPool.PoolUUID)
				if err := mod.poolDB.AddPoolService(ctx, existingPool); err != nil {
					t.Fatal(err)
				}
				lock.Release()
			}

			ctx := context.Background()
			gotMsg, gotErr := mod.handleCheckerDeregisterPool(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotResp := new(sharedpb.CheckDeregPoolResp)
			if err := proto.Unmarshal(gotMsg, gotResp); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expResp, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected response (-want +got):\n%s", diff)
			}
		})
	}
}

func TestServer_srvModule_handleCheckerReport(t *testing.T) {
	bytesFromReq := func(t *testing.T, req *sharedpb.CheckReportReq) []byte {
		b, err := proto.Marshal(req)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}

	// bare minimum
	validReq := &sharedpb.CheckReportReq{
		Report: &chkpb.CheckReport{},
	}

	for name, tc := range map[string]struct {
		mic      *control.MockInvokerConfig
		reqBytes []byte
		expErr   error
		expResp  *sharedpb.CheckReportResp
	}{
		"bad payload": {
			reqBytes: []byte{'b', 'a', 'd'},
			expErr:   drpc.UnmarshalingPayloadFailure(),
		},
		"gRPC failure to resp status": {
			mic: &control.MockInvokerConfig{
				UnaryError: errors.New("MockInvoker error"),
			},
			reqBytes: bytesFromReq(t, validReq),
			expResp: &sharedpb.CheckReportResp{
				Status: daos.MiscError.Int32(),
			},
		},
		"daos status error in resp": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{{
						Message: &sharedpb.CheckReportResp{
							Status: daos.MiscError.Int32(),
						},
					}},
				},
			},
			reqBytes: bytesFromReq(t, validReq),
			expResp: &sharedpb.CheckReportResp{
				Status: daos.MiscError.Int32(),
			},
		},
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{{
						Message: &sharedpb.CheckReportResp{},
					}},
				},
			},
			reqBytes: bytesFromReq(t, validReq),
			expResp:  &sharedpb.CheckReportResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)
			log := logging.FromContext(ctx)

			mod := mockSrvModule(t, log, 1)
			if tc.mic != nil {
				mod.rpcClient = control.NewMockInvoker(log, tc.mic)
			}

			respBytes, err := mod.handleCheckerReport(ctx, tc.reqBytes)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			resp := new(sharedpb.CheckReportResp)
			if err := proto.Unmarshal(respBytes, resp); err != nil {
				t.Fatal(err)
			}

			test.CmpAny(t, "CheckReportResp", tc.expResp, resp, cmpopts.IgnoreUnexported(sharedpb.CheckReportResp{}))
		})
	}
}

func TestServer_srvModule_chkReportErrToDaosStatus(t *testing.T) {
	for name, tc := range map[string]struct {
		in        error
		expResult daos.Status
	}{
		"bare daos.Status": {
			in:        daos.BadPath,
			expResult: daos.BadPath,
		},
		"wrapped daos.Status": {
			in:        errors.Wrap(daos.Busy, "a pretty pink bow"),
			expResult: daos.Busy,
		},
		"retryable connection error": {
			in:        control.FaultConnectionTimedOut("dontcare"),
			expResult: daos.TryAgain,
		},
		"permanent connection error": {
			in:        control.FaultConnectionBadHost("dontcare"),
			expResult: daos.Unreachable,
		},
		"ErrNotLeader": {
			in:        &system.ErrNotLeader{},
			expResult: daos.TryAgain,
		},
		"ErrNotReplica": {
			in:        &system.ErrNotReplica{},
			expResult: daos.TryAgain,
		},
		"MS connection failure": {
			in:        errors.Errorf("unable to contact the %s", build.ManagementServiceName), // unexported error from lib/control/system.go
			expResult: daos.Unreachable,
		},
		"checker not enabled": {
			in:        checker.FaultCheckerNotEnabled,
			expResult: daos.NotApplicable,
		},
		"checker not ready": {
			in:        checker.FaultIncorrectMemberStates(true, "dontcare", "dontcare"),
			expResult: daos.NotApplicable,
		},
		"generic failure": {
			in:        errors.New("'Twas brillig and the slithy toves did gyre and gimbel in the wabe"),
			expResult: daos.MiscError,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := chkReportErrToDaosStatus(tc.in)

			test.CmpErr(t, tc.expResult, result)
		})
	}
}
