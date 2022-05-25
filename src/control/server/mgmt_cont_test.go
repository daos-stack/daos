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

func newTestListContReq() *mgmtpb.ListContReq {
	return &mgmtpb.ListContReq{
		Sys: build.DefaultSystemName,
		Id:  "12345678-1234-1234-1234-123456789abc",
	}
}

func TestListCont_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ms, db := system.MockMembership(t, log, mockTCPResolver)
	svc := newMgmtSvc(NewEngineHarness(log), ms, db, nil,
		events.NewPubSub(context.Background(), log))

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, FaultHarnessNotStarted, err)
}

func TestListCont_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, expectedErr, err)
}

func TestPoolListCont_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(12)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("unmarshal"), err)
}

func TestListCont_ZeroContSuccess(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)

	expectedResp := &mgmtpb.ListContResp{}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	if diff := cmp.Diff(expectedResp, resp, test.DefaultCmpOpts()...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestListCont_ManyContSuccess(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)

	expectedResp := &mgmtpb.ListContResp{
		Containers: []*mgmtpb.ListContResp_Cont{
			{Uuid: "56781234-5678-5678-5678-123456789abc"},
			{Uuid: "67812345-6781-6781-6781-123456789abc"},
			{Uuid: "78123456-7812-7812-7812-123456789abc"},
			{Uuid: "81234567-8123-8123-8123-123456789abc"},
		},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	if diff := cmp.Diff(expectedResp, resp, test.DefaultCmpOpts()...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
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

	testPoolService := &system.PoolService{
		PoolLabel: "test-pool",
		PoolUUID:  uuid.MustParse(mockUUID),
		Replicas:  []system.Rank{0, 1, 2},
		State:     system.PoolServiceStateReady,
		Storage: &system.PoolServiceStorage{
			CreationRankStr: system.MustCreateRankSet("0-2").String(),
		},
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
				ms, db := system.MockMembership(t, log, mockTCPResolver)
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
			addTestPoolService(t, svc.sysdb, testPoolService)

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
