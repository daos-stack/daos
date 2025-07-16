//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

var (
	mockTierBytes   = []uint64{uint64(1), uint64(2)}
	mockSvcRanks    = []uint32{0}
	mockMemRatio    = float32(0.5)
	mockPoolService = &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []ranklist.Rank{0},
		Storage: &system.PoolServiceStorage{
			CreationRankStr:    "0",
			PerRankTierStorage: mockTierBytes,
			MemRatio:           mockMemRatio,
		},
	}
	errNotReplica = errors.New("not a MS replica")
)

func getPoolLockCtx(t *testing.T, parent context.Context, sysdb poolDatabase, poolUUID uuid.UUID) (*raft.PoolLock, context.Context) {
	t.Helper()

	if parent == nil {
		parent = test.Context(t)
	}

	lock, err := sysdb.TakePoolLock(parent, poolUUID)
	if err != nil {
		t.Fatal(err)
	}

	return lock, lock.InContext(parent)
}

func addTestPoolService(t *testing.T, sysdb *raft.Database, ps *system.PoolService) {
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

func addTestPools(t *testing.T, sysdb *raft.Database, poolUUIDs ...string) {
	t.Helper()

	for i, uuidStr := range poolUUIDs {
		addTestPoolService(t, sysdb, &system.PoolService{
			PoolUUID:  uuid.MustParse(uuidStr),
			PoolLabel: fmt.Sprintf("%d", i),
			State:     system.PoolServiceStateReady,
			Replicas:  []ranklist.Rank{0},
		})
	}
}

func testPoolLabelProp() []*mgmtpb.PoolProperty {
	return []*mgmtpb.PoolProperty{
		{
			Number: daos.PoolPropertyLabel,
			Value: &mgmtpb.PoolProperty_Strval{
				Strval: "test",
			},
		},
	}
}

func TestServer_MgmtSvc_PoolCreateAlreadyExists(t *testing.T) {
	for name, tc := range map[string]struct {
		state     system.PoolServiceState
		queryResp *mgmtpb.PoolQueryResp
		queryErr  error
		expResp   *mgmtpb.PoolCreateResp
		expErr    error
	}{
		"creating": {
			state: system.PoolServiceStateCreating,
			expResp: &mgmtpb.PoolCreateResp{
				Status: int32(daos.TryAgain),
			},
		},
		"ready": {
			state: system.PoolServiceStateReady,
			queryResp: &mgmtpb.PoolQueryResp{
				SvcLdr: 1,
			},
			expResp: &mgmtpb.PoolCreateResp{
				SvcLdr:    1,
				SvcReps:   []uint32{1},
				TgtRanks:  []uint32{1},
				TierBytes: []uint64{1, 2},
			},
		},
		"ready (query error)": {
			state:    system.PoolServiceStateReady,
			queryErr: errors.New("query error"),
			expErr:   errors.New("query error"),
		},
		"destroying": {
			state: system.PoolServiceStateDestroying,
			expResp: &mgmtpb.PoolCreateResp{
				Status: int32(daos.TryAgain),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)
			mdc := getMockDrpcClient(tc.queryResp, tc.queryErr)
			setupSvcDrpcClient(svc, 0, mdc)
			if _, err := svc.membership.Add(system.MockMember(t, 1, system.MemberStateJoined)); err != nil {
				t.Fatal(err)
			}

			poolUUID := test.MockPoolUUID(1)
			lock, ctx := getPoolLockCtx(t, nil, svc.sysdb, poolUUID)
			if err := svc.sysdb.AddPoolService(ctx, &system.PoolService{
				PoolUUID: poolUUID,
				State:    tc.state,
				Storage: &system.PoolServiceStorage{
					CreationRankStr:    "1",
					PerRankTierStorage: []uint64{1, 2},
					MemRatio:           0.5,
				},
				Replicas: []ranklist.Rank{1},
			}); err != nil {
				t.Fatal(err)
			}
			lock.Release()

			req := &mgmtpb.PoolCreateReq{
				Sys:        build.DefaultSystemName,
				Uuid:       test.MockUUID(1),
				TotalBytes: engine.ScmMinBytesPerTarget,
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
	nvmeTooSmallTotal := uint64(3 * humanize.GiByte)
	nvmeTooSmallReq := nvmeTooSmallTotal

	for name, tc := range map[string]struct {
		disableNVMe   bool
		enableMdOnSsd bool
		in            *mgmtpb.PoolCreateReq
		expOut        *mgmtpb.PoolCreateReq
		expErr        error
	}{
		"auto sizing": {
			in: &mgmtpb.PoolCreateReq{
				TotalBytes: defaultTotal,
				TierRatio:  defaultRatios,
				Ranks:      []uint32{0, 1},
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes / 2, defaultNvmeBytes / 2},
				Ranks:     []uint32{0, 1},
			},
		},
		"auto sizing (not enough SCM)": {
			in: &mgmtpb.PoolCreateReq{
				TotalBytes: scmTooSmallTotal,
				TierRatio:  []float64{scmTooSmallRatio, 0},
				Ranks:      []uint32{0},
			},
			expErr: FaultPoolScmTooSmall(scmTooSmallReq, minPoolScm),
		},
		"auto sizing (not enough NVMe)": {
			in: &mgmtpb.PoolCreateReq{
				TotalBytes: nvmeTooSmallTotal,
				TierRatio:  []float64{DefaultPoolScmRatio, DefaultPoolNvmeRatio},
				Ranks:      []uint32{0},
			},
			expErr: FaultPoolNvmeTooSmall(
				uint64(float64(nvmeTooSmallReq)*DefaultPoolNvmeRatio), minPoolNvme),
		},
		"auto sizing (no NVMe in config)": {
			disableNVMe: true,
			in: &mgmtpb.PoolCreateReq{
				TotalBytes: defaultTotal,
				TierRatio:  []float64{DefaultPoolScmRatio, 1 - DefaultPoolScmRatio},
				Ranks:      []uint32{0},
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultTotal, 0},
				Ranks:     []uint32{0},
			},
		},
		"auto sizing (mem-ratio but not MD-on-SSD)": {
			in: &mgmtpb.PoolCreateReq{
				TotalBytes: defaultTotal,
				TierRatio:  defaultRatios,
				Ranks:      []uint32{0, 1},
				MemRatio:   0.2,
			},
			expErr: FaultPoolMemRatioNoRoles,
		},
		"tier bytes set for both (no NVMe in config)": {
			disableNVMe: true,
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0},
			},
			expErr: errors.New("config has zero bdevs"),
		},
		"tier bytes set for scm only (no NVMe in config)": {
			disableNVMe: true,
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultTotal, 0},
				Ranks:     []uint32{0},
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultTotal, 0},
				Ranks:     []uint32{0},
			},
		},
		"mem-ratio is set (mdonssd not configured)": {
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0},
				MemRatio:  storage.DefaultMemoryFileRatio,
			},
			expErr: FaultPoolMemRatioNoRoles,
		},
		"mem-ratio is unset (mdonssd configured)": {
			enableMdOnSsd: true,
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0},
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0},
				MemRatio:  storage.DefaultMemoryFileRatio,
			},
		},
		"mem-ratio is set (mdonssd configured)": {
			enableMdOnSsd: true,
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0},
				MemRatio:  0.25,
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0},
				MemRatio:  0.25,
			},
		},
		"manual sizing": {
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0, 1},
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0, 1},
			},
		},
		"manual sizing (not enough SCM)": {
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{scmTooSmallReq, 0},
				Ranks:     []uint32{0},
			},
			expErr: FaultPoolScmTooSmall(scmTooSmallReq, minPoolScm),
		},
		"manual sizing (not enough NVMe)": {
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes, nvmeTooSmallReq},
				Ranks:     []uint32{0},
			},
			expErr: FaultPoolNvmeTooSmall(nvmeTooSmallReq, minPoolNvme),
		},
		"manual sizing (MD-on-SSD syntax used)": {
			enableMdOnSsd: true,
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0, 1},
				MemRatio:  1,
			},
			expOut: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0, 1},
				MemRatio:  1,
			},
		},
		"manual sizing (MD-on-SSD syntax used but not MD-on-SSD)": {
			in: &mgmtpb.PoolCreateReq{
				TierBytes: []uint64{defaultScmBytes - 1, defaultNvmeBytes - 1},
				Ranks:     []uint32{0, 1},
				MemRatio:  1,
			},
			expErr: FaultPoolMemRatioNoRoles,
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
				if tc.enableMdOnSsd {
					engineCfg.Storage.Tiers[0].
						WithBdevDeviceRoles(storage.BdevRoleAll)
				}
			}

			svc := newTestMgmtSvc(t, log)
			sp := storage.MockProvider(log, 0, &engineCfg.Storage, nil, nil, nil, nil)
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
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)
	defaultTierRatios := []float64{0.06, 0.94}

	for name, tc := range map[string]struct {
		mgmtSvc        *mgmtSvc
		setupMockDrpc  func(_ *mgmtSvc, _ error)
		targetCount    int
		memberCount    int
		mdonssdEnabled bool
		req            *mgmtpb.PoolCreateReq
		drpcRet        *mgmtpb.PoolCreateResp
		expResp        *mgmtpb.PoolCreateResp
		expErr         error
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
				TierBytes:  []uint64{100 * humanize.GiByte, 0},
				Properties: testPoolLabelProp(),
			},
			expErr: errNotReplica,
		},
		"not MS replica": {
			mgmtSvc:     notAP,
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 0},
				Properties: testPoolLabelProp(),
			},
			expErr: errNotReplica,
		},
		"dRPC send fails": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 0},
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			targetCount: 0,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 0},
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
			},
			expErr: errors.New("unmarshal"),
		},
		"successful creation": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Properties: testPoolLabelProp(),
			},
			drpcRet: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				TgtRanks:  []uint32{0, 1},
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				TgtRanks:  []uint32{0, 1},
			},
		},
		"create with memory file ratio; mdonssd not enabled": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				MemRatio:   storage.DefaultMemoryFileRatio,
				Properties: testPoolLabelProp(),
			},
			expErr: errors.New("MD-on-SSD has not been enabled"),
		},
		"successful creation with memory file ratio": {
			targetCount:    8,
			mdonssdEnabled: true,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				MemRatio:   storage.DefaultMemoryFileRatio,
				Properties: testPoolLabelProp(),
			},
			drpcRet: &mgmtpb.PoolCreateResp{
				TierBytes:    []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				MemFileBytes: 50 * humanize.GiByte,
				TgtRanks:     []uint32{0, 1},
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes:    []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				MemFileBytes: 50 * humanize.GiByte,
				TgtRanks:     []uint32{0, 1},
			},
		},
		"successful creation minimum size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid: test.MockUUID(1),
				TierBytes: []uint64{
					engine.ScmMinBytesPerTarget * 8,
					engine.NvmeMinBytesPerTarget * 8,
				},
				Properties: testPoolLabelProp(),
			},
			drpcRet: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{
					engine.ScmMinBytesPerTarget * 8,
					engine.NvmeMinBytesPerTarget * 8,
				},
				TgtRanks: []uint32{0, 1},
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{
					engine.ScmMinBytesPerTarget * 8,
					engine.NvmeMinBytesPerTarget * 8,
				},
				TgtRanks: []uint32{0, 1},
			},
		},
		"successful creation auto size": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TotalBytes: 100 * humanize.GiByte,
				TierRatio:  defaultTierRatios,
				Properties: testPoolLabelProp(),
			},
			drpcRet: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{
					(100 * humanize.GiByte * DefaultPoolScmRatio) / 2,
					(100 * humanize.GiByte * DefaultPoolNvmeRatio) / 2,
				},
				TgtRanks: []uint32{0, 1},
			},
			expResp: &mgmtpb.PoolCreateResp{
				TierBytes: []uint64{
					(100 * humanize.GiByte * DefaultPoolScmRatio) / 2,
					(100 * humanize.GiByte * DefaultPoolNvmeRatio) / 2,
				},
				TgtRanks: []uint32{0, 1},
			},
		},
		"failed creation invalid ranks": {
			targetCount: 1,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				Ranks:      []uint32{40, 11},
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidRanks([]ranklist.Rank{11, 40}),
		},
		"failed creation invalid number of ranks": {
			targetCount: 1,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TierBytes:  []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
				NumRanks:   3,
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidNumRanks(3, 2),
		},
		"svc replicas > max": {
			targetCount: 1,
			memberCount: MaxPoolServiceReps + 2,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TotalBytes: 100 * humanize.GiByte,
				TierRatio:  defaultTierRatios,
				NumSvcReps: MaxPoolServiceReps + 2,
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidServiceReps(uint32(MaxPoolServiceReps)),
		},
		"svc replicas > numRanks": {
			targetCount: 1,
			memberCount: MaxPoolServiceReps - 2,
			req: &mgmtpb.PoolCreateReq{
				Uuid:       test.MockUUID(1),
				TotalBytes: 100 * humanize.GiByte,
				TierRatio:  defaultTierRatios,
				NumSvcReps: MaxPoolServiceReps - 1,
				Properties: testPoolLabelProp(),
			},
			expErr: FaultPoolInvalidServiceReps(uint32(MaxPoolServiceReps - 2)),
		},
		"no label": {
			targetCount: 8,
			req: &mgmtpb.PoolCreateReq{
				Uuid:      test.MockUUID(1),
				TierBytes: []uint64{100 * humanize.GiByte, 10 * humanize.TByte},
			},
			expErr: FaultPoolNoLabel,
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tier := storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("foo", "bar")
				if tc.mdonssdEnabled {
					tier.WithBdevDeviceRoles(7)
				}
				engineCfg := engine.MockConfig().
					WithTargetCount(16).
					WithStorage(tier)
				mp := storage.NewProvider(log, 0, &engineCfg.Storage,
					nil, nil, nil, nil)
				tc.mgmtSvc = newTestMgmtSvcWithProvider(t, log, mp)
			}

			numMembers := tc.memberCount
			if numMembers < 1 {
				numMembers = 2
			}
			for i := 0; i < numMembers; i++ {
				mm := system.MockMember(t, uint32(i), system.MemberStateJoined)
				if _, err := tc.mgmtSvc.membership.Add(mm); err != nil {
					t.Fatal(err)
				}
			}

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupSvcDrpcClient(svc, 0, getMockDrpcClient(tc.drpcRet, tc.expErr))
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			pcCtx, pcCancel := context.WithTimeout(test.Context(t), 260*time.Millisecond)
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
	sp := storage.NewProvider(log, 0, &ec.Storage, nil, nil, nil, nil)
	mgmtSvc.harness.instances[0] = newTestEngine(log, false, sp, ec)

	dc := newMockDrpcClient(&mockDrpcClientConfig{IsConnectedBool: true})
	dc.cfg.setSendMsgResponse(drpc.Status_SUCCESS, nil, nil)
	mgmtSvc.harness.instances[0].(*EngineInstance).getDrpcClientFn = func(s string) drpc.DomainSocketClient { return dc }

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
		TotalBytes:   totalBytes,
		TierRatio:    []float64{0.06, 0.94},
		FaultDomains: fdTree,
		Properties:   testPoolLabelProp(),
	}
	wantReq := new(mgmtpb.PoolCreateReq)
	*wantReq = *req
	// Ugh. We shouldn't need to do all of this out here. Maybe create
	// a helper or otherwise find a way to focus this test on the logic
	// for avoiding downed ranks.
	wantReq.TotalBytes = 0
	wantReq.TierBytes = []uint64{
		uint64(float64(totalBytes)*DefaultPoolScmRatio) / 3,
		uint64(float64(totalBytes)*DefaultPoolNvmeRatio) / 3,
	}
	wantReq.TierRatio = nil

	_, err = mgmtSvc.PoolCreate(test.Context(t), req)
	if err != nil {
		t.Fatal(err)
	}

	// We should only be trying to create on the Joined ranks.
	wantReq.Ranks = []uint32{0, 2, 3}

	// These properties are automatically added by PoolCreate
	wantReq.Properties = append(wantReq.Properties, &mgmtpb.PoolProperty{
		Number: daos.PoolPropertyScrubMode,
		Value: &mgmtpb.PoolProperty_Numval{
			Numval: 0,
		},
	})
	wantReq.Properties = append(wantReq.Properties, &mgmtpb.PoolProperty{
		Number: daos.PoolPropertyScrubThresh,
		Value: &mgmtpb.PoolProperty_Numval{
			Numval: 0,
		},
	})

	gotReq := new(mgmtpb.PoolCreateReq)
	if err := proto.Unmarshal(dc.calls.get()[0].Body, gotReq); err != nil {
		t.Fatal(err)
	}

	cmpOpts := append(test.DefaultCmpOpts(),
		// Ensure stable ordering of properties to avoid intermittent failures.
		protocmp.SortRepeated(func(a, b *mgmtpb.PoolProperty) bool {
			return a.Number < b.Number
		}),
	)
	if diff := cmp.Diff(wantReq, gotReq, cmpOpts...); diff != "" {
		t.Fatalf("unexpected pool create req (-want, +got):\n%s\n", diff)
	}
}

