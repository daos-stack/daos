//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	uuid "github.com/google/uuid"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
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
		Replicas:  []system.Rank{0, 1, 2},
	}

	for name, tc := range map[string]struct {
		req        []byte
		notReplica bool
		expResp    *srvpb.CheckListPoolResp
		expErr     error
	}{
		"bad payload": {
			req:    []byte{'b', 'a', 'd'},
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"not replica": {
			notReplica: true,
			expResp:    &srvpb.CheckListPoolResp{Status: int32(drpc.DaosMiscError)},
		},
		"success": {
			expResp: &srvpb.CheckListPoolResp{
				Pools: []*srvpb.CheckListPoolResp_OnePool{
					{
						Uuid:    testPool.PoolUUID.String(),
						Label:   testPool.PoolLabel,
						Svcreps: system.RanksToUint32(testPool.Replicas),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mod := mockSrvModule(t, log, 1)
			if tc.notReplica {
				mod.poolDB = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{})
			} else {
				if err := mod.poolDB.AddPoolService(testPool); err != nil {
					t.Fatal(err)
				}
			}

			ctx := context.Background()
			gotMsg, gotErr := mod.handleCheckerListPools(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotResp := new(srvpb.CheckListPoolResp)
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
		Replicas:  []system.Rank{0, 1, 2},
	}
	makeReqBytes := func(id, label string, replicas []system.Rank) []byte {
		req := &srvpb.CheckRegPoolReq{
			Uuid:    id,
			Label:   label,
			Svcreps: system.RanksToUint32(replicas),
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
		expResp    *srvpb.CheckRegPoolResp
		expErr     error
	}{
		"bad payload": {
			req:    []byte{'b', 'a', 'd'},
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"not replica": {
			req:        makeReqBytes(newUUID, "new", []system.Rank{0}),
			notReplica: true,
			expResp:    &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosMiscError)},
		},
		"bad uuid": {
			req:     makeReqBytes("ow", "new", []system.Rank{0}),
			expResp: &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosInvalidInput)},
		},
		"bad label": {
			req:     makeReqBytes(newUUID, newUUID, []system.Rank{0}),
			expResp: &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosInvalidInput)},
		},
		"empty label": {
			req:     makeReqBytes(newUUID, "", []system.Rank{0}),
			expResp: &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosInvalidInput)},
		},
		"zero svcreps": {
			req:     makeReqBytes(newUUID, "new", []system.Rank{}),
			expResp: &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosInvalidInput)},
		},
		"duplicate uuid": {
			req:     makeReqBytes(existingPool.PoolUUID.String(), "new", []system.Rank{0}),
			expResp: &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosExists)},
		},
		"duplicate label": {
			req:     makeReqBytes(newUUID, existingPool.PoolLabel, []system.Rank{0}),
			expResp: &srvpb.CheckRegPoolResp{Status: int32(drpc.DaosExists)},
		},
		"success": {
			req:     makeReqBytes(newUUID, "new", []system.Rank{0}),
			expResp: &srvpb.CheckRegPoolResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mod := mockSrvModule(t, log, 1)
			if tc.notReplica {
				mod.poolDB = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{})
			} else {
				if err := mod.poolDB.AddPoolService(existingPool); err != nil {
					t.Fatal(err)
				}
			}

			ctx := context.Background()
			gotMsg, gotErr := mod.handleCheckerRegisterPool(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotResp := new(srvpb.CheckRegPoolResp)
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
		Replicas:  []system.Rank{0, 1, 2},
	}
	makeReqBytes := func(id string) []byte {
		req := &srvpb.CheckDeregPoolReq{
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
		expResp    *srvpb.CheckDeregPoolResp
		expErr     error
	}{
		"bad payload": {
			req:    []byte{'b', 'a', 'd'},
			expErr: drpc.UnmarshalingPayloadFailure(),
		},
		"not replica": {
			req:        makeReqBytes(existingPool.PoolUUID.String()),
			notReplica: true,
			expResp:    &srvpb.CheckDeregPoolResp{Status: int32(drpc.DaosMiscError)},
		},
		"bad uuid": {
			req:     makeReqBytes("ow"),
			expResp: &srvpb.CheckDeregPoolResp{Status: int32(drpc.DaosInvalidInput)},
		},
		"unknown uuid": {
			req:     makeReqBytes(unkUUID),
			expResp: &srvpb.CheckDeregPoolResp{Status: int32(drpc.DaosNonexistant)},
		},
		"success": {
			req:     makeReqBytes(existingPool.PoolUUID.String()),
			expResp: &srvpb.CheckDeregPoolResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mod := mockSrvModule(t, log, 1)
			if tc.notReplica {
				mod.poolDB = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{})
			} else {
				if err := mod.poolDB.AddPoolService(existingPool); err != nil {
					t.Fatal(err)
				}
			}

			ctx := context.Background()
			gotMsg, gotErr := mod.handleCheckerDeregisterPool(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotResp := new(srvpb.CheckDeregPoolResp)
			if err := proto.Unmarshal(gotMsg, gotResp); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expResp, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected response (-want +got):\n%s", diff)
			}
		})
	}
}
