//
// (C) Copyright 2018-2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"context"
	"net"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestHasPort(t *testing.T) {
	tests := []struct {
		addr        string
		expectedHas bool
	}{
		{"localhost", false},
		{"localhost:10000", true},
		{"192.168.1.1", false},
		{"192.168.1.1:10000", true},
	}

	for _, test := range tests {
		if has := hasPort(test.addr); has != test.expectedHas {
			t.Errorf("hasPort(%q) = %v", test.addr, has)
		}
	}
}

func TestCheckMgmtSvcReplica(t *testing.T) {
	defaultPort := strconv.Itoa(NewConfiguration().ControlPort)

	tests := []struct {
		self              string
		accessPoints      []string
		expectedIsReplica bool
		expectedBootstrap bool
		expectedErr       bool
	}{
		// Specified self cases
		{"localhost:" + defaultPort, []string{"localhost:" + defaultPort}, true, true, false},
		{"localhost:" + defaultPort, []string{"localhost"}, true, true, false},
		{"localhost:" + defaultPort, []string{"192.168.1.111", "localhost"}, true, false, false},
		{"localhost:" + defaultPort, []string{"192.168.1.111", "192.168.1.112"}, false, false, false},

		// Unspecified self cases
		{"0.0.0.0:" + defaultPort, []string{"localhost:" + defaultPort}, true, true, false},
		{"0.0.0.0:" + defaultPort, []string{"localhost"}, true, true, false},
		{"0.0.0.0:" + defaultPort, []string{"192.168.1.111", "localhost"}, true, false, false},
		{"0.0.0.0:" + defaultPort, []string{"192.168.1.111", "192.168.1.112"}, false, false, false},
	}

	oldGetInterfaceAddrs := getInterfaceAddrs
	getInterfaceAddrs = func() ([]net.Addr, error) {
		// Sample interface addresses
		ifAddrs := []net.Addr{
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0x7f, 0x0, 0x0, 0x1},
				Mask: net.IPMask{0xff, 0x0, 0x0, 0x0},
			}, // 127.0.0.1/8
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
			}, // ::1/128
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::1/64
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0x58, 0xc, 0x69, 0xd0, 0xe3, 0xca, 0xdd},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::c58:c69:d0e3:cadd/64
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0xde, 0x69},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0x0},
			}, // 192.168.222.105/24
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xd0, 0x21, 0xb5, 0xff, 0xfe, 0xb2, 0x3c, 0xc9},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::d021:b5ff:feb2:3cc9/64
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xef, 0x8e, 0xb4, 0x97, 0xc4, 0x32, 0x38, 0x6e},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::ef8e:b497:c432:386e/64
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xa, 0xd3, 0x37, 0x2},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0x0},
			}, // 10.211.55.2/24
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xa, 0x25, 0x81, 0x2},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0x0},
			}, // 10.37.129.2/24
		}
		return ifAddrs, nil
	}
	defer func() { getInterfaceAddrs = oldGetInterfaceAddrs }()

	for i, test := range tests {
		ta, err := net.ResolveTCPAddr("tcp", test.self)
		if err != nil {
			t.Errorf("test.self: %v", err)
		}
		isReplica, bootstrap, err := checkMgmtSvcReplica(ta, test.accessPoints)
		if isReplica != test.expectedIsReplica ||
			bootstrap != test.expectedBootstrap ||
			(err != nil) != test.expectedErr {
			t.Errorf("test %d checkMgmtSvcReplica(%v, %v) = (%v, %v, %v), want (%v, %v, %v)",
				i, ta, test.accessPoints, isReplica, bootstrap, err != nil,
				test.expectedIsReplica, test.expectedBootstrap, test.expectedErr)
		}
	}
}

func newTestListPoolsReq() *mgmtpb.ListPoolsReq {
	return &mgmtpb.ListPoolsReq{
		Sys: "daos",
	}
}

func TestListPools_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil)

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("no managed instances"), err)
}

