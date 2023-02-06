//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func getPoolLockCtx(t *testing.T, parent context.Context, sysdb *system.Database, poolUUID uuid.UUID) (*system.PoolLock, context.Context) {
	t.Helper()

	if parent == nil {
		parent = context.Background()
	}

	lock, err := sysdb.TakePoolLock(parent, poolUUID)
	if err != nil {
		t.Fatal(err)
	}

	return lock, lock.InContext(parent)
}

func addTestPoolService(t *testing.T, sysdb *system.Database, ps *system.PoolService) {
	t.Helper()

	var i uint32
	for _, r := range ps.Replicas {
		_, err := sysdb.FindMemberByRank(r)
		if err == nil {
			continue
		}
		if !system.IsMemberNotFound(err) {
			t.Fatal(err)
		}

		if err := sysdb.AddMember(system.MockMember(t, i, system.MemberStateJoined)); err != nil {
			t.Fatal(err)
		}

		i++
	}

	lock, ctx := getPoolLockCtx(t, nil, sysdb, ps.PoolUUID)
	defer lock.Release()
	if err := sysdb.AddPoolService(ctx, ps); err != nil {
		t.Fatal(err)
	}
}

func addTestPools(t *testing.T, sysdb *system.Database, poolUUIDs ...string) {
	t.Helper()

	for i, uuidStr := range poolUUIDs {
		addTestPoolService(t, sysdb, &system.PoolService{
			PoolUUID:  uuid.MustParse(uuidStr),
			PoolLabel: fmt.Sprintf("%d", i),
			State:     system.PoolServiceStateReady,
			Replicas:  []system.Rank{0},
		})
	}
}

func testPoolLabelProp() []*mgmtpb.PoolProperty {
	return []*mgmtpb.PoolProperty{
		{
			Number: drpc.PoolPropertyLabel,
			Value: &mgmtpb.PoolProperty_Strval{
				Strval: "test",
			},
		},
	}
}

