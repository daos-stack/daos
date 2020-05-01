//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"fmt"
	"net"
	"strconv"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
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

func TestMgmtSvc_PoolCreate(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(nil)
	notAP.harness.instances[0]._superblock.MS = false

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		targetCount   int
		req           *mgmtpb.PoolCreateReq
		expResp       *mgmtpb.PoolCreateResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			mgmtSvc:     missingSB,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc:     notAP,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("not an access point"),
		},
		"dRPC send fails": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			targetCount: 0,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"retries exceed context deadline": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolCreateResp{
					Status: int32(drpc.DaosGroupVersionMismatch),
				}, nil)
			},
			expErr: context.DeadlineExceeded,
		},
		"successful creation": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expResp: &mgmtpb.PoolCreateResp{},
		},
		"successful creation minimum size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  ioserver.ScmMinBytesPerTarget * 8,
				Nvmebytes: ioserver.NvmeMinBytesPerTarget * 8,
			},
			expResp: &mgmtpb.PoolCreateResp{},
		},
		"failed creation scm too small": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  (ioserver.ScmMinBytesPerTarget * 8) - 1,
				Nvmebytes: ioserver.NvmeMinBytesPerTarget * 8,
			},
			expErr: FaultPoolScmTooSmall((ioserver.ScmMinBytesPerTarget*8)-1, 8),
		},
		"failed creation nvme too small": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Scmbytes:  ioserver.ScmMinBytesPerTarget * 8,
				Nvmebytes: (ioserver.NvmeMinBytesPerTarget * 8) - 1,
			},
			expErr: FaultPoolNvmeTooSmall((ioserver.NvmeMinBytesPerTarget*8)-1, 8),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				ioCfg := ioserver.NewConfig().WithTargetCount(tc.targetCount)
				r := ioserver.NewTestRunner(nil, ioCfg)

				var msCfg mgmtSvcClientCfg
				msCfg.AccessPoints = append(msCfg.AccessPoints, "localhost")

				srv := NewIOServerInstance(log, nil, nil, newMgmtSvcClient(context.TODO(), log, msCfg), r)
				srv.setSuperblock(&Superblock{MS: true})

				harness := NewIOServerHarness(log)
				if err := harness.AddInstance(srv); err != nil {
					panic(err)
				}

				tc.mgmtSvc = newMgmtSvc(harness, nil)
			}
			tc.mgmtSvc.log = log

			if _, err := tc.mgmtSvc.harness.GetMSLeaderInstance(); err == nil {
				if tc.setupMockDrpc == nil {
					tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
						setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
					}
				}
				tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)
			}

			ctx, cancel := context.WithTimeout(context.Background(), poolCreateRetryDelay+10*time.Millisecond)
			defer cancel()
			gotResp, gotErr := tc.mgmtSvc.PoolCreate(ctx, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_PoolDestroy(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(nil)
	notAP.harness.instances[0]._superblock.MS = false

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolDestroyReq
		expResp       *mgmtpb.PoolDestroyResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolDestroyReq{Uuid: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolDestroyReq{Uuid: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolDestroyReq{Uuid: mockUUID},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			req:    &mgmtpb.PoolDestroyReq{Uuid: mockUUID},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolDestroyReq{Uuid: mockUUID},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolDestroyReq{},
			expErr: errors.New("nil UUID"),
		},
		"successful destroy": {
			req:     &mgmtpb.PoolDestroyReq{Uuid: mockUUID},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(log)
			}
			tc.mgmtSvc.log = log

			if _, err := tc.mgmtSvc.harness.GetMSLeaderInstance(); err == nil {
				if tc.setupMockDrpc == nil {
					tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
						setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
					}
				}
				tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)
			}

			gotResp, gotErr := tc.mgmtSvc.PoolDestroy(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
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
	badBytes := makeBadBytes(12)

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
	badBytes := makeBadBytes(12)

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
	badBytes := makeBadBytes(16)

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
	badBytes := makeBadBytes(16)

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
	badBytes := makeBadBytes(16)

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

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

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

func TestMgmtSvc_PoolQuery(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolQueryReq
		expResp       *mgmtpb.PoolQueryResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req: &mgmtpb.PoolQueryReq{
				Uuid: mockUUID,
			},
			expErr: errors.New("not an access point"),
		},
		"dRPC send fails": {
			req: &mgmtpb.PoolQueryReq{
				Uuid: mockUUID,
			},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolQueryReq{
				Uuid: mockUUID,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"successful query": {
			req: &mgmtpb.PoolQueryReq{
				Uuid: mockUUID,
			},
			expResp: &mgmtpb.PoolQueryResp{
				Uuid: mockUUID,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(log)
			}
			tc.mgmtSvc.log = log

			if _, err := tc.mgmtSvc.harness.GetMSLeaderInstance(); err == nil {
				if tc.setupMockDrpc == nil {
					tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
						setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
					}
				}
				tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)
			}

			gotResp, gotErr := tc.mgmtSvc.PoolQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_PoolSetProp(t *testing.T) {
	withName := func(r *mgmtpb.PoolSetPropReq, n string) *mgmtpb.PoolSetPropReq {
		r.SetPropertyName(n)
		return r
	}
	withNumber := func(r *mgmtpb.PoolSetPropReq, n uint32) *mgmtpb.PoolSetPropReq {
		r.SetPropertyNumber(n)
		return r
	}
	withStrVal := func(r *mgmtpb.PoolSetPropReq, v string) *mgmtpb.PoolSetPropReq {
		r.SetValueString(v)
		return r
	}
	withNumVal := func(r *mgmtpb.PoolSetPropReq, v uint64) *mgmtpb.PoolSetPropReq {
		r.SetValueNumber(v)
		return r
	}
	lastCall := func(svc *mgmtSvc) *drpc.Call {
		mi, _ := svc.harness.GetMSLeaderInstance()
		if mi == nil || mi._drpcClient == nil {
			return nil
		}
		return mi._drpcClient.(*mockDrpcClient).SendMsgInputCall
	}

	for name, tc := range map[string]struct {
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolSetPropReq
		expReq        *mgmtpb.PoolSetPropReq
		drpcResp      *mgmtpb.PoolSetPropResp
		expResp       *mgmtpb.PoolSetPropResp
		expErr        error
	}{
		"garbage resp": {
			req: withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"unhandled property": {
			req:    withName(new(mgmtpb.PoolSetPropReq), "unknown"),
			expErr: errors.New("unhandled pool property"),
		},
		"response property mismatch": {
			req: withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: 4242424242,
				},
			},
			expErr: errors.New("Response number doesn't match"),
		},
		"response value mismatch": {
			req: withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertySpaceReclaim,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: 4242424242,
				},
			},
			expErr: errors.New("Response value doesn't match"),
		},
		"reclaim-unknown": {
			req:    withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "unknown"),
			expErr: errors.New("unhandled reclaim type"),
		},
		"reclaim-disabled": {
			req: withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			expReq: withNumVal(
				withNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySpaceReclaim),
				drpc.PoolSpaceReclaimDisabled,
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertySpaceReclaim,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: drpc.PoolSpaceReclaimDisabled,
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "reclaim",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "disabled",
				},
			},
		},
		"reclaim-lazy": {
			req: withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "lazy"),
			expReq: withNumVal(
				withNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySpaceReclaim),
				drpc.PoolSpaceReclaimLazy,
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertySpaceReclaim,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: drpc.PoolSpaceReclaimLazy,
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "reclaim",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "lazy",
				},
			},
		},
		"reclaim-time": {
			req: withStrVal(withName(new(mgmtpb.PoolSetPropReq), "reclaim"), "time"),
			expReq: withNumVal(
				withNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySpaceReclaim),
				drpc.PoolSpaceReclaimTime,
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertySpaceReclaim,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: drpc.PoolSpaceReclaimTime,
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "reclaim",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "time",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(log)
			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(svc, tc.drpcResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(ms, tc.expErr)

			gotResp, gotErr := ms.PoolSetProp(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			// Also verify that the string values are properly resolved to C identifiers.
			gotReq := new(mgmtpb.PoolSetPropReq)
			if err := proto.Unmarshal(lastCall(ms).Body, gotReq); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expReq, gotReq); diff != "" {
				t.Fatalf("unexpected dRPC call (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_SmdListDevs(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP   bool
		numIO     int
		req       *mgmtpb.SmdDevReq
		junkResp  bool
		drpcResps []proto.Message
		expResp   *mgmtpb.SmdDevResp
		expErr    error
	}{
		"dRPC send fails": {
			req: &mgmtpb.SmdDevReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdDevResp{},
			},
			expErr: errors.New("send failure"),
		},
		"dRPC resp fails": {
			req:      &mgmtpb.SmdDevReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"successful query (single instance)": {
			numIO: 1,
			req:   &mgmtpb.SmdDevReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdDevResp{
					Devices: []*mgmtpb.SmdDevResp_Device{
						{
							Uuid:   "test-uuid",
							TgtIds: []int32{0, 1, 2},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdDevResp{
				Devices: []*mgmtpb.SmdDevResp_Device{
					{
						Uuid:   "test-uuid",
						TgtIds: []int32{0, 1, 2},
					},
				},
			},
		},
		"successful query (dual instance)": {
			numIO: 2,
			req:   &mgmtpb.SmdDevReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdDevResp{
					Devices: []*mgmtpb.SmdDevResp_Device{
						{
							Uuid:   "test-uuid",
							TgtIds: []int32{0, 1, 2},
						},
					},
				},
				&mgmtpb.SmdDevResp{
					Devices: []*mgmtpb.SmdDevResp_Device{
						{
							Uuid:   "test-uuid2",
							TgtIds: []int32{3, 4, 5},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdDevResp{
				Devices: []*mgmtpb.SmdDevResp_Device{
					{
						Uuid:   "test-uuid",
						TgtIds: []int32{0, 1, 2},
					},
					{
						Uuid:   "test-uuid2",
						TgtIds: []int32{3, 4, 5},
					},
				},
			},
		},
		"failed query (dual instance)": {
			numIO: 2,
			req:   &mgmtpb.SmdDevReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdDevResp{
					Status: -1,
				},
				&mgmtpb.SmdDevResp{
					Devices: []*mgmtpb.SmdDevResp_Device{
						{
							Uuid:   "test-uuid2",
							TgtIds: []int32{3, 4, 5},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdDevResp{
				Status: -1,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			if tc.numIO > 0 {
				ioserverCount = tc.numIO
			}
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			gotResp, gotErr := svc.SmdListDevs(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_SmdListPools(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP   bool
		numIO     int
		req       *mgmtpb.SmdPoolReq
		junkResp  bool
		drpcResps []proto.Message
		expResp   *mgmtpb.SmdPoolResp
		expErr    error
	}{
		"dRPC send fails": {
			req: &mgmtpb.SmdPoolReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdPoolResp{},
			},
			expErr: errors.New("send failure"),
		},
		"dRPC resp fails": {
			req:      &mgmtpb.SmdPoolReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"successful query (single instance)": {
			numIO: 1,
			req:   &mgmtpb.SmdPoolReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdPoolResp{
					Pools: []*mgmtpb.SmdPoolResp_Pool{
						{
							Uuid:   "test-uuid",
							TgtIds: []int32{0, 1, 2},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdPoolResp{
				Pools: []*mgmtpb.SmdPoolResp_Pool{
					{
						Uuid:   "test-uuid",
						TgtIds: []int32{0, 1, 2},
					},
				},
			},
		},
		"successful query (dual instance)": {
			numIO: 2,
			req:   &mgmtpb.SmdPoolReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdPoolResp{
					Pools: []*mgmtpb.SmdPoolResp_Pool{
						{
							Uuid:   "test-uuid",
							TgtIds: []int32{0, 1, 2},
						},
					},
				},
				&mgmtpb.SmdPoolResp{
					Pools: []*mgmtpb.SmdPoolResp_Pool{
						{
							Uuid:   "test-uuid2",
							TgtIds: []int32{3, 4, 5},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdPoolResp{
				Pools: []*mgmtpb.SmdPoolResp_Pool{
					{
						Uuid:   "test-uuid",
						TgtIds: []int32{0, 1, 2},
					},
					{
						Uuid:   "test-uuid2",
						TgtIds: []int32{3, 4, 5},
					},
				},
			},
		},
		"failed query (dual instance)": {
			numIO: 2,
			req:   &mgmtpb.SmdPoolReq{},
			drpcResps: []proto.Message{
				&mgmtpb.SmdPoolResp{
					Status: -1,
				},
				&mgmtpb.SmdPoolResp{
					Pools: []*mgmtpb.SmdPoolResp_Pool{
						{
							Uuid:   "test-uuid2",
							TgtIds: []int32{3, 4, 5},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdPoolResp{
				Status: -1,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			if tc.numIO > 0 {
				ioserverCount = tc.numIO
			}
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			gotResp, gotErr := svc.SmdListPools(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_BioHealthQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP   bool
		numIO     int
		req       *mgmtpb.BioHealthReq
		junkResp  bool
		drpcResps []proto.Message
		expResp   *mgmtpb.BioHealthResp
		expErr    error
	}{
		"dRPC resp fails": {
			req:      &mgmtpb.BioHealthReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"successful query (single instance)": {
			numIO: 1,
			req:   &mgmtpb.BioHealthReq{},
			drpcResps: []proto.Message{
				&mgmtpb.BioHealthResp{
					DevUuid: "test-uuid",
				},
			},
			expResp: &mgmtpb.BioHealthResp{
				DevUuid: "test-uuid",
			},
		},
		"successful query (dual instance; first succeeds)": {
			numIO: 2,
			req:   &mgmtpb.BioHealthReq{},
			drpcResps: []proto.Message{
				&mgmtpb.BioHealthResp{
					DevUuid: "test-uuid",
				},
				&mgmtpb.BioHealthResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.BioHealthResp{
				DevUuid: "test-uuid",
			},
		},
		"successful query (dual instance; second succeeds)": {
			numIO: 2,
			req:   &mgmtpb.BioHealthReq{},
			drpcResps: []proto.Message{
				&mgmtpb.BioHealthResp{
					Status: -1,
				},
				&mgmtpb.BioHealthResp{
					DevUuid: "test-uuid",
				},
			},
			expResp: &mgmtpb.BioHealthResp{
				DevUuid: "test-uuid",
			},
		},
		"failed query (dual instance; uuid)": {
			numIO: 2,
			req:   &mgmtpb.BioHealthReq{DevUuid: "fnord"},
			drpcResps: []proto.Message{
				&mgmtpb.BioHealthResp{
					Status: -1,
				},
				&mgmtpb.BioHealthResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.BioHealthResp{
				Status: -1,
			},
			expErr: errors.New("no rank matched"),
		},
		"failed query (dual instance; tgt)": {
			numIO: 2,
			req:   &mgmtpb.BioHealthReq{TgtId: "banana"},
			drpcResps: []proto.Message{
				&mgmtpb.BioHealthResp{
					Status: -1,
				},
				&mgmtpb.BioHealthResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.BioHealthResp{
				Status: -1,
			},
			expErr: errors.New("no rank matched"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			if tc.numIO > 0 {
				ioserverCount = tc.numIO
			}
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			gotResp, gotErr := svc.BioHealthQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_DevStateQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP   bool
		numIO     int
		req       *mgmtpb.DevStateReq
		junkResp  bool
		drpcResps []proto.Message
		expResp   *mgmtpb.DevStateResp
		expErr    error
	}{
		"dRPC resp fails": {
			req:      &mgmtpb.DevStateReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"successful query (single instance)": {
			numIO: 1,
			req:   &mgmtpb.DevStateReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					DevUuid: "test-uuid",
				},
			},
			expResp: &mgmtpb.DevStateResp{
				DevUuid: "test-uuid",
			},
		},
		"successful query (dual instance; first succeeds)": {
			numIO: 2,
			req:   &mgmtpb.DevStateReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					DevUuid: "test-uuid",
				},
				&mgmtpb.DevStateResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.DevStateResp{
				DevUuid: "test-uuid",
			},
		},
		"successful query (dual instance; second succeeds)": {
			numIO: 2,
			req:   &mgmtpb.DevStateReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					Status: -1,
				},
				&mgmtpb.DevStateResp{
					DevUuid: "test-uuid",
				},
			},
			expResp: &mgmtpb.DevStateResp{
				DevUuid: "test-uuid",
			},
		},
		"failed query (dual instance)": {
			numIO: 2,
			req:   &mgmtpb.DevStateReq{DevUuid: "fnord"},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					Status: -1,
				},
				&mgmtpb.DevStateResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.DevStateResp{
				Status: -1,
			},
			expErr: errors.New("no rank matched"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			if tc.numIO > 0 {
				ioserverCount = tc.numIO
			}
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			gotResp, gotErr := svc.DevStateQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_StorageSetFaulty(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP   bool
		numIO     int
		req       *mgmtpb.DevStateReq
		junkResp  bool
		drpcResps []proto.Message
		expResp   *mgmtpb.DevStateResp
		expErr    error
	}{
		"dRPC resp fails": {
			req:      &mgmtpb.DevStateReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"successful query (single instance)": {
			numIO: 1,
			req:   &mgmtpb.DevStateReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					DevUuid: "test-uuid",
				},
			},
			expResp: &mgmtpb.DevStateResp{
				DevUuid: "test-uuid",
			},
		},
		"successful query (dual instance; first succeeds)": {
			numIO: 2,
			req:   &mgmtpb.DevStateReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					DevUuid: "test-uuid",
				},
				&mgmtpb.DevStateResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.DevStateResp{
				DevUuid: "test-uuid",
			},
		},
		"successful query (dual instance; second succeeds)": {
			numIO: 2,
			req:   &mgmtpb.DevStateReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					Status: -1,
				},
				&mgmtpb.DevStateResp{
					DevUuid: "test-uuid",
				},
			},
			expResp: &mgmtpb.DevStateResp{
				DevUuid: "test-uuid",
			},
		},
		"failed query (dual instance)": {
			numIO: 2,
			req:   &mgmtpb.DevStateReq{DevUuid: "fnord"},
			drpcResps: []proto.Message{
				&mgmtpb.DevStateResp{
					Status: -1,
				},
				&mgmtpb.DevStateResp{
					Status: -1,
				},
			},
			expResp: &mgmtpb.DevStateResp{
				Status: -1,
			},
			expErr: errors.New("no rank matched"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			if tc.numIO > 0 {
				ioserverCount = tc.numIO
			}
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			gotResp, gotErr := svc.StorageSetFaulty(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_DrespToRankResult(t *testing.T) {
	dRank := uint32(1)
	dStateGood := system.MemberStateStarted
	dStateBad := system.MemberStateErrored

	for name, tc := range map[string]struct {
		daosResp    *mgmtpb.DaosResp
		inErr       error
		targetState system.MemberState
		junkRpc     bool
		expResult   *mgmtpb.RanksResp_RankResult
	}{
		"rank success": {
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank, Action: "test", State: uint32(dStateGood),
			},
		},
		"rank failure": {
			daosResp: &mgmtpb.DaosResp{Status: -1},
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank, Action: "test", State: uint32(dStateBad), Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC returned DER -1", dRank),
			},
		},
		"drpc failure": {
			inErr: errors.New("returned from CallDrpc"),
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank, Action: "test", State: uint32(dStateBad), Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC failed: returned from CallDrpc", dRank),
			},
		},
		"unmarshal failure": {
			junkRpc: true,
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank, Action: "test", State: uint32(dStateBad), Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC unmarshal failed: proto: mgmt.DaosResp: illegal tag 0 (wire type 0)", dRank),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.daosResp == nil {
				tc.daosResp = &mgmtpb.DaosResp{Status: 0}
			}
			if tc.targetState == system.MemberStateUnknown {
				tc.targetState = dStateGood
			}

			// convert input DaosResp to drpcResponse to test
			rb := makeBadBytes(42)
			if !tc.junkRpc {
				rb, _ = proto.Marshal(tc.daosResp)
			}
			resp := &drpc.Response{
				Status: drpc.Status_SUCCESS, // this will already have been validated by CallDrpc
				Body:   rb,
			}

			gotResult := drespToRankResult(dRank, "test", resp, tc.inErr, tc.targetState)
			if diff := cmp.Diff(tc.expResult, gotResult, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_PrepShutdownRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB: true,
			req:       &mgmtpb.RanksReq{},
			expErr:    errors.New("instance 0 has no superblock"),
		},
		"instances stopped": {
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: 3},
					{Rank: 2, Action: "prep shutdown", State: 3},
				},
			},
		},
		"dRPC resp fails": {
			req:     &mgmtpb.RanksReq{},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: 5, Errored: true},
					{Rank: 2, Action: "prep shutdown", State: 5, Errored: true},
				},
			},
		},
		"dRPC resp junk": {
			req:      &mgmtpb.RanksReq{},
			junkResp: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: 5, Errored: true},
					{Rank: 2, Action: "prep shutdown", State: 5, Errored: true},
				},
			},
		},
		"unsuccessful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: 5, Errored: true},
					{Rank: 2, Action: "prep shutdown", State: 5, Errored: true},
				},
			},
		},
		"successful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: 2},
					{Rank: 2, Action: "prep shutdown", State: 2},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}
				if tc.instancesStopped { // real runner reports not started
					srv.runner = ioserver.NewRunner(log,
						ioserver.NewConfig())
				}

				srv._superblock.Rank = new(ioserver.Rank)
				*srv._superblock.Rank = ioserver.Rank(i + 1)

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.PrepShutdownRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestMgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				if path.Last().String() == ".Msg" {
					return true
				}
				return false
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResp, gotResp, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_StopRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB: true,
			req:       &mgmtpb.RanksReq{},
			expErr:    errors.New("instance 0 has no superblock"),
		},
		"dRPC resp fails": { // doesn't effect result, err logged
			req:     &mgmtpb.RanksReq{},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: 1, Errored: true},
					{Rank: 2, Action: "stop", State: 1, Errored: true},
				},
			},
		},
		"dRPC resp junk": { // doesn't effect result, err logged
			req:      &mgmtpb.RanksReq{},
			junkResp: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: 1, Errored: true},
					{Rank: 2, Action: "stop", State: 1, Errored: true},
				},
			},
		},
		"unsuccessful call": { // doesn't effect result, err logged
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: 1, Errored: true},
					{Rank: 2, Action: "stop", State: 1, Errored: true},
				},
			},
		},
		"instances started": { // unsuccessful result for kill
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: 1, Errored: true},
					{Rank: 2, Action: "stop", State: 1, Errored: true},
				},
			},
		},
		"instances stopped": { // successful result for kill
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: 3},
					{Rank: 2, Action: "stop", State: 3},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}
				if tc.instancesStopped { // real runner reports not started
					srv.runner = ioserver.NewRunner(log,
						ioserver.NewConfig())
				}

				srv._superblock.Rank = new(ioserver.Rank)
				*srv._superblock.Rank = ioserver.Rank(i + 1)

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.StopRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestMgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				if path.Last().String() == ".Msg" {
					return true
				}
				return false
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResp, gotResp, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_PingRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		responseDelay    time.Duration
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB: true,
			req:       &mgmtpb.RanksReq{},
			expErr:    errors.New("instance 0 has no superblock"),
		},
		"instances stopped": {
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: 3},
					{Rank: 2, Action: "ping", State: 3},
				},
			},
		},
		"dRPC resp fails": {
			req:     &mgmtpb.RanksReq{},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: 5, Errored: true},
					{Rank: 2, Action: "ping", State: 5, Errored: true},
				},
			},
		},
		"dRPC resp junk": {
			req:      &mgmtpb.RanksReq{},
			junkResp: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: 5, Errored: true},
					{Rank: 2, Action: "ping", State: 5, Errored: true},
				},
			},
		},
		"unsuccessful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: 5, Errored: true},
					{Rank: 2, Action: "ping", State: 5, Errored: true},
				},
			},
		},
		"successful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: 1},
					{Rank: 2, Action: "ping", State: 1},
				},
			},
		},
		"ping timeout": {
			req:           &mgmtpb.RanksReq{},
			responseDelay: 200 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: 6, Errored: true},
					{Rank: 2, Action: "ping", State: 6, Errored: true},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}
				if tc.instancesStopped { // real runner reports not started
					srv.runner = ioserver.NewRunner(log,
						ioserver.NewConfig())
				}

				srv._superblock.Rank = new(ioserver.Rank)
				*srv._superblock.Rank = ioserver.Rank(i + 1)

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)

					if tc.responseDelay != time.Duration(0) {
						cfg.setResponseDelay(tc.responseDelay)
					}
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.PingRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestMgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				if path.Last().String() == ".Msg" {
					return true
				}
				return false
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResp, gotResp, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_StartRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB:        true,
			instancesStopped: true,
			req:              &mgmtpb.RanksReq{},
			expErr:           errors.New("instance 0 has no superblock"),
		},
		"instances started": {
			req:    &mgmtpb.RanksReq{},
			expErr: errors.New("can't start instances: already started"),
		},
		"instances stopped": { // unsuccessful result for kill
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{
						Rank: 1, Action: "start", State: 3,
						Errored: true, Msg: "want Started, got Stopped",
					},
					{
						Rank: 2, Action: "start", State: 3,
						Errored: true, Msg: "want Started, got Stopped",
					},
				},
			},
		},
		// TODO: test instance state changing to started after restart
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, false)

			svc.harness.setStarted()
			svc.harness.setRestartable()

			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}
				if tc.instancesStopped { // real runner reports not started
					srv.runner = ioserver.NewRunner(log,
						ioserver.NewConfig())
				}

				srv._superblock.Rank = new(ioserver.Rank)
				*srv._superblock.Rank = ioserver.Rank(i + 1)
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.StartRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
