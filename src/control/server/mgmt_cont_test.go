//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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
		Sys:  build.DefaultSystemName,
		Uuid: "12345678-1234-1234-1234-123456789abc",
	}
}

func TestListCont_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ms, db := system.MockMembership(t, log, mockTCPResolver)
	svc := newMgmtSvc(NewIOServerHarness(log), ms, db, nil,
		events.NewPubSub(context.Background(), log))

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
}

func TestListCont_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestPoolListCont_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(12)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestListCont_ZeroContSuccess(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)

	expectedResp := &mgmtpb.ListContResp{}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestListCont_ManyContSuccess(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

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

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestContSetOwnerReq() *mgmtpb.ContSetOwnerReq {
	return &mgmtpb.ContSetOwnerReq{
		Sys:        build.DefaultSystemName,
		ContUUID:   "contUUID",
		PoolUUID:   "poolUUID",
		Owneruser:  "user@",
		Ownergroup: "group@",
	}
}

func TestContSetOwner_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ms, db := system.MockMembership(t, log, mockTCPResolver)
	svc := newMgmtSvc(NewIOServerHarness(log), ms, db, nil,
		events.NewPubSub(context.Background(), log))

	resp, err := svc.ContSetOwner(context.TODO(), newTestContSetOwnerReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
}

func TestContSetOwner_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.ContSetOwner(context.TODO(), newTestContSetOwnerReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestContSetOwner_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(16)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.ContSetOwner(context.TODO(), newTestContSetOwnerReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestContSetOwner_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)

	expectedResp := &mgmtpb.ContSetOwnerResp{
		Status: 0,
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.ContSetOwner(context.TODO(), newTestContSetOwnerReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}