func TestServer_MgmtSvc_PoolCreateAlreadyExists(t *testing.T) {
	for name, tc := range map[string]struct {
		state   system.PoolServiceState
		expResp *mgmtpb.PoolCreateResp
		expErr  error
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
				SvcReps:   []uint32{1},
				TgtRanks:  []uint32{1},
				TierBytes: []uint64{1, 2},
			},
		},
		"destroying": {
			state: system.PoolServiceStateDestroying,
			expResp: &mgmtpb.PoolCreateResp{
				Status: int32(drpc.DaosTryAgain),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)
			if _, err := svc.membership.Add(system.MockMember(t, 1, system.MemberStateJoined)); err != nil {
				t.Fatal(err)
			}
			poolUUID := test.MockPoolUUID(1)
			lock, ctx := getPoolLockCtx(t, nil, svc.sysdb, poolUUID)
			defer lock.Release()

			if err := svc.sysdb.AddPoolService(ctx, &system.PoolService{
				PoolUUID: poolUUID,
				State:    tc.state,
				Storage: &system.PoolServiceStorage{
					CreationRankStr:    "1",
					PerRankTierStorage: []uint64{1, 2},
				},
				Replicas: []system.Rank{1},
			}); err != nil {
				t.Fatal(err)
			}

			req := &mgmtpb.PoolCreateReq{
				Sys:        build.DefaultSystemName,
				Uuid:       test.MockUUID(1),
				Totalbytes: engine.ScmMinBytesPerTarget,
				Properties: testPoolLabelProp(),
			}

			gotResp, gotErr := svc.PoolCreate(ctx, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expResp, gotResp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

// These should be adapted to test whatever the new logic is for calculating
// per-tier storage allocations.

func TestServer_MgmtSvc_calculateCreateStorage(t *testing.T) {
	defaultTotal := uint64(10 * humanize.TByte)
	defaultRatios := []float64{DefaultPoolScmRatio, DefaultPoolNvmeRatio}
	defaultScmBytes := uint64(float64(defaultTotal) * DefaultPoolScmRatio)
	defaultNvmeBytes := uint64(float64(defaultTotal) * DefaultPoolNvmeRatio)
	testTargetCount := 8
	minPoolScm := minPoolScm(uint64(testTargetCount), 1)
	minPoolNvme := minPoolNvme(uint64(testTargetCount), 1)
	scmTooSmallRatio := 0.01
	scmTooSmallTotal := uint64(testTargetCount * engine.NvmeMinBytesPerTarget)
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
				Tierratio:  defaultRatios,
				Ranks:      []uint32{0, 1},
			},
			expOut: &mgmtpb.PoolCreateReq{
				Tierbytes: []uint64{defaultScmBytes / 2, defaultNvmeBytes / 2},
				Tierratio: []float64{0, 0},
				Ranks:     []uint32{0, 1},
			},
		},
		"auto sizing (not enough SCM)": {
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: scmTooSmallTotal,
				Tierratio:  []float64{scmTooSmallRatio},
				Ranks:      []uint32{0},
			},
			expErr: FaultPoolScmTooSmall(scmTooSmallReq, minPoolScm),
		},
		"auto sizing (not enough NVMe)": {
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: nvmeTooSmallTotal,
				Tierratio:  []float64{DefaultPoolScmRatio, DefaultPoolNvmeRatio},
				Ranks:      []uint32{0},
			},
			expErr: FaultPoolNvmeTooSmall(uint64(float64(nvmeTooSmallReq)*DefaultPoolNvmeRatio), minPoolNvme),
		},
		"auto sizing (no NVMe in config)": {
			disableNVMe: true,
			in: &mgmtpb.PoolCreateReq{
				Totalbytes: defaultTotal,
				Tierratio:  []float64{DefaultPoolScmRatio, 1 - DefaultPoolScmRatio},
				Ranks:      []uint32{0},
			},
			expOut: &mgmtpb.PoolCreateReq{
				Tierbytes: []uint64{defaultTotal, 0},
				Tierratio: []float64{0, 0},
				Ranks:     []uint32{0},
			},
		},
		"manual sizing": {
			in: &mgmtpb.PoolCreateReq{
				Tierbytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0, 1},
			},
			expOut: &mgmtpb.PoolCreateReq{
				Tierbytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Tierratio: []float64{0, 0},
				Ranks:     []uint32{0, 1},
			},
		},
		"manual sizing (not enough SCM)": {
			in: &mgmtpb.PoolCreateReq{
				Tierbytes: []uint64{scmTooSmallReq},
				Ranks:     []uint32{0},
			},
			expErr: FaultPoolScmTooSmall(scmTooSmallReq, minPoolScm),
		},
		"manual sizing (not enough NVMe)": {
			in: &mgmtpb.PoolCreateReq{
				Tierbytes: []uint64{defaultScmBytes, nvmeTooSmallReq},
				Ranks:     []uint32{0},
			},
			expErr: FaultPoolNvmeTooSmall(nvmeTooSmallReq, minPoolNvme),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfg := engine.MockConfig().WithTargetCount(testTargetCount)
			if !tc.disableNVMe {
				engineCfg = engineCfg.
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("foo", "bar"),
					)
			}
			svc := newTestMgmtSvc(t, log)
			sp := storage.MockProvider(log, 0, &engineCfg.Storage, nil, nil, nil)
			svc.harness.instances[0] = newTestEngine(log, false, sp, engineCfg)

			gotErr := svc.calculateCreateStorage(tc.in)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, tc.in, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected req (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolCreate(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		targetCount   int
		memberCount   int
		req           *mgmtpb.PoolCreateReq
		expResp       *mgmtpb.PoolCreateResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req: &mgmtpb.PoolCreateReq{
				Uuid:       mockUUID,
				Sys:        "bad",
				Properties: testPoolLabelProp(),
			},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc:     missingSB,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc:     notAP,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("not an access point"),
		},
		"dRPC send fails": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			targetCount: 0,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
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
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				TgtRanks:  []uint32{0, 1},
			},
		},
		"successful creation minimum size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{engine.ScmMinBytesPerTarget * 8, engine.NvmeMinBytesPerTarget * 8},
				Properties: testPoolLabelProp(),
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{engine.ScmMinBytesPerTarget * 8, engine.NvmeMinBytesPerTarget * 8},
				TgtRanks:  []uint32{0, 1},
			},
		},
		"successful creation auto size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Totalbytes: 100 * humanize.GiByte,
				Properties: testPoolLabelProp(),
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{((100 * humanize.GiByte) * DefaultPoolScmRatio) / 2, (100 * humanize.GiByte * DefaultPoolNvmeRatio) / 2},
				TgtRanks:  []uint32{0, 1},
			},
		},
		"failed creation invalid ranks": {
			targetCount: 1,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Ranks:      []uint32{40, 11},
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidRanks([]system.Rank{11, 40}),
		},
		"failed creation invalid number of ranks": {
			targetCount: 1,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Tierbytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Numranks:   3,
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidNumRanks(3, 2),
		},
		"svc replicas > max": {
			targetCount: 1,
			memberCount: MaxPoolServiceReps + 2,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Totalbytes: 100 * humanize.GByte,
				Tierratio:  []float64{0.06, 0.94},
				Numsvcreps: MaxPoolServiceReps + 2,
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidServiceReps(uint32(MaxPoolServiceReps)),
		},
		"svc replicas > numRanks": {
			targetCount: 1,
			memberCount: MaxPoolServiceReps - 2,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				Totalbytes: 100 * humanize.GByte,
				Tierratio:  []float64{0.06, 0.94},
				Numsvcreps: MaxPoolServiceReps - 1,
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidServiceReps(uint32(MaxPoolServiceReps - 2)),
		},
		"no label": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      test.MockUUID(1),
				Tierbytes: []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
			},
			expErr: FaultPoolNoLabel,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			if tc.mgmtSvc == nil {
				engineCfg := engine.MockConfig().
					WithTargetCount(tc.targetCount).
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("foo", "bar"),
					)
				r := engine.NewTestRunner(nil, engineCfg)
				if err := r.Start(ctx, make(chan<- error)); err != nil {
					t.Fatal(err)
				}

				mp := storage.NewProvider(log, 0, &engineCfg.Storage,
					nil, nil, nil)
				srv := NewEngineInstance(log, mp, nil, r)
				srv.ready.SetTrue()

				harness := NewEngineHarness(log)
				if err := harness.AddInstance(srv); err != nil {
					panic(err)
				}
				harness.started.SetTrue()

				ms, db := system.MockMembership(t, log, mockTCPResolver)
				tc.mgmtSvc = newMgmtSvc(harness, ms, db, nil,
					events.NewPubSub(context.Background(), log))
			}

			numMembers := tc.memberCount
			if numMembers < 1 {
				numMembers = 2
			}
			tc.mgmtSvc.log = log
			for i := 0; i < numMembers; i++ {
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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolCreateDownRanks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	mgmtSvc := newTestMgmtSvc(t, log)
	ec := engine.MockConfig().
		WithTargetCount(1).
		WithStorage(
			storage.NewTierConfig().
				WithStorageClass("ram").
				WithScmMountPoint("/foo/bar"),
			storage.NewTierConfig().
				WithStorageClass("nvme").
				WithBdevDeviceList("foo", "bar"),
		)
	sp := storage.NewProvider(log, 0, &ec.Storage, nil, nil, nil)
	mgmtSvc.harness.instances[0] = newTestEngine(log, false, sp, ec)

	dc := newMockDrpcClient(&mockDrpcClientConfig{IsConnectedBool: true})
	dc.cfg.setSendMsgResponse(drpc.Status_SUCCESS, nil, nil)
	mgmtSvc.harness.instances[0].(*EngineInstance)._drpcClient = dc

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

	fdTree, err := mgmtSvc.membership.CompressedFaultDomainTree(0, 2, 3)
	if err != nil {
		t.Fatal(err)
	}
	totalBytes := uint64(100 * humanize.GiByte)
	req := &mgmtpb.PoolCreateReq{
		Sys:          build.DefaultSystemName,
		Uuid:         test.MockUUID(),
		Totalbytes:   totalBytes,
		FaultDomains: fdTree,
		Properties:   testPoolLabelProp(),
	}
	wantReq := new(mgmtpb.PoolCreateReq)
	*wantReq = *req
	// Ugh. We shouldn't need to do all of this out here. Maybe create
	// a helper or otherwise find a way to focus this test on the logic
	// for avoiding downed ranks.
	wantReq.Totalbytes = 0
	wantReq.Tierbytes = []uint64{
		uint64(float64(totalBytes)*DefaultPoolScmRatio) / 3,
		uint64(float64(totalBytes)*DefaultPoolNvmeRatio) / 3,
	}
	wantReq.Tierratio = []float64{0, 0}
	wantReq.Numsvcreps = DefaultPoolServiceReps

	_, err = mgmtSvc.PoolCreate(ctx, req)
	if err != nil {
		t.Fatal(err)
	}

	// We should only be trying to create on the Joined ranks.
	wantReq.Ranks = []uint32{0, 2, 3}

	gotReq := new(mgmtpb.PoolCreateReq)
	if err := proto.Unmarshal(dc.calls[0].Body, gotReq); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(wantReq, gotReq, test.DefaultCmpOpts()...); diff != "" {
		t.Fatalf("unexpected pool create req (-want, +got):\n%s\n", diff)
	}
}