func TestServer_MgmtSvc_PoolDestroy(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	creating := system.PoolServiceStateCreating
	ready := system.PoolServiceStateReady
	destroying := system.PoolServiceStateDestroying
	testPoolService := func() *system.PoolService {
		return &system.PoolService{
			PoolLabel: "test-pool",
			PoolUUID:  uuid.MustParse(mockUUID),
			Replicas:  []ranklist.Rank{0, 1, 2},
			State:     ready,
			Storage: &system.PoolServiceStorage{
				CreationRankStr: ranklist.MustCreateRankSet("0-7").String(),
			},
		}
	}
	curTestPoolSvc := new(system.PoolService)
	svcWithState := func(in *system.PoolService, state system.PoolServiceState) *system.PoolService {
		out := new(system.PoolService)
		*out = *in
		out.State = state
		return out
	}

	// Note: PoolDestroy will invoke up to three dRPCs (list-cont/evict/destroy).
	// expDrpcListContReq/expDrpcEvReq/expDrpcReq verify the request used in the last of the
	// calls. drpcResps specifies the list of responses sequentially returned over the dRPC
	// channel.
	for name, tc := range map[string]struct {
		mgmtSvc            *mgmtSvc
		poolSvcState       *system.PoolServiceState // Initial state.
		req                *mgmtpb.PoolDestroyReq
		junkResp           bool
		drpcResps          []*mockDrpcResponse // Sequential list of dRPC responses.
		expDrpcListContReq *mgmtpb.ListContReq
		expDrpcEvReq       *mgmtpb.PoolEvictReq
		expDrpcReq         *mgmtpb.PoolDestroyReq
		expResp            *mgmtpb.PoolDestroyResp
		expSvcState        *system.PoolServiceState // Expected end state.
		expErr             error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolDestroyReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolDestroyReq{},
			expErr: errors.New("empty pool id"),
		},
		"unknown uuid": {
			req:    &mgmtpb.PoolDestroyReq{Id: badMockUUID},
			expErr: errors.New("for a different pool"),
		},
		"no dRPC response": {
			req:    &mgmtpb.PoolDestroyReq{Id: mockUUID},
			expErr: errors.New("no response"),
		},
		"garbage resp": {
			req:      &mgmtpb.PoolDestroyReq{Id: mockUUID},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"error resp": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.ListContResp{},
					Error:   errNotReplica,
				},
			},
			expDrpcListContReq: &mgmtpb.ListContReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2},
			},
			expErr: errNotReplica,
		},
		// Note: evict dRPC fails as no pool service alive, remains in creating state.
		// getPoolService() returns TryAgain in resp before list-cont dRPC is issued.
		"already creating, evict dRPC fails -DER_AGAIN, remains creating": {
			req:          &mgmtpb.PoolDestroyReq{Id: mockUUID},
			poolSvcState: &creating,
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.TryAgain), // Returned from list-cont call.
			},
			expSvcState: &creating,
		},
		// getPoolService() returns error before evict dRPC is issued.
		"recursive=true, already creating, evict dRPC fails -DER_AGAIN, remains creating": {
			req:          &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			poolSvcState: &creating,
			expErr:       daos.TryAgain,
			expSvcState:  &creating,
		},
		// getPoolService() returns TryAgain during list-cont and evict but errors ignored.
		"force=true, already creating, evict dRPC fails -DER_AGAIN, remains creating": {
			req:          &mgmtpb.PoolDestroyReq{Id: mockUUID, Force: true},
			poolSvcState: &creating,
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.TryAgain), // Returned from list-cont call.
			},
			expSvcState: &creating,
		},
		// getPoolService() returns TryAgain during list-cont and evict but errors ignored.
		"force=true, recursive=true, already creating, evict dRPC fails -DER_AGAIN, destroy succeeds": {
			req: &mgmtpb.PoolDestroyReq{
				Id:        mockUUID,
				Force:     true,
				Recursive: true,
			},
			poolSvcState: &creating,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
				Force:     true,
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
		// Note: evict dRPC fails but because of Busy status, remains in ready state.
		"recursive=true, evict dRPC fails -DER_BUSY due to open handles, pool still ready": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{
						Status: int32(daos.Busy),
					},
				},
			},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.Busy), // Returned from evict call.
			},
			expSvcState: &ready,
		},
		// Note: evict dRPC fails but because of NotService status, remains in ready state.
		"recursive=true, evict dRPC fails -DER_NO_SERVICE due to open handles, pool still ready": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{
						Status: int32(daos.NoService),
					},
				},
			},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.NoService),
			},
			expSvcState: &ready,
		},
		"recursive=true, evict dRPC with engine error": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{
						Status: int32(daos.MiscError),
					},
				},
			},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.MiscError),
			},
			expSvcState: &destroying,
		},
		"force=true, recursive=true, evict dRPC fails -DER_BUSY, pool in destroying state": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Force: true, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{
						Status: int32(daos.Busy),
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{
						Status: int32(daos.MiscError),
					},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
				Force:     true,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.MiscError),
			},
			expSvcState: &destroying,
		},
		// Initial destroying state means list-containers and evict calls are skipped.
		"recursive=true, already destroying, destroy dRPC with engine error": {
			req:          &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			poolSvcState: &destroying,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{
						Status: int32(daos.MiscError),
					},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.MiscError),
			},
			expSvcState: &destroying,
		},
		"force=true, recursive=true, already destroying, destroy dRPC with engine error": {
			req: &mgmtpb.PoolDestroyReq{
				Id:        mockUUID,
				Force:     true,
				Recursive: true,
			},
			poolSvcState: &destroying,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{
						Status: int32(daos.MiscError),
					},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
				Force:     true,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.MiscError),
			},
			expSvcState: &destroying,
		},
		"evict dRPC fails -DER_NOTLEADER on first try": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{
						Status: int32(daos.NotLeader),
					},
				},
			},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.NotLeader),
			},
			expSvcState: &destroying,
		},
		"evict dRPC fails -DER_NOTREPLICA on first try": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{
						Status: int32(daos.NotReplica),
					},
				},
			},
			expDrpcEvReq: &mgmtpb.PoolEvictReq{
				Sys:          build.DefaultSystemName,
				Id:           mockUUID,
				SvcRanks:     []uint32{0, 1, 2},
				Destroy:      true,
				ForceDestroy: false,
			},
			expResp: &mgmtpb.PoolDestroyResp{
				Status: int32(daos.NotReplica),
			},
			expSvcState: &destroying,
		},
		"already destroying, destroy dRPC succeeds": {
			req:          &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			poolSvcState: &destroying,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
		"successful destroy": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.ListContResp{},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
		"force=true, successful destroy": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Force: true, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.ListContResp{
						Containers: []*mgmtpb.ListContResp_Cont{
							{
								Uuid: mockUUID,
							},
						},
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
				Force:     true,
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
		"recursive=false, list containers fails": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Error: daos.MiscError,
				},
			},
			expDrpcListContReq: &mgmtpb.ListContReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2},
			},
			expErr: daos.MiscError,
		},
		"recursive=false, containers exist; destroy refused": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.ListContResp{
						Containers: []*mgmtpb.ListContResp_Cont{
							{
								Uuid: mockUUID,
							},
						},
					},
				},
			},
			expDrpcListContReq: &mgmtpb.ListContReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2},
			},
			expErr: FaultPoolHasContainers,
		},
		"recursive=true; list containers returns true; successful destroy": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID, Recursive: true},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.ListContResp{
						Containers: []*mgmtpb.ListContResp_Cont{
							{
								Uuid: mockUUID,
							},
						},
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:       build.DefaultSystemName,
				Id:        mockUUID,
				SvcRanks:  []uint32{0, 1, 2, 3, 4, 5, 6, 7},
				Recursive: true,
			},
			expResp: &mgmtpb.PoolDestroyResp{},
		},
		"recursive=false, containers do not exist; successful destroy": {
			req: &mgmtpb.PoolDestroyReq{Id: mockUUID},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.ListContResp{},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolEvictResp{},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolDestroyResp{},
				},
			},
			expDrpcReq: &mgmtpb.PoolDestroyReq{
				Sys:      build.DefaultSystemName,
				Id:       mockUUID,
				SvcRanks: []uint32{0, 1, 2, 3, 4, 5, 6, 7},
			},
			// ListContainers RPC resp contains no containers so poolHasContainers()
			// check passes.
			expResp: &mgmtpb.PoolDestroyResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			mgmtSvc := newTestMgmtSvc(t, log)

			numMembers := 8
			for i := 0; i < numMembers; i++ {
				if _, err := mgmtSvc.membership.Add(system.MockMember(t,
					uint32(i), system.MemberStateJoined)); err != nil {
					t.Fatal(err)
				}
			}

			curTestPoolSvc = testPoolService()
			if tc.poolSvcState != nil {
				curTestPoolSvc.State = *tc.poolSvcState
			}

			lock, ctx := getPoolLockCtx(t, nil, mgmtSvc.sysdb, curTestPoolSvc.PoolUUID)
			defer lock.Release()

			if err := mgmtSvc.sysdb.AddPoolService(ctx, curTestPoolSvc); err != nil {
				t.Fatal(err)
			}

			cfg := new(mockDrpcClientConfig)
			if tc.junkResp {
				cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
			} else {
				for _, mock := range tc.drpcResps {
					cfg.setSendMsgResponseList(t, mock)
				}
			}
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(mgmtSvc, 0, mdc)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := mgmtSvc.PoolDestroy(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)

			cmpOpts := append(
				test.DefaultCmpOpts(),
				cmpopts.IgnoreTypes(system.PoolServiceStorage{}),
				cmpopts.IgnoreFields(system.PoolService{}, "LastUpdate"),
			)

			if tc.expDrpcReq != nil {
				gotReq := new(mgmtpb.PoolDestroyReq)
				if err := proto.Unmarshal(getLastMockCall(mdc).Body, gotReq); err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(tc.expDrpcReq, gotReq, cmpOpts...); diff != "" {
					t.Fatalf("unexpected destroy dRPC call (-want, +got):\n%s\n", diff)
				}
			}
			if tc.expDrpcEvReq != nil {
				gotReq := new(mgmtpb.PoolEvictReq)
				if err := proto.Unmarshal(getLastMockCall(mdc).Body, gotReq); err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(tc.expDrpcEvReq, gotReq, cmpOpts...); diff != "" {
					t.Fatalf("unexpected evict dRPC call (-want, +got):\n%s\n", diff)
				}
			}
			if tc.expDrpcListContReq != nil {
				gotReq := new(mgmtpb.ListContReq)
				if err := proto.Unmarshal(getLastMockCall(mdc).Body, gotReq); err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(tc.expDrpcListContReq, gotReq, cmpOpts...); diff != "" {
					t.Fatalf("unexpected list cont dRPC call (-want, +got):\n%s\n", diff)
				}
			}

			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			gotSvc, err := mgmtSvc.sysdb.FindPoolServiceByUUID(uuid.MustParse(mockUUID))
			if err != nil {
				if tc.expSvcState != nil || !system.IsPoolNotFound(err) {
					t.Fatalf("unexpected error: %v", err)
				}
			}
			if tc.expSvcState != nil {
				expSvc := svcWithState(curTestPoolSvc, *tc.expSvcState)
				if diff := cmp.Diff(expSvc, gotSvc, cmpOpts...); diff != "" {
					t.Fatalf("unexpected ending PS values (-want, +got)\n%s\n", diff)
				}
			} else if gotSvc != nil {
				t.Fatalf("expected pool to be destroyed, but found %+v", gotSvc)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolExtend(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)
	mockRanks := []uint32{1}
	mockFaultDomains := []uint32{0, 1, 1, 1, 1}

	for name, tc := range map[string]struct {
		nilReq      bool
		getMockDrpc func(error) *mockDrpcClient
		mgmtSvc     *mgmtSvc
		reqIn       *mgmtpb.PoolExtendReq
		drpcResp    *mgmtpb.PoolExtendResp
		expDrpcReq  *mgmtpb.PoolExtendReq
		expErr      error
	}{
		"nil request": {
			nilReq: true,
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			reqIn:  &mgmtpb.PoolExtendReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			expErr:  errNotReplica,
		},
		"not MS replica": {
			mgmtSvc: notAP,
			expErr:  errNotReplica,
		},
		"dRPC send fails": {
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			getMockDrpc: func(err error) *mockDrpcClient {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				return getMockDrpcClientBytes(badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			reqIn:  &mgmtpb.PoolExtendReq{Ranks: mockRanks},
			expErr: errors.New("empty pool id"),
		},
		"successfully extended": {
			drpcResp: &mgmtpb.PoolExtendResp{
				TierBytes: mockTierBytes,
			},
			// Expect that the last request contains updated params from ps entry.
			expDrpcReq: &mgmtpb.PoolExtendReq{
				Sys:          build.DefaultSystemName,
				SvcRanks:     mockSvcRanks,
				FaultDomains: mockFaultDomains,
				Id:           mockUUID,
				Ranks:        mockRanks,
				TierBytes:    mockTierBytes,
				MemRatio:     mockMemRatio,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.reqIn == nil && !tc.nilReq {
				tc.reqIn = &mgmtpb.PoolExtendReq{Id: mockUUID, Ranks: mockRanks}
			}
			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			addTestPoolService(t, tc.mgmtSvc.sysdb, mockPoolService)

			if tc.getMockDrpc == nil {
				tc.getMockDrpc = func(err error) *mockDrpcClient {
					return getMockDrpcClient(tc.drpcResp, err)
				}
			}
			mdc := tc.getMockDrpc(tc.expErr)
			setupSvcDrpcClient(tc.mgmtSvc, 0, mdc)

			if tc.reqIn != nil && tc.reqIn.Sys == "" {
				tc.reqIn.Sys = build.DefaultSystemName
			}

			_, err := tc.mgmtSvc.membership.Add(system.MockMember(t, 1,
				system.MemberStateJoined))
			if err != nil {
				t.Fatal(err)
			}

			gotResp, gotErr := tc.mgmtSvc.PoolExtend(test.Context(t), tc.reqIn)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.drpcResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			// Check extend gets called with correct params from PS entry.
			lastReq := new(mgmtpb.PoolExtendReq)
			if err := proto.Unmarshal(getLastMockCall(mdc).Body, lastReq); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expDrpcReq, lastReq, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected final dRPC request (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolReintegrate(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)

	for name, tc := range map[string]struct {
		nilReq      bool
		getMockDrpc func(error) *mockDrpcClient
		ms          *mgmtSvc
		members     system.Members
		reqIn       *mgmtpb.PoolReintReq
		drpcResp    *mgmtpb.PoolReintResp
		expDrpcReq  *mgmtpb.PoolReintReq
		expErr      error
	}{
		"nil request": {
			nilReq: true,
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			reqIn:  &mgmtpb.PoolReintReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			ms:     missingSB,
			expErr: errNotReplica,
		},
		"not MS replica": {
			ms:     notAP,
			expErr: errNotReplica,
		},
		"dRPC send fails": {
			expErr: errors.New("send failure"),
		},
		"garbage resp": {
			getMockDrpc: func(err error) *mockDrpcClient {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				return getMockDrpcClientBytes(badBytes, err)
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			reqIn:  &mgmtpb.PoolReintReq{Rank: 1},
			expErr: errors.New("empty pool id"),
		},
		"invalid rank": {
			members: system.Members{
				mockMember(t, 1, 2, "excluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
			},
			expErr: errors.New("ranks with invalid state: 1"),
		},
		"successfully reintegrated": {
			drpcResp: &mgmtpb.PoolReintResp{},
			// Expect that the last request contains updated params from ps entry.
			expDrpcReq: &mgmtpb.PoolReintReq{
				Sys:       build.DefaultSystemName,
				SvcRanks:  mockSvcRanks,
				Id:        mockUUID,
				Rank:      1,
				TargetIdx: []uint32{1, 2},
				TierBytes: mockTierBytes,
				MemRatio:  mockMemRatio,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.reqIn == nil && !tc.nilReq {
				tc.reqIn = &mgmtpb.PoolReintReq{
					Id:        mockUUID,
					Rank:      1,
					TargetIdx: []uint32{1, 2},
				}
			}
			if tc.members == nil {
				tc.members = system.Members{
					mockMember(t, 1, 2, "joined"),
					mockMember(t, 2, 2, "joined"),
					mockMember(t, 3, 1, "joined"),
				}
			}
			if tc.ms == nil {
				tc.ms = mgmtSystemTestSetup(t, log, tc.members, nil)
			} else {
				for _, m := range tc.members {
					if _, err := tc.ms.membership.Add(m); err != nil {
						t.Fatal(err)
					}
				}
			}
			addTestPoolService(t, tc.ms.sysdb, mockPoolService)

			if tc.getMockDrpc == nil {
				tc.getMockDrpc = func(err error) *mockDrpcClient {
					return getMockDrpcClient(tc.drpcResp, err)
				}
			}
			mdc := tc.getMockDrpc(tc.expErr)
			setupSvcDrpcClient(tc.ms, 0, mdc)

			if tc.reqIn != nil && tc.reqIn.Sys == "" {
				tc.reqIn.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := tc.ms.PoolReintegrate(test.Context(t), tc.reqIn)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.drpcResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			// Check extend gets called with correct params from PS entry.
			lastReq := new(mgmtpb.PoolReintReq)
			if err := proto.Unmarshal(getLastMockCall(mdc).Body, lastReq); err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expDrpcReq, lastReq, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected final dRPC request (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_PoolExclude(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)

	for name, tc := range map[string]struct {
		ms            *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		nilReq        bool
		req           *mgmtpb.PoolExcludeReq
		expResp       *mgmtpb.PoolExcludeResp
		expErr        error
	}{
		"nil request": {
			nilReq: true,
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolExcludeReq{Id: mockUUID, Sys: "bad", Rank: 0},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			ms:     missingSB,
			expErr: errNotReplica,
		},
		"not MS replica": {
			ms:     notAP,
			expErr: errNotReplica,
		},
		"dRPC send fails": {
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolExcludeReq{Rank: 0, TargetIdx: []uint32{1, 2}},
			expErr: errors.New("empty pool id"),
		},
		"missing rank": {
			req:    &mgmtpb.PoolExcludeReq{Id: mockUUID, Rank: 1, TargetIdx: []uint32{1, 2}},
			expErr: errors.New("invalid rank: 1"),
		},
		"successful drained": {
			expResp: &mgmtpb.PoolExcludeResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.req == nil && !tc.nilReq {
				tc.req = &mgmtpb.PoolExcludeReq{
					Id:        mockUUID,
					Rank:      0,
					TargetIdx: []uint32{1, 2},
				}
			}
			members := system.Members{mockMember(t, 0, 2, "joined")}
			if tc.ms == nil {
				tc.ms = mgmtSystemTestSetup(t, log, members, nil)
			} else {
				for _, m := range members {
					if _, err := tc.ms.membership.Add(m); err != nil {
						t.Fatal(err)
					}
				}
			}
			addTestPoolService(t, tc.ms.sysdb, mockPoolService)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupSvcDrpcClient(svc, 0, getMockDrpcClient(tc.expResp, tc.expErr))
				}
			}
			tc.setupMockDrpc(tc.ms, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := tc.ms.PoolExclude(test.Context(t), tc.req)
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
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)

	for name, tc := range map[string]struct {
		ms            *mgmtSvc
		setupMockDrpc func(_ *mgmtSvc, _ error)
		nilReq        bool
		req           *mgmtpb.PoolDrainReq
		expResp       *mgmtpb.PoolDrainResp
		expErr        error
	}{
		"nil request": {
			nilReq: true,
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req:    &mgmtpb.PoolDrainReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"missing superblock": {
			ms:     missingSB,
			expErr: errNotReplica,
		},
		"not MS replica": {
			ms:     notAP,
			expErr: errNotReplica,
		},
		"dRPC send fails": {
			expErr: errors.New("send failure"),
		},
		"zero target count": {
			expErr: errors.New("zero target count"),
		},
		"garbage resp": {
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
			},
			expErr: errors.New("unmarshal"),
		},
		"missing uuid": {
			req:    &mgmtpb.PoolDrainReq{Rank: 0, TargetIdx: []uint32{1, 2}},
			expErr: errors.New("empty pool id"),
		},
		"missing rank": {
			req:    &mgmtpb.PoolDrainReq{Id: mockUUID, Rank: 2, TargetIdx: []uint32{1, 2}},
			expErr: errors.New("invalid rank: 2"),
		},
		"successful drained": {
			expResp: &mgmtpb.PoolDrainResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.req == nil && !tc.nilReq {
				tc.req = &mgmtpb.PoolDrainReq{
					Id:        mockUUID,
					Rank:      0,
					TargetIdx: []uint32{1, 2},
				}
			}
			members := system.Members{mockMember(t, 0, 2, "joined")}
			if tc.ms == nil {
				tc.ms = mgmtSystemTestSetup(t, log, members, nil)
			} else {
				for _, m := range members {
					if _, err := tc.ms.membership.Add(m); err != nil {
						t.Fatal(err)
					}
				}
			}
			addTestPoolService(t, tc.ms.sysdb, mockPoolService)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupSvcDrpcClient(svc, 0, getMockDrpcClient(tc.expResp, tc.expErr))
				}
			}
			tc.setupMockDrpc(tc.ms, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := tc.ms.PoolDrain(test.Context(t), tc.req)
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
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []ranklist.Rank{0},
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
			expErr:  errNotReplica,
		},
		"not MS replica": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolEvictReq{Id: mockUUID},
			expErr:  errNotReplica,
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

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
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
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			addTestPoolService(t, tc.mgmtSvc.sysdb, testPoolService)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupSvcDrpcClient(svc, 0, getMockDrpcClient(tc.expResp, tc.expErr))
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := tc.mgmtSvc.PoolEvict(test.Context(t), tc.req)
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

	resp, err := svc.ListPools(test.Context(t), newTestListPoolsReq())

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
			Replicas:  []ranklist.Rank{0, 1, 2},
		},
		{
			PoolUUID:  test.MockPoolUUID(2),
			PoolLabel: "1",
			State:     system.PoolServiceStateReady,
			Replicas:  []ranklist.Rank{0, 1, 2},
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

	resp, err := svc.ListPools(test.Context(t), newTestListPoolsReq())

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

	resp, err := svc.PoolGetACL(test.Context(t), newTestGetACLReq())

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
		Acl: &mgmtpb.AccessControlList{
			Entries: []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
		},
	}
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(expectedResp, nil))

	resp, err := svc.PoolGetACL(test.Context(t), newTestGetACLReq())

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
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(nil, expectedErr))

	resp, err := svc.PoolGetACL(test.Context(t), newTestGetACLReq())

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

	setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, nil))

	resp, err := svc.PoolGetACL(test.Context(t), newTestGetACLReq())

	if resp != nil {
		t.Errorf("Expected no response, got: %+v", resp)
	}

	test.CmpErr(t, errors.New("unmarshal"), err)
}

func newTestModifyACLReq() *mgmtpb.ModifyACLReq {
	return &mgmtpb.ModifyACLReq{
		Sys: build.DefaultSystemName,
		Id:  mockUUID,
		Entries: []string{
			"A::OWNER@:rw",
		},
	}
}

func TestPoolOverwriteACL_NoMS(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	svc := newTestMgmtSvcNonReplica(t, log)

	resp, err := svc.PoolOverwriteACL(test.Context(t), newTestModifyACLReq())

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
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(nil, expectedErr))

	resp, err := svc.PoolOverwriteACL(test.Context(t), newTestModifyACLReq())

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

	setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, nil))

	resp, err := svc.PoolOverwriteACL(test.Context(t), newTestModifyACLReq())

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
		Acl: &mgmtpb.AccessControlList{
			Entries: []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
		},
	}
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(expectedResp, nil))

	resp, err := svc.PoolOverwriteACL(test.Context(t), newTestModifyACLReq())

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

	resp, err := svc.PoolUpdateACL(test.Context(t), newTestModifyACLReq())

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
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(nil, expectedErr))

	resp, err := svc.PoolUpdateACL(test.Context(t), newTestModifyACLReq())

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

	setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, nil))

	resp, err := svc.PoolUpdateACL(test.Context(t), newTestModifyACLReq())

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
		Acl: &mgmtpb.AccessControlList{
			Entries: []string{"A::OWNER@:rw", "A:g:GROUP@:r"},
		},
	}
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(expectedResp, nil))

	resp, err := svc.PoolUpdateACL(test.Context(t), newTestModifyACLReq())

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

	resp, err := svc.PoolDeleteACL(test.Context(t), newTestDeleteACLReq())

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
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(nil, expectedErr))

	resp, err := svc.PoolDeleteACL(test.Context(t), newTestDeleteACLReq())

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

	setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, nil))

	resp, err := svc.PoolDeleteACL(test.Context(t), newTestDeleteACLReq())

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
		Acl: &mgmtpb.AccessControlList{
			Entries: []string{"A::OWNER@:rw", "A:G:readers@:r"},
		},
	}
	setupSvcDrpcClient(svc, 0, getMockDrpcClient(expectedResp, nil))

	resp, err := svc.PoolDeleteACL(test.Context(t), newTestDeleteACLReq())

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("bad response (-want, +got): \n%s\n", diff)
	}
}

