//
// (C) Copyright 2020-2021 Intel Corporation.
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
	"github.com/google/go-cmp/cmp/cmpopts"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

func addTestPoolService(t *testing.T, sysdb *system.Database, ps *system.PoolService) {
	t.Helper()

	for _, r := range ps.Replicas {
		_, err := sysdb.FindMemberByRank(r)
		if err == nil {
			continue
		}
		if !system.IsMemberNotFound(err) {
			t.Fatal(err)
		}

		if err := sysdb.AddMember(system.MockMember(t, 0, system.MemberStateJoined)); err != nil {
			t.Fatal(err)
		}
	}

	if err := sysdb.AddPoolService(ps); err != nil {
		t.Fatal(err)
	}
}

func addTestPools(t *testing.T, sysdb *system.Database, poolUUIDs ...string) {
	t.Helper()

	for _, uuidStr := range poolUUIDs {
		addTestPoolService(t, sysdb, &system.PoolService{
			PoolUUID: uuid.MustParse(uuidStr),
			State:    system.PoolServiceStateReady,
			Replicas: []system.Rank{0},
		})
	}
}

func TestServer_MgmtSvc_PoolCreateAlreadyExists(t *testing.T) {
	for name, tc := range map[string]struct {
		state   system.PoolServiceState
		expResp *mgmtpb.PoolCreateResp
	}{
		"creating": {
			state: system.PoolServiceStateCreating,
			expResp: &mgmtpb.PoolCreateResp{
				Status: int32(drpc.DaosTryAgain),
			},
		},
		"ready": {
			state: system.PoolServiceStateReady,
			expResp: &mgmtpb.PoolCreateResp{
				Status: int32(drpc.DaosAlready),
			},
		},
		"destroying": {
			state: system.PoolServiceStateDestroying,
			expResp: &mgmtpb.PoolCreateResp{
				Status: int32(drpc.DaosAlready),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)
			if err := svc.sysdb.AddPoolService(&system.PoolService{
				PoolUUID: uuid.MustParse(common.MockUUID(0)),
				State:    tc.state,
			}); err != nil {
				t.Fatal(err)
			}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			req := &mgmtpb.PoolCreateReq{
				Sys:      build.DefaultSystemName,
				Uuid:     common.MockUUID(0),
				Scmbytes: ioserver.ScmMinBytesPerTarget,
			}

			gotResp, err := svc.PoolCreate(ctx, req)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_calculateCreateStorage(t *testing.T) {
	defaultTotal := uint64(10 * humanize.TByte)
	defaultRatio := 0.06
	defaultScmBytes := uint64(float64(defaultTotal) * defaultRatio)
	defaultNvmeBytes := defaultTotal
	testTargetCount := 8
	scmTooSmallRatio := 0.01
	scmTooSmallTotal := uint64(testTargetCount * ioserver.NvmeMinBytesPerTarget)
	scmTooSmallReq := uint64(float64(scmTooSmallTotal) * scmTooSmallRatio)
	nvmeTooSmallTotal := uint64(3 * humanize.GByte)
	nvmeTooSmallReq := nvmeTooSmallTotal

	for name, tc := range map[string]struct {
		disableNVMe bool
		in          *mgmtpb.PoolCreateReq
		expOut      *mgmtpb.PoolCreateReq
		expErr      error
	}{
		"auto sizing": {
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: defaultTotal,
				Scmratio:   defaultRatio,
				Ranks:      []uint32{0, 1},
			},
			expOut: &mgmtpb.PoolCreateReq{
				Scmbytes:  defaultScmBytes / 2,
				Nvmebytes: defaultNvmeBytes / 2,
				Ranks:     []uint32{0, 1},
			},
		},
		"auto sizing (not enough SCM)": {
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: scmTooSmallTotal,
				Scmratio:   scmTooSmallRatio,
				Ranks:      []uint32{0},
			},
			expErr: FaultPoolScmTooSmall(scmTooSmallReq, testTargetCount),
		},
		"auto sizing (not enough NVMe)": {
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: nvmeTooSmallTotal,
				Scmratio:   defaultRatio,
				Ranks:      []uint32{0},
			},
			expErr: FaultPoolNvmeTooSmall(nvmeTooSmallReq, testTargetCount),
		},
		"auto sizing (no NVMe in config)": {
			disableNVMe: true,
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: defaultTotal,
				Scmratio:   defaultRatio,
				Ranks:      []uint32{0},
			},
			expOut: &mgmtpb.PoolCreateReq{
				Scmbytes: defaultTotal,
				Ranks:    []uint32{0},
			},
		},
		"manual sizing": {
			in: &mgmtpb.PoolCreateReq{
				Scmbytes:  defaultScmBytes - 1,
				Nvmebytes: defaultNvmeBytes - 1,
				Ranks:     []uint32{0, 1},
			},
			expOut: &mgmtpb.PoolCreateReq{
				Scmbytes:  defaultScmBytes - 1,
				Nvmebytes: defaultNvmeBytes - 1,
				Ranks:     []uint32{0, 1},
			},
		},
		"manual sizing (not enough SCM)": {
			in: &mgmtpb.PoolCreateReq{
				Scmbytes: scmTooSmallReq,
				Ranks:    []uint32{0},
			},
			expErr: FaultPoolScmTooSmall(scmTooSmallReq, testTargetCount),
		},
		"manual sizing (not enough NVMe)": {
			in: &mgmtpb.PoolCreateReq{
				Scmbytes:  defaultScmBytes,
				Nvmebytes: nvmeTooSmallReq,
				Ranks:     []uint32{0},
			},
			expErr: FaultPoolNvmeTooSmall(nvmeTooSmallReq, testTargetCount),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			srvCfg := ioserver.NewConfig().WithTargetCount(testTargetCount)
			if !tc.disableNVMe {
				srvCfg = srvCfg.
					WithBdevClass("nvme").
					WithBdevDeviceList("foo", "bar")
			}
			svc := newTestMgmtSvc(t, log)
			svc.harness.instances[0] = newTestIOServer(log, false, srvCfg)

			gotErr := svc.calculateCreateStorage(tc.in)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, tc.in); diff != "" {
				t.Fatalf("unexpected req (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolCreate(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)

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
		"wrong system": {
			req:    &mgmtpb.PoolCreateReq{Uuid: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc:     missingSB,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc:     notAP,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("not an access point"),
		},
		"dRPC send fails": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			targetCount: 0,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
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
		"successful creation": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
			},
			expResp: &mgmtpb.PoolCreateResp{
				ScmBytes:  (100 * humanize.GiByte),
				NvmeBytes: (10 * humanize.TByte),
				TgtRanks:  []uint32{0, 1},
			},
		},
		"successful creation minimum size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  ioserver.ScmMinBytesPerTarget * 8,
				Nvmebytes: ioserver.NvmeMinBytesPerTarget * 8,
			},
			expResp: &mgmtpb.PoolCreateResp{
				ScmBytes:  (ioserver.ScmMinBytesPerTarget * 8),
				NvmeBytes: (ioserver.NvmeMinBytesPerTarget * 8),
				TgtRanks:  []uint32{0, 1},
			},
		},
		"successful creation auto size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       common.MockUUID(0),
				Totalbytes: 100 * humanize.GiByte,
			},
			expResp: &mgmtpb.PoolCreateResp{
				ScmBytes:  ((100 * humanize.GiByte) * DefaultPoolScmRatio) / 2,
				NvmeBytes: (100 * humanize.GiByte) / 2,
				TgtRanks:  []uint32{0, 1},
			},
		},
		"failed creation invalid ranks": {
			targetCount: 1,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      common.MockUUID(0),
				Scmbytes:  100 * humanize.GiByte,
				Nvmebytes: 10 * humanize.TByte,
				Ranks:     []uint32{40, 11},
			},
			expErr: FaultPoolInvalidRanks([]system.Rank{11, 40}),
		},
		"too many svc replicas": {
			targetCount: 1,
			req: &mgmt.PoolCreateReq{
				Uuid:       common.MockUUID(0),
				Totalbytes: 100 * humanize.GByte,
				Scmratio:   0.06,
				Numsvcreps: MaxPoolServiceReps + 2,
			},
			expErr: FaultPoolInvalidServiceReps,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			if tc.mgmtSvc == nil {
				ioCfg := ioserver.NewConfig().
					WithTargetCount(tc.targetCount).
					WithBdevClass("nvme").
					WithBdevDeviceList("foo", "bar")
				r := ioserver.NewTestRunner(nil, ioCfg)
				if err := r.Start(ctx, make(chan<- error)); err != nil {
					t.Fatal(err)
				}

				srv := NewIOServerInstance(log, nil, nil, nil, r)
				srv.ready.SetTrue()

				harness := NewIOServerHarness(log)
				if err := harness.AddInstance(srv); err != nil {
					panic(err)
				}
				harness.started.SetTrue()

				ms, db := system.MockMembership(t, log, mockTCPResolver)
				tc.mgmtSvc = newMgmtSvc(harness, ms, db, nil,
					events.NewPubSub(context.Background(), log))
			}
			tc.mgmtSvc.log = log
			for i := 0; i < 2; i++ {
				if _, err := tc.mgmtSvc.membership.Add(system.MockMember(t, uint32(i), system.MemberStateJoined)); err != nil {
					t.Fatal(err)
				}
			}

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			pcCtx, pcCancel := context.WithTimeout(context.Background(), defaultRetryAfter+10*time.Millisecond)
			defer pcCancel()
			gotResp, gotErr := tc.mgmtSvc.PoolCreate(pcCtx, tc.req)
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

func TestServer_MgmtSvc_PoolCreateDownRanks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	mgmtSvc := newTestMgmtSvc(t, log)
	mgmtSvc.harness.instances[0] = newTestIOServer(log, false,
		ioserver.NewConfig().
			WithTargetCount(1).
			WithBdevClass("nvme").
			WithBdevDeviceList("foo", "bar"),
	)

	dc := newMockDrpcClient(&mockDrpcClientConfig{IsConnectedBool: true})
	dc.cfg.setSendMsgResponse(drpc.Status_SUCCESS, nil, nil)
	mgmtSvc.harness.instances[0]._drpcClient = dc

	for _, m := range []*system.Member{
		system.MockMember(t, 0, system.MemberStateJoined),
		system.MockMember(t, 1, system.MemberStateStopped),
		system.MockMember(t, 2, system.MemberStateJoined),
		system.MockMember(t, 3, system.MemberStateJoined),
	} {
		if err := mgmtSvc.sysdb.AddMember(m); err != nil {
			t.Fatal(err)
		}
	}

	req := &mgmtpb.PoolCreateReq{
		Sys:          build.DefaultSystemName,
		Uuid:         common.MockUUID(),
		Scmbytes:     100 * humanize.GiByte,
		Nvmebytes:    10 * humanize.TByte,
		FaultDomains: mgmtSvc.sysdb.FaultDomainTree().ToProto(),
	}
	wantReq := new(mgmtpb.PoolCreateReq)
	*wantReq = *req
	wantReq.Numsvcreps = DefaultPoolServiceReps

	_, err := mgmtSvc.PoolCreate(ctx, req)
	if err != nil {
		t.Fatal(err)
	}

	// We should only be trying to create on the Joined ranks.
	wantReq.Ranks = []uint32{0, 2, 3}

	gotReq := new(mgmtpb.PoolCreateReq)
	if err := proto.Unmarshal(dc.calls[0].Body, gotReq); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(wantReq, gotReq, common.DefaultCmpOpts()...); diff != "" {
		t.Fatalf("unexpected pool create req (-want, +got):\n%s\n", diff)
	}
}