func TestServer_MgmtSvc_PoolDestroy(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)
	testPoolService := &system.PoolService{
		PoolLabel: "test-pool",
		PoolUUID:  uuid.MustParse(mockUUID),
		Replicas:  []system.Rank{0, 1, 2},
		State:     system.PoolServiceStateReady,
		Storage: &system.PoolServiceStorage{
			CreationRankStr: system.MustCreateRankSet("0-7").String(),
		},
	}
	svcWithState := func(in *system.PoolService, state system.PoolServiceState) (out *system.PoolService) {
		out = new(system.PoolService)
		*out = *in
		out.State = state
		return
	}

	// Note: PoolDestroy will invoke one or two dRPCs (evict, evict+destroy)
	// expDrpcEvReq is here for those cases in which just the evict dRPC is run
	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		poolSvc       *system.PoolService
		req           *mgmtpb.PoolDestroyReq
		expDrpcEvReq  *mgmtpb.PoolEvictReq
		expDrpcReq    *mgmtpb.PoolDestroyReq
		expResp       *mgmtpb.PoolDestroyResp
		expSvc        *system.PoolService
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolDestroyReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolDestroyReq{},
			expErr: errors.New("empty pool id"),
		},
		// Note: evict dRPC fails, still expect a PoolDestroyResp from PoolDestroy
		"evict dRPC fails with -DER_BUSY due to open handles force=false, pool still ready": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolEvictResp{
					Status: int32(drpc.DaosBusy),
				}, nil)
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosBusy),
			},
			expSvc: testPoolService,
		},
		"evict dRPC fails due to engine error": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolEvictResp{
					Status: int32(drpc.DaosMiscError),
				}, nil)
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosMiscError),
			},
			expSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		"force=true, evict dRPC fails due to engine error": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Force: true},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: true,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolEvictResp{
					Status: int32(drpc.DaosMiscError),
				}, nil)
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosMiscError),
			},
			expSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		"already destroying, destroy dRPC fails due to engine error": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2, 3, 4, 5, 6, 7},
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolDestroyResp{
					Status: int32(drpc.DaosMiscError),
				}, nil)
			},
			poolSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosMiscError),
			},
			expSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		"force=true already destroying, destroy dRPC fails due to engine error": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Force: true},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Force:    true,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolDestroyResp{
					Status: int32(drpc.DaosMiscError),
				}, nil)
			},
			poolSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosMiscError),
			},
			expSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		"evict dRPC fails with -DER_NOTLEADER on first try": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolEvictResp{
					Status: int32(drpc.DaosNotLeader),
				}, nil)
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosNotLeader),
			},
			expSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		"evict dRPC fails with -DER_NOTREPLICA on first try": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				setupMockDrpcClient(svc, &mgmtpb.PoolEvictResp{
					Status: int32(drpc.DaosNotReplica),
				}, nil)
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosNotReplica),
			},
			expSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		"already destroying, destroy dRPC succeeds": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2, 3, 4, 5, 6, 7},
			},
			expResp: &mgmtpb.PoolDestroyResp{},
			poolSvc: svcWithState(testPoolService, system.PoolServiceStateDestroying),
		},
		// Note: PoolDestroy() is going to run both evict and destroy dRPCs each of which will succeed
		"successful destroy": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2, 3, 4, 5, 6, 7},
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
		"force=true, successful destroy": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Force: true},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Force:    true,
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			tc.mgmtSvc.log = log

			numMembers := 8
			for i := 0; i < numMembers; i++ {
				if _, err := tc.mgmtSvc.membership.Add(system.MockMember(t, uint32(i), system.MemberStateJoined)); err != nil {
					t.Fatal(err)
				}
			}

			poolSvc := tc.poolSvc
			if poolSvc == nil {
				poolSvc = testPoolService
			}
			lock, ctx := getPoolLockCtx(t, nil, tc.mgmtSvc.sysdb, poolSvc.PoolUUID)
			defer lock.Release()

			if err := tc.mgmtSvc.sysdb.AddPoolService(ctx, poolSvc); err != nil {
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

			gotResp, gotErr := tc.mgmtSvc.PoolDestroy(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := append(
				test.DefaultCmpOpts(),
				cmpopts.IgnoreTypes(system.PoolServiceStorage{}),
				cmpopts.IgnoreFields(system.PoolService{}, "LastUpdate"),
			)
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			gotSvc, err := tc.mgmtSvc.sysdb.FindPoolServiceByUUID(uuid.MustParse(mockUUID))
			if err != nil {
				if tc.expSvc != nil || !system.IsPoolNotFound(err) {
					t.Fatalf("unexpected error: %v", err)
				}
			}
			if tc.expSvc == nil && gotSvc != nil {
				t.Fatalf("expected pool to be destroyed, but found %+v", gotSvc)
			}
			if diff := cmp.Diff(tc.expSvc, gotSvc, cmpOpts...); diff != "" {
				t.Fatalf("unexpected ending PS values (-want, +got)\n%s\n", diff)
			}

			if tc.expDrpcReq != nil {
				gotReq := new(mgmtpb.PoolDestroyReq)
				if err := proto.Unmarshal(getLastMockCall(tc.mgmtSvc).Body, gotReq); err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(tc.expDrpcReq, gotReq, cmpOpts...); diff != "" {
					t.Fatalf("unexpected destroy dRPC call (-want, +got):\n%s\n", diff)
				}
			}
			if tc.expDrpcEvReq != nil {
				gotReq := new(mgmtpb.PoolEvictReq)
				if err := proto.Unmarshal(getLastMockCall(tc.mgmtSvc).Body, gotReq); err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(tc.expDrpcEvReq, gotReq, cmpOpts...); diff != "" {
					t.Fatalf("unexpected evict dRPC call (-want, +got):\n%s\n", diff)
				}
			}

		})
	}
}

