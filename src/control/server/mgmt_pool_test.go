//
// (C) Copyright 2020 Intel Corporation.
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
)

func TestServer_MgmtSvc_PoolCreate(t *testing.T) {
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
				harness.started.SetTrue()

				tc.mgmtSvc = newMgmtSvc(harness, nil, nil)
			}
			tc.mgmtSvc.log = log

			if _, err := tc.mgmtSvc.harness.getMSLeaderInstance(); err == nil {
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

func TestServer_MgmtSvc_PoolDestroy(t *testing.T) {
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

			if _, err := tc.mgmtSvc.harness.getMSLeaderInstance(); err == nil {
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

func TestServer_MgmtSvc_PoolDrain(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(nil)
	notAP.harness.instances[0]._superblock.MS = false

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolDrainReq
		expResp       *mgmtpb.PoolDrainResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolDrainReq{Uuid: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolDrainReq{Uuid: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolDrainReq{Uuid: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			req:    &mgmtpb.PoolDrainReq{Uuid: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolDrainReq{Uuid: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolDrainReq{Rank: 2, Targetidx: []uint32{1, 2}},
			expErr: errors.New("nil UUID"),
		},
		"successful drained": {
			req:     &mgmtpb.PoolDrainReq{Uuid: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expResp: &mgmtpb.PoolDrainResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(log)
			}
			tc.mgmtSvc.log = log

			if _, err := tc.mgmtSvc.harness.getMSLeaderInstance(); err == nil {
				if tc.setupMockDrpc == nil {
					tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
						setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
					}
				}
				tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)
			}

			gotResp, gotErr := tc.mgmtSvc.PoolDrain(context.TODO(), tc.req)
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

func TestServer_MgmtSvc_PoolEvict(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(nil)
	notAP.harness.instances[0]._superblock.MS = false

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolEvictReq
		expResp       *mgmtpb.PoolEvictResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "daos"},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "daos"},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "daos"},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			req:    &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "daos"},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "daos"},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolEvictReq{Sys: "daos"},
			expErr: errors.New("nil UUID"),
		},
		"successful evicted": {
			req:     &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "daos"},
			expResp: &mgmtpb.PoolEvictResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(log)
			}
			tc.mgmtSvc.log = log

			if _, err := tc.mgmtSvc.harness.getMSLeaderInstance(); err == nil {
				if tc.setupMockDrpc == nil {
					tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
						setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
					}
				}
				tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)
			}

			gotResp, gotErr := tc.mgmtSvc.PoolEvict(context.TODO(), tc.req)
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

	h := NewIOServerHarness(log)
	h.started.SetTrue()
	svc := newMgmtSvc(h, nil, nil)

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

func newTestGetACLReq() *mgmtpb.GetACLReq {
	return &mgmtpb.GetACLReq{
		Uuid: "testUUID",
	}
}

func TestPoolGetACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newMgmtSvc(NewIOServerHarness(log), nil, nil)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
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

	svc := newMgmtSvc(NewIOServerHarness(log), nil, nil)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
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

	svc := newMgmtSvc(NewIOServerHarness(log), nil, nil)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
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

	svc := newMgmtSvc(NewIOServerHarness(log), nil, nil)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, FaultHarnessNotStarted, err)
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

func TestServer_MgmtSvc_PoolQuery(t *testing.T) {
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

			if _, err := tc.mgmtSvc.harness.getMSLeaderInstance(); err == nil {
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

func TestServer_MgmtSvc_PoolSetProp(t *testing.T) {
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
		mi, _ := svc.harness.getMSLeaderInstance()
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