func TestServer_MgmtSvc_PoolQuery(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil

	allRanksDown := newTestMgmtSvc(t, log)
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
			expErr: errNotReplica,
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

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
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
				State: mgmtpb.PoolServiceState_Ready,
				Uuid:  mockUUID,
			},
		},
		"successful query (includes pre-2.6 Leader field)": {
			req: &mgmtpb.PoolQueryReq{
				Id: mockUUID,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				resp := &mgmtpb.PoolQueryResp{
					State:  mgmtpb.PoolServiceState_Ready,
					Uuid:   mockUUID,
					SvcLdr: 42,
				}
				setupMockDrpcClient(svc, resp, nil)
			},
			expResp: &mgmtpb.PoolQueryResp{
				State:  mgmtpb.PoolServiceState_Ready,
				Uuid:   mockUUID,
				SvcLdr: 42,
				Leader: 42,
			},
		},
		"successful query; mdonssd enabled": {
			req: &mgmtpb.PoolQueryReq{
				Id: mockUUID,
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				resp := &mgmtpb.PoolQueryResp{
					State:        mgmtpb.PoolServiceState_Ready,
					Uuid:         mockUUID,
					MemFileBytes: humanize.GiByte,
				}
				setupMockDrpcClient(svc, resp, nil)
			},
			expResp: &mgmtpb.PoolQueryResp{
				State:        mgmtpb.PoolServiceState_Ready,
				Uuid:         mockUUID,
				MemFileBytes: humanize.GiByte,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tier := storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("foo", "bar")
				engineCfg := engine.MockConfig().
					WithTargetCount(16).
					WithStorage(tier)
				mp := storage.NewProvider(log, 0, &engineCfg.Storage,
					nil, nil, nil, nil)
				tc.mgmtSvc = newTestMgmtSvcWithProvider(t, log, mp)
			}
			addTestPools(t, tc.mgmtSvc.sysdb, mockUUID)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupSvcDrpcClient(svc, 0,
						getMockDrpcClient(tc.expResp, tc.expErr))
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := tc.mgmtSvc.PoolQuery(test.Context(t), tc.req)
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

func getLastMockCall(mdc *mockDrpcClient) *drpc.Call {
	return mdc.SendMsgInputCall
}

func TestServer_MgmtSvc_PoolSetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		getMockDrpc func(error) *mockDrpcClient
		drpcResp    *mgmtpb.PoolSetPropResp
		req         *mgmtpb.PoolSetPropReq
		expDrpcReq  *mgmtpb.PoolSetPropReq
		expErr      error
	}{
		"wrong system": {
			req:    &mgmtpb.PoolSetPropReq{Id: mockUUID, Sys: "bad"},
			expErr: FaultWrongSystem("bad", build.DefaultSystemName),
		},
		"garbage resp": {
			req: &mgmtpb.PoolSetPropReq{
				Id: mockUUID,
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
				},
			},
			getMockDrpc: func(err error) *mockDrpcClient {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				return getMockDrpcClientBytes(badBytes, err)
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
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
					{
						Number: daos.PoolPropertySpaceReclaim,
						Value:  &mgmtpb.PoolProperty_Numval{daos.PoolSpaceReclaimDisabled},
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
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
				},
			},
			expDrpcReq: &mgmtpb.PoolSetPropReq{
				Sys:      build.DefaultSystemName,
				SvcRanks: []uint32{0},
				Id:       mockUUID,
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"0"},
					},
				},
			},
		},
		"success": {
			req: &mgmtpb.PoolSetPropReq{
				Id: mockUUID,
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
						Value:  &mgmtpb.PoolProperty_Strval{"ok"},
					},
					{
						Number: daos.PoolPropertySpaceReclaim,
						Value:  &mgmtpb.PoolProperty_Numval{daos.PoolSpaceReclaimDisabled},
					},
				},
			},
			// expect that the last request is to set the remainder of props
			expDrpcReq: &mgmtpb.PoolSetPropReq{
				Sys:      build.DefaultSystemName,
				SvcRanks: []uint32{0},
				Id:       mockUUID,
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertySpaceReclaim,
						Value:  &mgmtpb.PoolProperty_Numval{daos.PoolSpaceReclaimDisabled},
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

			if tc.getMockDrpc == nil {
				tc.getMockDrpc = func(err error) *mockDrpcClient {
					return getMockDrpcClient(tc.drpcResp, err)
				}
			}
			mdc := tc.getMockDrpc(tc.expErr)
			setupSvcDrpcClient(ms, 0, mdc)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			_, gotErr := ms.PoolSetProp(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			lastReq := new(mgmtpb.PoolSetPropReq)
			if err := proto.Unmarshal(getLastMockCall(mdc).Body, lastReq); err != nil {
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
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
					},
				},
			},
			setupMockDrpc: func(svc *mgmtSvc, err error) {
				// dRPC call returns junk in the message body
				badBytes := makeBadBytes(42)

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
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
				Properties: []*mgmtpb.PoolProperty{
					{
						Number: daos.PoolPropertyLabel,
					},
					{
						Number: daos.PoolPropertySpaceReclaim,
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
					setupSvcDrpcClient(svc, 0, getMockDrpcClient(tc.drpcResp, tc.expErr))
				}
			}
			tc.setupMockDrpc(ms, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			_, gotErr := ms.PoolGetProp(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestServer_MgmtSvc_PoolUpgrade(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	missingSB := newTestMgmtSvc(t, log)
	missingSB.harness.instances[0].(*EngineInstance)._superblock = nil
	notAP := newTestMgmtSvc(t, log)
	testPoolService := &system.PoolService{
		PoolUUID: uuid.MustParse(mockUUID),
		State:    system.PoolServiceStateReady,
		Replicas: []ranklist.Rank{0},
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
			expErr:  errNotReplica,
		},
		"not MS replica": {
			mgmtSvc: notAP,
			req:     &mgmtpb.PoolUpgradeReq{Id: mockUUID},
			expErr:  errNotReplica,
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

				setupSvcDrpcClient(svc, 0, getMockDrpcClientBytes(badBytes, err))
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
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(t, log)
			}
			addTestPoolService(t, tc.mgmtSvc.sysdb, testPoolService)

			if tc.setupMockDrpc == nil {
				tc.setupMockDrpc = func(svc *mgmtSvc, err error) {
					setupSvcDrpcClient(svc, 0, getMockDrpcClient(tc.expResp, tc.expErr))
				}
			}
			tc.setupMockDrpc(tc.mgmtSvc, tc.expErr)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := tc.mgmtSvc.PoolUpgrade(test.Context(t), tc.req)
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