func TestServer_MgmtSvc_PoolExtend(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)
	scmAllocation := uint64(1)
	nvmeAllocation := uint64(2)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []system.Rank{0},
		Storage: &system.PoolServiceStorage{
			CreationRankStr:    "0",
			CurrentRankStr:     "0",
			PerRankTierStorage: []uint64{scmAllocation, nvmeAllocation},
		},
	}

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolExtendReq
		expResp       *mgmtpb.PoolExtendResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolExtendReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolExtendReq{Id: mockUUID, Ranks: []uint32{1}},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolExtendReq{Id: mockUUID, Ranks: []uint32{1}},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolExtendReq{Id: mockUUID, Ranks: []uint32{1}},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolExtendReq{Id: mockUUID, Ranks: []uint32{1}},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolExtendReq{Ranks: []uint32{1}},
			expErr: errors.New("empty pool id"),
		},
		"successfully extended": {
			req: &mgmtpb.PoolExtendReq{Id: mockUUID, Ranks: []uint32{1}},
			expResp: &mgmtpb.PoolExtendResp{
				TierBytes: []uint64{scmAllocation, nvmeAllocation},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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

			if _, err := tc.mgmtSvc.membership.Add(system.MockMember(t, 1, system.MemberStateJoined)); err != nil {
				t.Fatal(err)
			}

			gotResp, gotErr := tc.mgmtSvc.PoolExtend(context.TODO(), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolDrain(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
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
			req:    &mgmtpb.PoolDrainReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			req:    &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolDrainReq{Rank: 2, Targetidx: []uint32{1, 2}},
			expErr: errors.New("empty pool id"),
		},
		"successful drained": {
			req:     &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, Targetidx: []uint32{1, 2}},
			expResp: &mgmtpb.PoolDrainResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolEvict(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
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
			req:    &mgmtpb.PoolEvictReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolEvictReq{Id: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolEvictReq{Id: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolEvictReq{Id: mockUUID},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolEvictReq{Id: mockUUID},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolEvictReq{},
			expErr: errors.New("empty pool id"),
		},
		"successful evicted": {
			req:     &mgmtpb.PoolEvictReq{Id: mockUUID},
			expResp: &mgmtpb.PoolEvictResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
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
	defer test.ShowBufferOnFailure(t, buf)

	h := NewEngineHarness(log)
	h.started.SetTrue()
	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("replica"), err)
}

func TestListPools_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	testPools := []*system.PoolService{
		{
			PoolUUID:  test.MockPoolUUID(1),
			PoolLabel: "0",
			State:     system.PoolServiceStateReady,
			Replicas:  []system.Rank{0, 1, 2},
		},
		{
			PoolUUID:  test.MockPoolUUID(2),
			PoolLabel: "1",
			State:     system.PoolServiceStateReady,
			Replicas:  []system.Rank{0, 1, 2},
		},
	}
	expectedResp := &mgmtpb.ListPoolsResp{
		DataVersion: uint64(len(testPools)),
	}

	svc := newTestMgmtSvc(t, log)
	for _, ps := range testPools {
		lock, ctx := getPoolLockCtx(t, nil, svc.sysdb, ps.PoolUUID)
		if err := svc.sysdb.AddPoolService(ctx, ps); err != nil {
			t.Fatal(err)
		}
		lock.Release()
		expectedResp.Pools = append(expectedResp.Pools, &mgmtpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			Label:   ps.PoolLabel,
			SvcReps: []uint32{0, 1, 2},
			State:   system.PoolServiceStateReady.String(),
		})
	}

	resp, err := svc.ListPools(context.TODO(), newTestListPoolsReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := test.DefaultCmpOpts()
	cmpOpts = append(cmpOpts,
		protocmp.SortRepeated(func(a, b *mgmtpb.ListPoolsResp_Pool) bool {
			return a.GetUuid() < b.GetUuid()
		}),
	)
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestGetACLReq() *mgmtpb.GetACLReq {
	return &mgmtpb.GetACLReq{
		Sys: build.DefaultSystemName,
		Id:  mockUUID,
	}
}

func TestPoolGetACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("replica"), err)
}

func TestPoolGetACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

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

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestPoolGetACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, expectedErr, err)
}

func TestPoolGetACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(12)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolGetACL(context.TODO(), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("unmarshal"), err)
}

func newTestModifyACLReq() *mgmtpb.ModifyACLReq {
	return &mgmtpb.ModifyACLReq{
		Sys: build.DefaultSystemName,
		Id:  mockUUID,
		ACL: []string{
			"A::OWNER@:rw",
		},
	}
}

func TestPoolOverwriteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("replica"), err)
}

func TestPoolOverwriteACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, expectedErr, err)
}

func TestPoolOverwriteACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(16)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolOverwriteACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolOverwriteACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

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

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestPoolUpdateACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("replica"), err)
}

func TestPoolUpdateACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, expectedErr, err)
}

func TestPoolUpdateACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(16)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolUpdateACL(context.TODO(), newTestModifyACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolUpdateACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

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

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func newTestDeleteACLReq() *mgmtpb.DeleteACLReq {
	return &mgmtpb.DeleteACLReq{
		Sys:       build.DefaultSystemName,
		Id:        mockUUID,
		Principal: "u:user@",
	}
}

func TestPoolDeleteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("replica"), err)
}

func TestPoolDeleteACL_DrpcFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	expectedErr := errors.New("mock error")
	setupMockDrpcClient(svc, nil, expectedErr)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, expectedErr, err)
}

