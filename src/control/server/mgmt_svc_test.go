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
