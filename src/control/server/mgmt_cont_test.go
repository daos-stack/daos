//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

const (
	mockUUID = "00000000-0000-0000-0000-000000000000"
)

func makeBadBytes(count int) (badBytes []byte) {
	badBytes = make([]byte, count)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}
	return
}

func testPoolService() *system.PoolService {
	return &system.PoolService{
		PoolLabel: "test-pool",
		PoolUUID:  uuid.MustParse(mockUUID),
		Replicas:  []system.Rank{0, 1, 2},
		State:     system.PoolServiceStateReady,
		Storage: &system.PoolServiceStorage{
			CreationRankStr: system.MustCreateRankSet("0-2").String(),
		},
	}
}

func TestMgmt_ListContainers(t *testing.T) {
	validListContReq := func() *mgmtpb.ListContReq {
		return &mgmtpb.ListContReq{
			Sys: build.DefaultSystemName,
			Id:  mockUUID,
		}
	}

	multiConts := []*mgmtpb.ListContResp_Cont{
		{Uuid: "56781234-5678-5678-5678-123456789abc"},
		{Uuid: "67812345-6781-6781-6781-123456789abc"},
		{Uuid: "78123456-7812-7812-7812-123456789abc"},
		{Uuid: "81234567-8123-8123-8123-123456789abc"},
	}

	for name, tc := range map[string]struct {
		createMS  func(*testing.T, logging.Logger) *mgmtSvc
		setupDrpc func(*testing.T, *mgmtSvc)
		req       *mgmtpb.ListContReq
		expResp   *mgmtpb.ListContResp
		expErr    error
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"pool svc not found": {
			req: &mgmtpb.ListContReq{
				Sys: build.DefaultSystemName,
				Id:  "fake",
			},
			expErr: errors.New("unable to find pool"),
		},
		"harness not started": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				db := raft.MockDatabase(t, log)
				ms := system.MockMembership(t, log, db, mockTCPResolver)
				return newMgmtSvc(NewEngineHarness(log), ms, db, nil,
					events.NewPubSub(context.Background(), log))
			},
			req:    validListContReq(),
			expErr: FaultHarnessNotStarted,
		},
		"drpc error": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				setupMockDrpcClient(svc, nil, errors.New("mock drpc"))
			},
			req:    validListContReq(),
			expErr: errors.New("mock drpc"),
		},
		"bad drpc resp": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				badBytes := makeBadBytes(16)
				setupMockDrpcClientBytes(svc, badBytes, nil)
			},
			req:    validListContReq(),
			expErr: errors.New("unmarshal"),
		},
		"success; zero containers": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				setupMockDrpcClient(svc, &mgmtpb.ListContResp{}, nil)
			},
			req:     validListContReq(),
			expResp: &mgmtpb.ListContResp{},
		},
		"success; multiple containers": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				setupMockDrpcClient(svc, &mgmtpb.ListContResp{
					Containers: multiConts,
				}, nil)
			},
			req: validListContReq(),
			expResp: &mgmtpb.ListContResp{
				Containers: multiConts,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.createMS == nil {
				tc.createMS = newTestMgmtSvc
			}
			svc := tc.createMS(t, log)
			addTestPoolService(t, svc.sysdb, testPoolService())

			if tc.setupDrpc != nil {
				tc.setupDrpc(t, svc)
			}

			resp, err := svc.ListContainers(context.TODO(), tc.req)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got): \n%s\n", diff)
			}
		})
	}
}

func TestMgmt_ContSetOwner(t *testing.T) {
	validContSetOwnerReq := func() *mgmtpb.ContSetOwnerReq {
		return &mgmtpb.ContSetOwnerReq{
			Sys:        build.DefaultSystemName,
			ContUUID:   "contUUID",
			PoolUUID:   mockUUID,
			Owneruser:  "user@",
			Ownergroup: "group@",
		}
	}

	for name, tc := range map[string]struct {
		createMS  func(*testing.T, logging.Logger) *mgmtSvc
		setupDrpc func(*testing.T, *mgmtSvc)
		req       *mgmtpb.ContSetOwnerReq
		expResp   *mgmtpb.ContSetOwnerResp
		expErr    error
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"pool svc not found": {
			req: &mgmtpb.ContSetOwnerReq{
				Sys:        build.DefaultSystemName,
				ContUUID:   "contUUID",
				PoolUUID:   "fake",
				Owneruser:  "user@",
				Ownergroup: "group@",
			},
			expErr: errors.New("unable to find pool"),
		},
		"harness not started": {
			createMS: func(t *testing.T, log logging.Logger) *mgmtSvc {
				db := raft.MockDatabase(t, log)
				ms := system.MockMembership(t, log, db, mockTCPResolver)
				return newMgmtSvc(NewEngineHarness(log), ms, db, nil,
					events.NewPubSub(context.Background(), log))
			},
			req:    validContSetOwnerReq(),
			expErr: FaultHarnessNotStarted,
		},
		"drpc error": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				setupMockDrpcClient(svc, nil, errors.New("mock drpc"))
			},
			req:    validContSetOwnerReq(),
			expErr: errors.New("mock drpc"),
		},
		"bad drpc resp": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				badBytes := makeBadBytes(16)
				setupMockDrpcClientBytes(svc, badBytes, nil)
			},
			req:    validContSetOwnerReq(),
			expErr: errors.New("unmarshal"),
		},
		"success": {
			setupDrpc: func(t *testing.T, svc *mgmtSvc) {
				setupMockDrpcClient(svc, &mgmtpb.ContSetOwnerResp{}, nil)
			},
			req: validContSetOwnerReq(),
			expResp: &mgmtpb.ContSetOwnerResp{
				Status: 0,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.createMS == nil {
				tc.createMS = newTestMgmtSvc
			}
			svc := tc.createMS(t, log)
			addTestPoolService(t, svc.sysdb, testPoolService())

			if tc.setupDrpc != nil {
				tc.setupDrpc(t, svc)
			}

			resp, err := svc.ContSetOwner(context.TODO(), tc.req)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got): \n%s\n", diff)
			}
		})
	}
}