func TestPoolDeleteACL_BadDrpcResp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvc(t, log)
	addTestPools(t, svc.sysdb, mockUUID)
	// dRPC call returns junk in the message body
	badBytes := makeBadBytes(16)

	setupMockDrpcClientBytes(svc, badBytes, nil)

	resp, err := svc.PoolDeleteACL(context.TODO(), newTestDeleteACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("unmarshal"), err)
}

func TestPoolDeleteACL_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

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

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestServer_MgmtSvc_PoolQuery(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil

	allRanksDown := newTestMgmtSvc(t, testLog)
	downRanksPool := test.MockUUID(9)
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
			req:    &mgmtpb.PoolQueryReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req: &mgmtpb.PoolQueryReq{
				Id: mockUUID,
			},
			expErr: errors.New("not an access point"),
		},
		"dRPC send fails": {
			req: &mgmtpb.PoolQueryReq{
				Id: mockUUID,
			},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolQueryReq{
				Id: mockUUID,
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
				Id: downRanksPool,
			},
			expErr: errors.New("available"),
		},
		"successful query": {
			req: &mgmtpb.PoolQueryReq{
				Id: mockUUID,
			},
			expResp: &mgmtpb.PoolQueryResp{
				Uuid: mockUUID,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func getLastMockCall(svc *mgmtSvc) *drpc.Call {
	mi := svc.harness.instances[0].(*EngineInstance)
	if mi == nil || mi._drpcClient == nil {
		return nil
	}

	return mi._drpcClient.(*mockDrpcClient).SendMsgInputCall
}

func TestServer_MgmtSvc_PoolSetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		setupMockDrpc func(_ *mgmtSvc, _ error)
		drpcResp      *mgmtpb.PoolSetPropResp
		req           *mgmtpb.PoolSetPropReq
		expDrpcReq    *mgmtpb.PoolSetPropReq
		expErr        error
	}{
		"wrong system": {
			req:    &mgmtpb.PoolSetPropReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"garbage resp": {
			req: &mgmtpb.PoolSetPropReq{
				Id: mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
				},
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"req with 0 props": {
			req:    &mgmtpb.PoolSetPropReq{Id: mockUUID},
			expErr: errors.New("0 properties"),
		},
		"label is not unique": {
			req: &mgmtpb.PoolSetPropReq{
				Id: test.MockUUID(3),
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
					{
						Number: drpc.PoolPropertySpaceReclaim,
						Value:  &mgmtpb.PoolProperty_Numval{drpc.PoolSpaceReclaimDisabled},
					},
				},
			},
			expErr: FaultPoolDuplicateLabel("0"),
			// Expect that there was no dRPC request made because the label is invalid.
			expDrpcReq: nil,
		},
		"label set is idempotent": {
			req: &mgmtpb.PoolSetPropReq{
				Id: mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
				},
			},
			expDrpcReq: &mgmtpb.PoolSetPropReq{
				Sys:      build.DefaultSystemName,
				SvcRanks: []uint32{0},
				Id:       mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
				},
			},
		},
		"success": {
			req: &mgmtpb.PoolSetPropReq{
				Id: mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"ok"},
					},
					{
						Number: drpc.PoolPropertySpaceReclaim,
						Value:  &mgmtpb.PoolProperty_Numval{drpc.PoolSpaceReclaimDisabled},
					},
				},
			},
			// expect that the last request is to set the remainder of props
			expDrpcReq: &mgmtpb.PoolSetPropReq{
				Sys:      build.DefaultSystemName,
				SvcRanks: []uint32{0},
				Id:       mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertySpaceReclaim,
						Value:  &mgmtpb.PoolProperty_Numval{drpc.PoolSpaceReclaimDisabled},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)
			if tc.req.Id != mockUUID {
				addTestPools(t, ms.sysdb, tc.req.Id)
			}
			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(svc, tc.drpcResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(ms, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			_, gotErr := ms.PoolSetProp(context.TODO(), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			lastReq := new(mgmtpb.PoolSetPropReq)
			if err := proto.Unmarshal(getLastMockCall(ms).Body, lastReq); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expDrpcReq, lastReq, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected final dRPC request (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolGetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		setupMockDrpc func(_ *mgmtSvc, _ error)
		drpcResp      *mgmtpb.PoolGetPropResp
		req           *mgmtpb.PoolGetPropReq
		expDrpcReq    *mgmtpb.PoolGetPropReq
		expErr        error
	}{
		"wrong system": {
			req:    &mgmtpb.PoolGetPropReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"garbage resp": {
			req: &mgmtpb.PoolGetPropReq{
				Id: mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
					},
				},
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"req with 0 props": {
			req:    &mgmtpb.PoolGetPropReq{Id: mockUUID},
			expErr: errors.New("0 properties"),
		},
		"success": {
			req: &mgmtpb.PoolGetPropReq{
				Id: mockUUID,
				Properties: []*mgmt.PoolProperty{
					{
						Number: drpc.PoolPropertyLabel,
					},
					{
						Number: drpc.PoolPropertySpaceReclaim,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ms := newTestMgmtSvc(t, log)
			addTestPools(t, ms.sysdb, mockUUID)
			if tc.req.Id != mockUUID {
				addTestPools(t, ms.sysdb, tc.req.Id)
			}
			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupMockDrpcClient(svc, tc.drpcResp, tc.expErr)
				}
			}
			tc.setupMockDrpc(ms, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			_, gotErr := ms.PoolGetProp(context.TODO(), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestServer_MgmtSvc_PoolUpgrade(t *testing.T) {
	testLog, _ := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, testLog)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, testLog)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []system.Rank{0},
	}

	for name, tc := range map[string]struct {
		mgmtSvc       *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		req           *mgmtpb.PoolUpgradeReq
		expResp       *mgmtpb.PoolUpgradeResp
		expErr        error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolUpgradeReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.PoolUpgradeReq{Id: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"not access point": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolUpgradeReq{Id: mockUUID},
			expErr:  errors.New("not an access point"),
		},
		"dRPC send fails": {
			req:    &mgmtpb.PoolUpgradeReq{Id: mockUUID},
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			req: &mgmtpb.PoolUpgradeReq{Id: mockUUID},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupMockDrpcClientBytes(svc, badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolUpgradeReq{},
			expErr: errors.New("empty pool id"),
		},
		"successful upgraded": {
			req:     &mgmtpb.PoolUpgradeReq{Id: mockUUID},
			expResp: &mgmtpb.PoolUpgradeResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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

			gotResp, gotErr := tc.mgmtSvc.PoolUpgrade(context.TODO(), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
