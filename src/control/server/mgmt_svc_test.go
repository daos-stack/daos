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
	"net"
	"strconv"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
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

func newTestListContReq() *mgmtpb.ListContReq {
	return &mgmtpb.ListContReq{
		Uuid: "12345678-1234-1234-1234-123456789abc",
	}
}

func TestListCont_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil, nil)

	resp, err := svc.ListContainers(context.TODO(), newTestListContReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
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

func TestServer_MgmtSvc_SmdListDevs(t *testing.T) {
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

func TestServer_MgmtSvc_SmdListPools(t *testing.T) {
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

func TestServer_MgmtSvc_BioHealthQuery(t *testing.T) {
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

func TestServer_MgmtSvc_DevStateQuery(t *testing.T) {
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

func TestServer_MgmtSvc_StorageSetFaulty(t *testing.T) {
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

func newTestContSetOwnerReq() *mgmtpb.ContSetOwnerReq {
	return &mgmtpb.ContSetOwnerReq{
		ContUUID:   "contUUID",
		PoolUUID:   "poolUUID",
		Owneruser:  "user@",
		Ownergroup: "group@",
	}
}

func TestContSetOwner_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil, nil)

	resp, err := svc.ContSetOwner(context.TODO(), newTestContSetOwnerReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
}

func TestContSetOwner_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(log)
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

	svc := newTestMgmtSvc(log)
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

	svc := newTestMgmtSvc(log)

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