func TestListPools_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestPoolListPools_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	// dRPC call returns junk in the message body
	badBytes := make([]byte, 12)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestListPools_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)

	expectedResp := &mgmtpb.ListPoolsResp{
		Pools: []*mgmtpb.ListPoolsResp_Pool{
			{Uuid: "12345678-1234-1234-1234-123456789abc"},
			{Uuid: "87654321-4321-4321-4321-cba987654321"},
		},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestListContReq() *mgmtpb.ListContReq {
	return &mgmtpb.ListContReq{
		Uuid: "12345678-1234-1234-1234-123456789abc",
	}
}

func TestListCont_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("no managed instances"), err)
}

func TestListCont_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
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

	svc := newTestMgmtSvc(log)
	// dRPC call returns junk in the message body
	badBytes := make([]byte, 12)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

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

	svc := newTestMgmtSvc(log)

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

	svc := newTestMgmtSvc(log)

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

func newTestGetACLReq() *mgmtpb.GetACLReq {
	return &mgmtpb.GetACLReq{
		Uuid: "testUUID",
	}
}

func TestPoolGetACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("no managed instances"), err)
}

func TestPoolGetACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestPoolGetACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	// dRPC call returns junk in the message body
	badBytes := make([]byte, 12)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolGetACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)

	expectedResp := &mgmtpb.ACLResp{
		Status: 0,
		ACL:    []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestModifyACLReq() *mgmtpb.ModifyACLReq {
	return &mgmtpb.ModifyACLReq{
		Uuid: "testUUID",
		ACL: []string{
			"A::OWNER@:rw",
		},
	}
}

func TestPoolOverwriteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("no managed instances"), err)
}

func TestPoolOverwriteACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestPoolOverwriteACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	// dRPC call returns junk in the message body
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolOverwriteACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)

	expectedResp := &mgmtpb.ACLResp{
		Status: 0,
		ACL:    []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.PoolOverwriteACL(nil, newTestModifyACLReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestPoolUpdateACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("no managed instances"), err)
}

func TestPoolUpdateACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestPoolUpdateACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	// dRPC call returns junk in the message body
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolUpdateACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)

	expectedResp := &mgmtpb.ACLResp{
		Status: 0,
		ACL:    []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.PoolUpdateACL(nil, newTestModifyACLReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestDeleteACLReq() *mgmtpb.DeleteACLReq {
	return &mgmtpb.DeleteACLReq{
		Uuid:      "testUUID",
		Principal: "u:user@",
	}
}
func TestPoolDeleteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("no managed instances"), err)
}

func TestPoolDeleteACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, expectedErr, err)
}

func TestPoolDeleteACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
	// dRPC call returns junk in the message body
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolDeleteACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)

	expectedResp := &mgmtpb.ACLResp{
		Status: 0,
		ACL:    []string{"A::OWNER@:rw", "A:G:readers@:r"},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.PoolDeleteACL(nil, newTestDeleteACLReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestMgmtSvc_LeaderQuery(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil
	missingAPs := newTestMgmtSvc(nil)
	missingAPs.harness.instances[0].msClient.cfg.AccessPoints = nil

	for name, tc := range map[string]struct {
		mgmtSvc *mgmtSvc
		req     *mgmtpb.LeaderQueryReq
		expResp *mgmtpb.LeaderQueryResp
		expErr  error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req: &mgmtpb.LeaderQueryReq{
				System: "quack",
			},
			expErr: errors.New("wrong system"),
		},
		"no i/o servers": {
			mgmtSvc: newMgmtSvc(NewIOServerHarness(nil), nil),
			req:     &mgmtpb.LeaderQueryReq{},
			expErr:  errors.New("no I/O servers"),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.LeaderQueryReq{},
			expErr:  errors.New("no I/O superblock"),
		},
		"fail to get current leader address": {
			mgmtSvc: missingAPs,
			req:     &mgmtpb.LeaderQueryReq{},
			expErr:  errors.New("current leader address"),
		},
		"successful query": {
			req: &mgmtpb.LeaderQueryReq{},
			expResp: &mgmtpb.LeaderQueryResp{
				CurrentLeader: "localhost",
				Replicas:      []string{"localhost"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(log)
			}

			gotResp, gotErr := tc.mgmtSvc.LeaderQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