func TestServer_MgmtSvc_PoolDestroy(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
	}

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
		"wrong system": {
			req:    &mgmtpb.PoolDestroyReq{Uuid: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
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
			expErr: errors.New("invalid UUID"),
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
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			tc.mgmtSvc.log = log
			if err := tc.mgmtSvc.sysdb.AddPoolService(testPoolService); err != nil {
				t.Fatal(err)
			}

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
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
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []system.Rank{0},
	}

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
		"wrong system": {
			req:    &mgmtpb.PoolDrainReq{Uuid: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
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
			expErr: errors.New("invalid UUID"),
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
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			tc.mgmtSvc.log = log
			addTestPoolService(t, tc.mgmtSvc.sysdb, testPoolService)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
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
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0]._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []system.Rank{0},
	}

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
		"wrong system": {
			req:    &mgmtpb.PoolEvictReq{Uuid: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolEvictReq{Uuid: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolEvictReq{Uuid: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolEvictReq{Uuid: mockUUID},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolEvictReq{Uuid: mockUUID},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolEvictReq{},
			expErr: errors.New("invalid UUID"),
		},
		"successful evicted": {
			req:     &mgmtpb.PoolEvictReq{Uuid: mockUUID},
			expResp: &mgmtpb.PoolEvictResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			tc.mgmtSvc.log = log
			addTestPoolService(t, tc.mgmtSvc.sysdb, testPoolService)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
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
		Sys: build.DefaultSystemName,
	}
}

func TestListPools_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	h := NewIOServerHarness(log)
	h.started.SetTrue()
	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("replica"), err)
}

func TestListPools_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	testPools := []*system.PoolService{
		{
			PoolUUID: uuid.MustParse(common.MockUUID(0)),
			State:    system.PoolServiceStateReady,
			Replicas: []system.Rank{0, 1, 2},
		},
		{
			PoolUUID: uuid.MustParse(common.MockUUID(1)),
			State:    system.PoolServiceStateReady,
			Replicas: []system.Rank{0, 1, 2},
		},
	}
	expectedResp := new(mgmtpb.ListPoolsResp)

	svc := newTestMgmtSvc(t, log)
	for _, ps := range testPools {
		if err := svc.sysdb.AddPoolService(ps); err != nil {
			t.Fatal(err)
		}
		expectedResp.Pools = append(expectedResp.Pools, &mgmtpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			SvcReps: []uint32{0, 1, 2},
		})
	}

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := common.DefaultCmpOpts()
	cmpOpts = append(cmpOpts,
		cmpopts.SortSlices(func(a, b *mgmtpb.ListPoolsResp_Pool) bool {
			return a.GetUuid() < b.GetUuid()
		}),
	)
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestGetACLReq() *mgmtpb.GetACLReq {
	return &mgmtpb.GetACLReq{
		Sys:  build.DefaultSystemName,
		Uuid: mockUUID,
	}
}

func TestPoolGetACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("replica"), err)
}

func TestPoolGetACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)

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

func TestPoolGetACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(12)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("unmarshal"), err)
}

func newTestModifyACLReq() *mgmtpb.ModifyACLReq {
	return &mgmtpb.ModifyACLReq{
		Sys:  build.DefaultSystemName,
		Uuid: mockUUID,
		ACL: []string{
			"A::OWNER@:rw",
		},
	}
}

func TestPoolOverwriteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("replica"), err)
}

func TestPoolOverwriteACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)

	expectedResp := &mgmtpb.ACLResp{
		Status: 0,
		ACL:    []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

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

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("replica"), err)
}

func TestPoolUpdateACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)

	expectedResp := &mgmtpb.ACLResp{
		Status: 0,
		ACL:    []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
	}
	setupMockDrpcClient(svc, expectedResp, nil)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

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
		Sys:       build.DefaultSystemName,
		Uuid:      mockUUID,
		Principal: "u:user@",
	}
}

func TestPoolDeleteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	common.CmpErr(t, errors.New("replica"), err)
}

func TestPoolDeleteACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
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

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)

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
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0]._superblock = nil

	allRanksDown := newTestMgmtSvc(t, testLog)
	downRanksPool := common.MockUUID(9)
	addTestPools(t, allRanksDown.sysdb, downRanksPool)
	if err := allRanksDown.membership.UpdateMemberStates(system.MemberResults{
		&system.MemberResult{
			Rank:  0,
			State: system.MemberStateStopped,
		},
	}, true); err != nil {
		t.Fatal(err)
	}

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
		"wrong system": {
			req:    &mgmtpb.PoolQueryReq{Uuid: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
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
		"no ranks available": {
			mgmtSvc: allRanksDown,
			req: &mgmtpb.PoolQueryReq{
				Uuid: downRanksPool,
			},
			expErr: errors.New("available"),
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
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			tc.mgmtSvc.log = log
			addTestPools(t, tc.mgmtSvc.sysdb, mockUUID)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(tc.mgmtSvc, tc.expResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
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

func TestServer_MgmtSvc_PoolResolveID(t *testing.T) {
	defaultLabel := "test-pool"

	for name, tc := range map[string]struct {
		req     *mgmtpb.PoolResolveIDReq
		expResp *mgmtpb.PoolResolveIDResp
		expErr  error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolResolveIDReq{Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"empty request": {
			req:    &mgmtpb.PoolResolveIDReq{},
			expErr: errors.New("empty request"),
		},
		"invalid label lookup": {
			req: &mgmtpb.PoolResolveIDReq{
				HumanID: "nope-bad-not-gonna-work",
			},
			expErr: errors.New("unable to find pool service"),
		},
		"valid label lookup": {
			req: &mgmtpb.PoolResolveIDReq{
				HumanID: defaultLabel,
			},
			expResp: &mgmtpb.PoolResolveIDResp{
				Uuid: mockUUID,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)
			ps, err := ms.sysdb.FindPoolServiceByUUID(uuid.MustParse(mockUUID))
			if err != nil {
				t.Fatal(err)
			}
			ps.PoolLabel = defaultLabel
			if err := ms.sysdb.UpdatePoolService(ps); err != nil {
				t.Fatal(err)
			}

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := ms.PoolResolveID(context.TODO(), tc.req)
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

func propWithName(r *mgmtpb.PoolSetPropReq, n string) *mgmtpb.PoolSetPropReq {
	r.SetPropertyName(n)
	return r
}
func propWithNumber(r *mgmtpb.PoolSetPropReq, n uint32) *mgmtpb.PoolSetPropReq {
	r.SetPropertyNumber(n)
	return r
}

func propWithStrVal(r *mgmtpb.PoolSetPropReq, v string) *mgmtpb.PoolSetPropReq {
	r.SetValueString(v)
	return r
}
func propWithNumVal(r *mgmtpb.PoolSetPropReq, v uint64) *mgmtpb.PoolSetPropReq {
	r.SetValueNumber(v)
	return r
}

func TestServer_MgmtSvc_PoolSetProp_Label(t *testing.T) {
	defaultLabel := "test-label"

	for name, tc := range map[string]struct {
		poolUUID string
		label    string
		expErr   error
	}{
		"labels must be unique": {
			poolUUID: common.MockUUID(3),
			label:    defaultLabel,
			expErr:   FaultPoolDuplicateLabel(defaultLabel),
		},
		"success": {
			label: "unique-label",
		},
		"pool label application should be idempotent": {
			poolUUID: mockUUID,
			label:    defaultLabel,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			setupMockDrpcClient(ms, &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertyLabel,
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: tc.label,
				}}, nil)
			addTestPools(t, ms.sysdb, mockUUID)
			if tc.poolUUID != "" && tc.poolUUID != mockUUID {
				addTestPools(t, ms.sysdb, tc.poolUUID)
			}
			ps, err := ms.sysdb.FindPoolServiceByUUID(uuid.MustParse(mockUUID))
			if err != nil {
				t.Fatal(err)
			}
			ps.PoolLabel = defaultLabel
			if err := ms.sysdb.UpdatePoolService(ps); err != nil {
				t.Fatal(err)
			}

			req := propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "label"), tc.label)
			req.Uuid = tc.poolUUID
			if req.Uuid == "" {
				req.Uuid = mockUUID
			}
			if req.Sys == "" {
				req.Sys = build.DefaultSystemName
			}

			_, gotErr := ms.PoolSetProp(context.TODO(), req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			found, err := ms.sysdb.FindPoolServiceByLabel(tc.label)
			if err != nil {
				t.Fatal(err)
			}

			if found.PoolUUID != ps.PoolUUID {
				t.Fatalf("after labeling, found pool UUID doesn't match original: %s != %s", found.PoolUUID, ps.PoolUUID)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolSetProp(t *testing.T) {
	lastCall := func(svc *mgmtSvc) *drpc.Call {
		mi := svc.harness.instances[0]
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
		"wrong system": {
			req:    &mgmtpb.PoolSetPropReq{Uuid: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"garbage resp": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"unhandled property": {
			req:    propWithName(new(mgmtpb.PoolSetPropReq), "unknown"),
			expErr: errors.New("unhandled pool property"),
		},
		"response property mismatch": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: 4242424242,
				},
			},
			expErr: errors.New("Response number doesn't match"),
		},
		"response value mismatch": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
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
			req:    propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "unknown"),
			expErr: errors.New("unhandled reclaim type"),
		},
		"reclaim-disabled": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "disabled"),
			expReq: propWithNumVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySpaceReclaim),
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
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "lazy"),
			expReq: propWithNumVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySpaceReclaim),
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
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "reclaim"), "time"),
			expReq: propWithNumVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySpaceReclaim),
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
		"label": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "label"), "foo"),
			expReq: propWithStrVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertyLabel),
				"foo",
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertyLabel,
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "foo",
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "label",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "foo",
				},
			},
		},
		"empty label is valid": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "label"), ""),
			expReq: propWithStrVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertyLabel),
				"",
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertyLabel,
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "",
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "label",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "",
				},
			},
		},

		"space_rb > 100": {
			req:    propWithNumVal(propWithName(new(mgmtpb.PoolSetPropReq), "space_rb"), 101),
			expErr: errors.New("invalid space_rb value"),
		},
		"space_rb 5%": {
			// if the input was interpreted as a string, we should reject it
			req:    propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "space_rb"), "5%"),
			expErr: errors.New("invalid space_rb value"),
		},
		"space_rb": {
			req: propWithNumVal(propWithName(new(mgmtpb.PoolSetPropReq), "space_rb"), 42),
			expReq: propWithNumVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertyReservedSpace),
				42,
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertyReservedSpace,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: 42,
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "space_rb",
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: 42,
				},
			},
		},
		"self_heal-unknown": {
			req:    propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "self_heal"), "unknown"),
			expErr: errors.New("unhandled self_heal type"),
		},
		"self_heal-exclude": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "self_heal"), "exclude"),
			expReq: propWithNumVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySelfHealing),
				drpc.PoolSelfHealingAutoExclude,
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertySelfHealing,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: drpc.PoolSelfHealingAutoExclude,
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "self_heal",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "exclude",
				},
			},
		},
		"self_heal-rebuild": {
			req: propWithStrVal(propWithName(new(mgmtpb.PoolSetPropReq), "self_heal"), "rebuild"),
			expReq: propWithNumVal(
				propWithNumber(new(mgmtpb.PoolSetPropReq), drpc.PoolPropertySelfHealing),
				drpc.PoolSelfHealingAutoRebuild,
			),
			drpcResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Number{
					Number: drpc.PoolPropertySelfHealing,
				},
				Value: &mgmtpb.PoolSetPropResp_Numval{
					Numval: drpc.PoolSelfHealingAutoRebuild,
				},
			},
			expResp: &mgmtpb.PoolSetPropResp{
				Property: &mgmtpb.PoolSetPropResp_Name{
					Name: "self_heal",
				},
				Value: &mgmtpb.PoolSetPropResp_Strval{
					Strval: "rebuild",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)
			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(svc, tc.drpcResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(ms, tc.expErr)

			if tc.req.GetUuid() == "" {
				tc.req.Uuid = mockUUID
			}
			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
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
			tc.expReq.Uuid = tc.req.Uuid
			gotReq.SvcRanks = nil
			if diff := cmp.Diff(tc.expReq, gotReq); diff != "" {
				t.Fatalf("unexpected dRPC call (-want, +got):\n%s\n", diff)
			}
		})
	}
}
