//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security/auth"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func rankSetCmpOpt() []cmp.Option {
	return []cmp.Option{
		cmp.Transformer("RankSet", func(in *ranklist.RankSet) *[]ranklist.Rank {
			if in == nil {
				return nil
			}

			ranks := in.Ranks()
			return &ranks
		}),
	}
}

func TestControl_PoolDestroy(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolDestroyReq
		expErr error
	}{
		"local failure": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"-DER_GRPVER is retried": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.GroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
				},
			},
		},
		"-DER_AGAIN is retried": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
				},
			},
		},
		"ErrPoolNotFound on first try is not retried": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", system.ErrPoolUUIDNotFound(test.MockPoolUUID()), nil),
				},
			},
			expErr: system.ErrPoolUUIDNotFound(test.MockPoolUUID()),
		},
		"ErrPoolNotFound on retry is treated as success": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TimedOut, nil),
					MockMSResponse("host1", system.ErrPoolUUIDNotFound(test.MockPoolUUID()), nil),
				},
			},
		},
		"DataPlaneNotStarted error is retried": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", &fault.Fault{Code: code.ServerDataPlaneNotStarted}, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
				},
			},
		},
		"success": {
			req: &PoolDestroyReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolDestroyResp{},
				),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotErr := PoolDestroy(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestControl_PoolUpgrade(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolUpgradeReq
		expErr error
	}{
		"local failure": {
			req: &PoolUpgradeReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolUpgradeReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"-DER_GRPVER is retried": {
			req: &PoolUpgradeReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.GroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolUpgradeResp{}),
				},
			},
		},
		"-DER_AGAIN is retried": {
			req: &PoolUpgradeReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolUpgradeResp{}),
				},
			},
		},
		"success": {
			req: &PoolUpgradeReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolUpgradeResp{},
				),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotErr := PoolUpgrade(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestControl_PoolDrain(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolDrainReq
		expErr error
	}{
		"local failure": {
			req: &PoolDrainReq{
				ID:        test.MockUUID(),
				Rank:      2,
				Targetidx: []uint32{1, 2, 3},
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolDrainReq{
				ID:        test.MockUUID(),
				Rank:      2,
				Targetidx: []uint32{1, 2, 3},
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"success": {
			req: &PoolDrainReq{
				ID:        test.MockUUID(),
				Rank:      2,
				Targetidx: []uint32{1, 2, 3},
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolDrainResp{},
				),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotErr := PoolDrain(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestControl_PoolEvict(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolEvictReq
		expErr error
	}{
		"local failure": {
			req: &PoolEvictReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolEvictReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"success": {
			req: &PoolEvictReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolEvictResp{},
				),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotErr := PoolEvict(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestControl_PoolCreate(t *testing.T) {
	mockExt := auth.NewMockExtWithUser("poolTest", 0, 0)
	strVal := func(s string) daos.PoolPropertyValue {
		v := daos.PoolPropertyValue{}
		v.SetString(s)
		return v
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolCreateReq
		expResp *PoolCreateResp
		expErr  error
	}{
		"local failure": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"non-retryable failure": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.IOError, nil),
				},
			},
			expErr: daos.IOError,
		},
		"missing storage params": {
			req:    &PoolCreateReq{},
			expErr: errors.New("size of 0"),
		},
		"bad label": {
			req: &PoolCreateReq{
				TotalBytes: 10,
				Properties: []*daos.PoolProperty{
					{
						Name:   "label",
						Number: daos.PoolPropertyLabel,
						Value:  strVal("yikes!"),
					},
				},
			},
			expErr: errors.New("invalid label"),
		},
		"create -DER_TIMEDOUT is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TimedOut, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"create -DER_GRPVER is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.GroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"create -DER_AGAIN is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"create DataPlaneNotStarted error is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", &fault.Fault{Code: code.ServerDataPlaneNotStarted}, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"success": {
			req: &PoolCreateReq{
				TotalBytes: 10,
				Properties: []*daos.PoolProperty{
					{
						Name:   "label",
						Number: daos.PoolPropertyLabel,
						Value:  strVal("foo"),
					},
				},
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolCreateResp{
						SvcReps:  []uint32{0, 1, 2},
						TgtRanks: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				SvcReps:  []uint32{0, 1, 2},
				TgtRanks: []uint32{0, 1, 2},
			},
		},
		"success no props": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolCreateResp{
						SvcReps:  []uint32{0, 1, 2},
						TgtRanks: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				SvcReps:  []uint32{0, 1, 2},
				TgtRanks: []uint32{0, 1, 2},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			if tc.req.userExt == nil {
				tc.req.userExt = mockExt
			}
			gotResp, gotErr := PoolCreate(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpt := cmpopts.IgnoreFields(PoolCreateResp{}, "UUID")
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpt); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_UpdateState(t *testing.T) {
	for name, tc := range map[string]struct {
		pqr      *PoolQueryResp
		expState string
	}{
		"Pool state as Ready": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				PoolInfo: PoolInfo{
					TotalTargets:    1,
					DisabledTargets: 0,
				},
			},
			expState: system.PoolServiceStateReady.String(),
		},
		"Pool state as Degraded": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				State:  system.PoolServiceStateReady,
				PoolInfo: PoolInfo{
					TotalTargets:    1,
					DisabledTargets: 4,
				},
			},
			expState: system.PoolServiceStateDegraded.String(),
		},
		"Pool state as Unknown": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				State:  system.PoolServiceStateReady,
				PoolInfo: PoolInfo{
					TotalTargets: 0,
				},
			},
			expState: system.PoolServiceStateUnknown.String(),
		},
		"Pool state as Default": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				State:  system.PoolServiceStateUnknown,
				PoolInfo: PoolInfo{
					TotalTargets: 1,
				},
			},
			expState: system.PoolServiceStateReady.String(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.pqr.UpdateState()

			if diff := cmp.Diff(tc.expState, tc.pqr.State.String()); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolQueryResp_MarshallJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		pqr *PoolQueryResp
		exp string
	}{
		"nil": {
			exp: "null",
		},
		"null rankset": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				State:  system.PoolServiceStateReady,
				PoolInfo: PoolInfo{
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					Leader:           6,
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
			exp: `{"enabled_ranks":null,"disabled_ranks":null,"status":0,"state":"Ready","uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
		},
		"valid rankset": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				State:  system.PoolServiceStateReady,
				PoolInfo: PoolInfo{
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					Leader:           6,
					EnabledRanks:     ranklist.MustCreateRankSet("[0-3,5]"),
					DisabledRanks:    &ranklist.RankSet{},
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
			exp: `{"enabled_ranks":[0,1,2,3,5],"disabled_ranks":[],"status":0,"state":"Ready","uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := json.Marshal(tc.pqr)
			if err != nil {
				t.Fatalf("Unexpected error: %s\n", err.Error())
			}

			if diff := cmp.Diff(tc.exp, string(got)); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolQueryResp_UnmarshallJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		data    string
		expResp PoolQueryResp
		expErr  error
	}{
		"null rankset": {
			data: `{"enabled_ranks":null,"disabled_ranks":null,"status":0,"uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
			expResp: PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				PoolInfo: PoolInfo{
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					Leader:           6,
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
		},
		"valid rankset": {
			data: `{"enabled_ranks":"[0,1-3,5]","disabled_ranks":"[]","status":0,"uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
			expResp: PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				PoolInfo: PoolInfo{
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					Leader:           6,
					EnabledRanks:     ranklist.MustCreateRankSet("[0-3,5]"),
					DisabledRanks:    &ranklist.RankSet{},
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
		},
		"invalid resp": {
			data:   `a cow goes quack`,
			expErr: errors.New("invalid character"),
		},
		"invalid rankset": {
			data:   `{"enabled_ranks":"a cow goes quack","disabled_ranks":null,"status":0,"uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
			expErr: errors.New("unexpected alphabetic character(s)"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var gotResp PoolQueryResp
			err := json.Unmarshal([]byte(tc.data), &gotResp)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, rankSetCmpOpt()...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolQueryReq
		expResp *PoolQueryResp
		expErr  error
	}{
		"local failure": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"query succeeds": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolQueryResp{
						Uuid:             test.MockUUID(),
						TotalTargets:     42,
						ActiveTargets:    16,
						DisabledTargets:  17,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
						State:            mgmtpb.PoolServiceState_Degraded,
						Rebuild: &mgmtpb.PoolRebuildStatus{
							State:   mgmtpb.PoolRebuildStatus_BUSY,
							Objects: 1,
							Records: 2,
						},
						TierStats: []*mgmtpb.StorageUsageStats{
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(StorageMediaTypeScm),
							},
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(StorageMediaTypeNvme),
							},
						},
					},
				),
			},
			expResp: &PoolQueryResp{
				UUID:  test.MockUUID(),
				State: system.PoolServiceStateDegraded,
				PoolInfo: PoolInfo{
					TotalTargets:     42,
					ActiveTargets:    16,
					DisabledTargets:  17,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					Rebuild: &PoolRebuildStatus{
						State:   PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*StorageUsageStats{
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: StorageMediaTypeScm,
						},
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: StorageMediaTypeNvme,
						},
					},
				},
			},
		},
		"query succeeds enabled ranks": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolQueryResp{
						Uuid:             test.MockUUID(),
						TotalTargets:     42,
						ActiveTargets:    16,
						DisabledTargets:  17,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
						State:            mgmtpb.PoolServiceState_Degraded,
						Rebuild: &mgmtpb.PoolRebuildStatus{
							State:   mgmtpb.PoolRebuildStatus_BUSY,
							Objects: 1,
							Records: 2,
						},
						TierStats: []*mgmtpb.StorageUsageStats{
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(StorageMediaTypeScm),
							},
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(StorageMediaTypeNvme),
							},
						},
						EnabledRanks: "[0,1,2,3,5]",
					},
				),
			},
			expResp: &PoolQueryResp{
				UUID:  test.MockUUID(),
				State: system.PoolServiceStateDegraded,
				PoolInfo: PoolInfo{
					TotalTargets:     42,
					ActiveTargets:    16,
					DisabledTargets:  17,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					Rebuild: &PoolRebuildStatus{
						State:   PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*StorageUsageStats{
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: StorageMediaTypeScm,
						},
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: StorageMediaTypeNvme,
						},
					},
					EnabledRanks: ranklist.MustCreateRankSet("[0-3,5]"),
				},
			},
		},
		"query succeeds disabled ranks": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolQueryResp{
						Uuid:             test.MockUUID(),
						TotalTargets:     42,
						ActiveTargets:    16,
						DisabledTargets:  17,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
						State:            mgmtpb.PoolServiceState_Degraded,
						Rebuild: &mgmtpb.PoolRebuildStatus{
							State:   mgmtpb.PoolRebuildStatus_BUSY,
							Objects: 1,
							Records: 2,
						},
						TierStats: []*mgmtpb.StorageUsageStats{
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(StorageMediaTypeScm),
							},
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(StorageMediaTypeNvme),
							},
						},
						DisabledRanks: "[]",
					},
				),
			},
			expResp: &PoolQueryResp{
				UUID:  test.MockUUID(),
				State: system.PoolServiceStateDegraded,
				PoolInfo: PoolInfo{
					TotalTargets:     42,
					ActiveTargets:    16,
					DisabledTargets:  17,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					Rebuild: &PoolRebuildStatus{
						State:   PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*StorageUsageStats{
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: StorageMediaTypeScm,
						},
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: StorageMediaTypeNvme,
						},
					},
					DisabledRanks: &ranklist.RankSet{},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			req := tc.req
			if req == nil {
				req = &PoolQueryReq{
					ID: test.MockUUID(),
				}
			}
			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := PoolQuery(ctx, mi, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, rankSetCmpOpt()...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func propWithVal(key, val string) *daos.PoolProperty {
	hdlr := daos.PoolProperties()[key]
	prop := hdlr.GetProperty(key)
	if val != "" {
		if err := prop.SetValue(val); err != nil {
			panic(err)
		}
	}
	return prop
}

func TestControl_PoolSetProp(t *testing.T) {
	defaultReq := &PoolSetPropReq{
		ID:         test.MockUUID(),
		Properties: []*daos.PoolProperty{propWithVal("label", "foo")},
	}

	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolSetPropReq
		expErr error
	}{
		"local failure": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"empty request properties": {
			req: &PoolSetPropReq{
				ID: test.MockUUID(),
			},
			expErr: errors.New("empty properties list"),
		},
		"unknown property": {
			req: &PoolSetPropReq{
				ID: test.MockUUID(),
				Properties: []*daos.PoolProperty{
					{
						Name: "fido",
					},
				},
			},
			expErr: errors.New("unknown property"),
		},
		"bad property": {
			req: &PoolSetPropReq{
				ID: test.MockUUID(),
				Properties: []*daos.PoolProperty{
					{
						Name: "label",
					},
				},
			},
			expErr: errors.New("invalid label"),
		},
		"success": {
			req: &PoolSetPropReq{
				ID: test.MockUUID(),
				Properties: []*daos.PoolProperty{
					propWithVal("label", "ok"),
					propWithVal("space_rb", "5"),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			req := tc.req
			if req == nil {
				req = defaultReq
			}
			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotErr := PoolSetProp(ctx, mi, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func propWithNoVal(s string) *daos.PoolProperty {
	p := propWithVal(s, "")
	p.Value = daos.PoolPropertyValue{}
	return p
}

func TestControl_PoolGetProp(t *testing.T) {
	defaultReq := &PoolGetPropReq{
		ID: test.MockUUID(),
		Properties: []*daos.PoolProperty{propWithVal("label", ""),
			propWithVal("policy", "type=io_size")},
	}
	props2_2 := []*mgmtpb.PoolProperty{
		{
			Number: propWithVal("label", "").Number,
			Value:  &mgmtpb.PoolProperty_Strval{"foo"},
		},
		{
			Number: propWithVal("space_rb", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{42},
		},
		{
			Number: propWithVal("upgrade_status", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{1},
		},
		{
			Number: propWithVal("reclaim", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{daos.PoolSpaceReclaimDisabled},
		},
		{
			Number: propWithVal("self_heal", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{daos.PoolSelfHealingAutoExclude},
		},
		{
			Number: propWithVal("ec_cell_sz", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{4096},
		},
		{
			Number: propWithVal("rd_fac", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{1},
		},
		{
			Number: propWithVal("ec_pda", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{1},
		},
		{
			Number: propWithVal("global_version", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{1},
		},
		{
			Number: propWithVal("rp_pda", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{2},
		},
		{
			Number: propWithVal("policy", "").Number,
			Value:  &mgmtpb.PoolProperty_Strval{"type=io_size"},
		},
		{
			Number: propWithVal("perf_domain", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{255},
		},
		{
			Number: propWithVal("svc_list", "").Number,
			Value:  &mgmtpb.PoolProperty_Strval{"[0-3]"},
		},
	}
	props2_4 := append(props2_2, []*mgmtpb.PoolProperty{
		{
			Number: propWithVal("scrub", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{daos.PoolScrubModeTimed},
		},
		{
			Number: propWithVal("scrub-freq", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{1024},
		},
		{
			Number: propWithVal("scrub-thresh", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{0},
		},
		{
			Number: propWithVal("svc_rf", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{3},
		},
		{
			Number: propWithVal("checkpoint", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{daos.PoolCheckpointTimed},
		},
		{
			Number: propWithVal("checkpoint_freq", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{10000},
		},
		{
			Number: propWithVal("checkpoint_thresh", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{20},
		},
		{
			Number: propWithVal("reintegration", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{daos.PoolReintModeNoDataSync},
		},
	}...)

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolGetPropReq
		expResp []*daos.PoolProperty
		expErr  error
	}{
		"local failure": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"nil prop in request": {
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
				Properties: []*daos.PoolProperty{
					propWithVal("label", ""),
					nil,
					propWithVal("space_rb", ""),
				},
			},
			expErr: errors.New("nil prop"),
		},
		"duplicate props in response": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: []*mgmtpb.PoolProperty{
						{
							Number: propWithVal("label", "").Number,
							Value:  &mgmtpb.PoolProperty_Strval{"foo"},
						},
						{
							Number: propWithVal("label", "").Number,
							Value:  &mgmtpb.PoolProperty_Strval{"foo"},
						},
					},
				}),
			},
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
			},
			expErr: errors.New("got > 1"),
		},
		"nil prop value in response": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: []*mgmtpb.PoolProperty{
						{
							Number: propWithVal("label", "").Number,
							Value:  nil,
						},
					},
				}),
			},
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
				Properties: []*daos.PoolProperty{
					propWithVal("label", ""),
				},
			},
			expErr: errors.New("unable to represent"),
		},
		"all props requested": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: props2_4,
				}),
			},
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
			},
			expResp: []*daos.PoolProperty{
				propWithVal("checkpoint", "timed"),
				propWithVal("checkpoint_freq", "10000"),
				propWithVal("checkpoint_thresh", "20"),
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
				propWithVal("policy", "type=io_size"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("reintegration", "no_data_sync"),
				propWithVal("rp_pda", "2"),
				propWithVal("scrub", "timed"),
				propWithVal("scrub-freq", "1024"),
				propWithVal("scrub-thresh", "0"),
				propWithVal("self_heal", "exclude"),
				propWithVal("space_rb", "42"),
				func() *daos.PoolProperty {
					p := propWithVal("svc_list", "")
					p.Value.SetString("[0-3]")
					return p
				}(),
				propWithVal("svc_rf", "3"),
				propWithVal("upgrade_status", "in progress"),
			},
		},
		"specific props requested": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: []*mgmtpb.PoolProperty{
						{
							Number: propWithVal("label", "").Number,
							Value:  &mgmtpb.PoolProperty_Strval{"foo"},
						},
						{
							Number: propWithVal("space_rb", "").Number,
							Value:  &mgmtpb.PoolProperty_Numval{42},
						},
					},
				}),
			},
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
				Properties: []*daos.PoolProperty{
					propWithVal("label", ""),
					propWithVal("space_rb", ""),
				},
			},
			expResp: []*daos.PoolProperty{
				propWithVal("label", "foo"),
				propWithVal("space_rb", "42"),
			},
		},
		"missing props in response; compatibility with old pool": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: props2_2,
				}),
			},
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
			},
			expResp: []*daos.PoolProperty{
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
				propWithVal("policy", "type=io_size"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("rp_pda", "2"),
				propWithVal("self_heal", "exclude"),
				propWithVal("space_rb", "42"),
				func() *daos.PoolProperty {
					p := propWithVal("svc_list", "")
					p.Value.SetString("[0-3]")
					return p
				}(),
				propWithVal("upgrade_status", "in progress"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			req := tc.req
			if req == nil {
				req = defaultReq
			}
			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := PoolGetProp(ctx, mi, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(daos.PoolProperty{}),
				cmp.Comparer(func(a, b daos.PoolPropertyValue) bool {
					return a.String() == b.String()
				}),
			}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			// Verify response can be marshalled without error.
			_, err := json.Marshal(gotResp)
			if err != nil {
				t.Fatalf("Unexpected error: %s\n", err.Error())
			}
		})
	}
}

func TestControl_PoolGetPropResp_MarshalJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		resp    []*daos.PoolProperty
		expData string
		expErr  error
	}{
		"nil": {
			expData: "null",
		},
		"all props": {
			resp: []*daos.PoolProperty{
				propWithVal("checkpoint", "timed"),
				propWithVal("checkpoint_freq", "10000"),
				propWithVal("checkpoint_thresh", "20"),
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
				propWithVal("policy", "type=io_size"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("rp_pda", "2"),
				propWithVal("scrub", "timed"),
				propWithVal("scrub-freq", "1024"),
				propWithVal("scrub-thresh", "0"),
				propWithVal("self_heal", "exclude"),
				propWithVal("space_rb", "42"),
				func() *daos.PoolProperty {
					p := propWithVal("svc_list", "")
					p.Value.SetString("[0-3]")
					return p
				}(),
				propWithVal("svc_rf", "3"),
				propWithVal("upgrade_status", "in progress"),
			},
			expData: `[{"name":"checkpoint","description":"WAL Checkpointing behavior","value":"timed"},{"name":"checkpoint_freq","description":"WAL Checkpointing frequency, in seconds","value":10000},{"name":"checkpoint_thresh","description":"Usage of WAL before checkpoint is triggered, as a percentage","value":20},{"name":"ec_cell_sz","description":"EC cell size","value":4096},{"name":"ec_pda","description":"Performance domain affinity level of EC","value":1},{"name":"global_version","description":"Global Version","value":1},{"name":"label","description":"Pool label","value":"foo"},{"name":"perf_domain","description":"Pool performance domain","value":"root"},{"name":"policy","description":"Tier placement policy","value":"type=io_size"},{"name":"rd_fac","description":"Pool redundancy factor","value":1},{"name":"reclaim","description":"Reclaim strategy","value":"disabled"},{"name":"rp_pda","description":"Performance domain affinity level of RP","value":2},{"name":"scrub","description":"Checksum scrubbing mode","value":"timed"},{"name":"scrub-freq","description":"Checksum scrubbing frequency","value":1024},{"name":"scrub-thresh","description":"Checksum scrubbing threshold","value":0},{"name":"self_heal","description":"Self-healing policy","value":"exclude"},{"name":"space_rb","description":"Rebuild space ratio","value":42},{"name":"svc_list","description":"Pool service replica list","value":[0,1,2,3]},{"name":"svc_rf","description":"Pool service redundancy factor","value":3},{"name":"upgrade_status","description":"Upgrade Status","value":"in progress"}]`,
		},
		"missing props; v2_2 pool": {
			resp: []*daos.PoolProperty{
				propWithNoVal("checkpoint"),
				propWithNoVal("checkpoint_freq"),
				propWithNoVal("checkpoint_thresh"),
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
				propWithVal("policy", "type=io_size"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("rp_pda", "2"),
				propWithNoVal("scrub"),
				propWithNoVal("scrub-freq"),
				propWithNoVal("scrub-thresh"),
				propWithVal("self_heal", "exclude"),
				propWithVal("space_rb", "42"),
				func() *daos.PoolProperty {
					p := propWithVal("svc_list", "")
					p.Value.SetString("[0-3]")
					return p
				}(),
				propWithNoVal("svc_rf"),
				propWithVal("upgrade_status", "in progress"),
			},
			expErr: errors.New("value not set"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotData, gotErr := json.Marshal(tc.resp)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expData, string(gotData)); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_Pool_setUsage(t *testing.T) {
	for name, tc := range map[string]struct {
		status        int32
		scmStats      *StorageUsageStats
		nvmeStats     *StorageUsageStats
		totalTargets  uint32
		activeTargets uint32
		expPool       *Pool
		expErr        error
	}{
		"successful query": {
			scmStats: &StorageUsageStats{
				Total: humanize.GByte * 30,
				Free:  humanize.GByte * 15,
				Min:   humanize.GByte * 1.6,
				Max:   humanize.GByte * 2,
			},
			nvmeStats: &StorageUsageStats{
				Total: humanize.GByte * 500,
				Free:  humanize.GByte * 250,
				Min:   humanize.GByte * 29.5,
				Max:   humanize.GByte * 36,
			},
			totalTargets:  8,
			activeTargets: 8,
			expPool: &Pool{
				Usage: []*PoolTierUsage{
					{
						TierName:  "SCM",
						Size:      humanize.GByte * 30,
						Free:      humanize.GByte * 15,
						Imbalance: 10,
					},
					{
						TierName:  "NVME",
						Size:      humanize.GByte * 500,
						Free:      humanize.GByte * 250,
						Imbalance: 10,
					},
				},
			},
		},
		"disabled targets": {
			scmStats: &StorageUsageStats{
				Total: humanize.GByte * 30,
				Free:  humanize.GByte * 15,
				Min:   humanize.GByte * 1.6,
				Max:   humanize.GByte * 2,
			},
			nvmeStats: &StorageUsageStats{
				Total: humanize.GByte * 500,
				Free:  humanize.GByte * 250,
				Min:   humanize.GByte * 29.5,
				Max:   humanize.GByte * 36,
			},
			totalTargets:  8,
			activeTargets: 4,
			expPool: &Pool{
				Usage: []*PoolTierUsage{
					{
						TierName:  "SCM",
						Size:      humanize.GByte * 30,
						Free:      humanize.GByte * 15,
						Imbalance: 5,
					},
					{
						TierName:  "NVME",
						Size:      humanize.GByte * 500,
						Free:      humanize.GByte * 250,
						Imbalance: 5,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			resp := &PoolQueryResp{Status: tc.status}
			resp.TierStats = append(resp.TierStats, tc.scmStats, tc.nvmeStats)
			resp.TotalTargets = tc.totalTargets
			resp.ActiveTargets = tc.activeTargets
			resp.DisabledTargets = tc.activeTargets

			pool := new(Pool)
			pool.setUsage(resp)

			if diff := cmp.Diff(tc.expPool, pool); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_ListPools(t *testing.T) {
	queryResp := func(i int32) *mgmtpb.PoolQueryResp {
		return &mgmtpb.PoolQueryResp{
			Uuid:            test.MockUUID(i),
			TotalTargets:    42,
			ActiveTargets:   16,
			DisabledTargets: 17,
			Rebuild: &mgmtpb.PoolRebuildStatus{
				State:   mgmtpb.PoolRebuildStatus_BUSY,
				Objects: 1,
				Records: 2,
			},
			TierStats: []*mgmtpb.StorageUsageStats{
				{Total: 123456,
					Free: 0,
					Min:  1000,
					Max:  2000,
					Mean: 1500,
				},
				{
					Total: 1234567,
					Free:  600000,
					Min:   1000,
					Max:   2000,
					Mean:  15000,
				},
			},
		}
	}
	expUsage := []*PoolTierUsage{
		{
			TierName:  "SCM",
			Size:      123456,
			Free:      0,
			Imbalance: 12,
		},
		{
			TierName:  "NVME",
			Size:      1234567,
			Free:      600000,
			Imbalance: 1,
		},
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *ListPoolsReq
		expResp *ListPoolsResp
		expErr  error
	}{
		"local failure": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"no pools": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{},
					},
				),
			},
			expResp: &ListPoolsResp{},
		},
		"one pool": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:    test.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
								State:   system.PoolServiceStateReady.String(),
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            test.MockUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateDegraded.String(),
						RebuildState:    "busy",
					},
				},
			},
		},
		"one pool; uuid mismatch in query response": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:    test.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
								State:   system.PoolServiceStateReady.String(),
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expErr: errors.New("uuid does not match"),
		},
		"two pools": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:    test.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
								State:   system.PoolServiceStateReady.String(),
							},
							{
								Uuid:    test.MockUUID(2),
								SvcReps: []uint32{1, 2, 3},
								State:   system.PoolServiceStateReady.String(),
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            test.MockUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateDegraded.String(),
						RebuildState:    "busy",
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateDegraded.String(),
						RebuildState:    "busy",
					},
				},
			},
		},
		"two pools; one query has error": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:         test.MockUUID(1),
								SvcReps:      []uint32{1, 3, 5, 8},
								State:        system.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
							{
								Uuid:         test.MockUUID(2),
								SvcReps:      []uint32{1, 2, 3},
								State:        system.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
						},
					}),
					MockMSResponse("host1", errors.New("remote failed"), nil),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            test.MockUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						QueryErrorMsg:   "remote failed",
						State:           system.PoolServiceStateReady.String(),
						RebuildState:    "busy",
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateDegraded.String(),
						RebuildState:    "busy",
					},
				},
			},
		},
		"two pools; one query has bad status": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:         test.MockUUID(1),
								SvcReps:      []uint32{1, 3, 5, 8},
								State:        system.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
							{
								Uuid:         test.MockUUID(2),
								SvcReps:      []uint32{1, 2, 3},
								State:        system.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
						},
					}),
					MockMSResponse("host1", nil, &mgmtpb.PoolQueryResp{
						Status: int32(daos.NotInit),
					}),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            test.MockUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						QueryStatusMsg:  "DER_UNINIT(-1015): Device or resource not initialized",
						State:           system.PoolServiceStateReady.String(),
						RebuildState:    "busy",
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateDegraded.String(),
						RebuildState:    "busy",
					},
				},
			},
		},
		"two pools; one in destroying state": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:    test.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
								State:   system.PoolServiceStateReady.String(),
							},
							{
								Uuid:         test.MockUUID(2),
								SvcReps:      []uint32{1, 2, 3},
								State:        system.PoolServiceStateDestroying.String(),
								RebuildState: "busy",
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            test.MockUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateDegraded.String(),
						RebuildState:    "busy",
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						State:           system.PoolServiceStateDestroying.String(),
						RebuildState:    "busy",
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			req := tc.req
			if req == nil {
				req = &ListPoolsReq{}
			}
			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := ListPools(ctx, mi, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_GetMaxPoolSize(t *testing.T) {
	devStateFaulty := storage.NvmeStateFaulty
	type ExpectedOutput struct {
		ScmBytes   uint64
		NvmeBytes  uint64
		Error      error
		QueryError error
		Debug      string
	}

	for name, tc := range map[string]struct {
		HostsConfigArray []MockHostStorageConfig
		TgtRanks         []ranklist.Rank
		ExpectedOutput   ExpectedOutput
	}{
		"single server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(100) * uint64(humanize.GByte),
				NvmeBytes: uint64(1) * uint64(humanize.TByte),
			},
		},
		"single MD-on-SSD server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
								NvmeRole: &storage.BdevRoles{
									storage.OptionBits(storage.BdevRoleData),
								},
							},
							Rank: 0,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(2) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
								NvmeRole: &storage.BdevRoles{
									storage.OptionBits(storage.BdevRoleWAL | storage.BdevRoleMeta),
								},
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(100) * uint64(humanize.GByte),
				NvmeBytes: uint64(1) * uint64(humanize.TByte),
			},
		},
		"single Ephemeral server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
								NvmeRole:    &storage.BdevRoles{storage.OptionBits(0)},
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(100) * uint64(humanize.GByte),
				NvmeBytes: uint64(1) * uint64(humanize.TByte),
			},
		},
		"double server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
				{
					HostName: "bar[1,3]",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(50) * uint64(humanize.GByte),
								UsableBytes: uint64(50) * uint64(humanize.GByte),
							},
							Rank: 3,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(400) * uint64(humanize.GByte),
								UsableBytes: uint64(400) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(300) * uint64(humanize.GByte),
								UsableBytes: uint64(300) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(3) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 3,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(50) * uint64(humanize.GByte),
				NvmeBytes: uint64(700) * uint64(humanize.GByte),
			},
		},
		"double server; rank filter": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
				{
					HostName: "bar[1,3]",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.GByte),
								AvailBytes:  uint64(1) * uint64(humanize.GByte),
								UsableBytes: uint64(1) * uint64(humanize.GByte),
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(50) * uint64(humanize.GByte),
								UsableBytes: uint64(50) * uint64(humanize.GByte),
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.GByte),
								AvailBytes:  uint64(1) * uint64(humanize.GByte),
								UsableBytes: uint64(1) * uint64(humanize.GByte),
							},
							Rank: 5,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(400) * uint64(humanize.GByte),
								UsableBytes: uint64(400) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(300) * uint64(humanize.GByte),
								UsableBytes: uint64(300) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.GByte),
								AvailBytes:  uint64(1) * uint64(humanize.GByte),
								UsableBytes: uint64(1) * uint64(humanize.GByte),
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(3) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(3) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 5,
						},
					},
				},
			},
			TgtRanks: []ranklist.Rank{0, 1, 2, 4},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(50) * uint64(humanize.GByte),
				NvmeBytes: uint64(700) * uint64(humanize.GByte),
			},
		},
		"No NVME; single server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(100) * uint64(humanize.GByte),
				NvmeBytes: uint64(0),
			},
		},
		"No NVME; double server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
				{
					HostName: "bar",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.GByte),
								AvailBytes:  uint64(1) * uint64(humanize.GByte),
								UsableBytes: uint64(1) * uint64(humanize.GByte),
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(50) * uint64(humanize.GByte),
								UsableBytes: uint64(50) * uint64(humanize.GByte),
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.GByte),
								AvailBytes:  uint64(1) * uint64(humanize.GByte),
								UsableBytes: uint64(1) * uint64(humanize.GByte),
							},
							Rank: 5,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(400) * uint64(humanize.GByte),
								UsableBytes: uint64(400) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(300) * uint64(humanize.GByte),
								UsableBytes: uint64(300) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.GByte),
								AvailBytes:  uint64(1) * uint64(humanize.GByte),
								UsableBytes: uint64(1) * uint64(humanize.GByte),
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(3) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(3) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 5,
						},
					},
				},
			},
			TgtRanks: []ranklist.Rank{0, 1, 2, 4},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(50) * uint64(humanize.GByte),
				NvmeBytes: uint64(0),
			},
		},
		"SCM:NVME ratio": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.TByte),
								AvailBytes:  uint64(100) * uint64(humanize.TByte),
								UsableBytes: uint64(100) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(100) * uint64(humanize.GByte),
				NvmeBytes: uint64(100) * uint64(humanize.TByte),
			},
		},
		"Invalid response message": {
			HostsConfigArray: []MockHostStorageConfig{{}},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("unable to unpack message"),
			},
		},
		"empty response": {
			HostsConfigArray: []MockHostStorageConfig{},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("host storage response"),
			},
		},
		"query fails": {
			HostsConfigArray: []MockHostStorageConfig{},
			ExpectedOutput: ExpectedOutput{
				QueryError: errors.New("query whoops"),
				Error:      errors.New("query whoops"),
			},
		},
		"No SCM storage": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName:   "foo",
					ScmConfig:  []MockScmConfig{},
					NvmeConfig: []MockNvmeConfig{},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("Host without SCM storage"),
			},
		},
		"Engine with two SCM storage": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("Multiple SCM devices found for rank"),
			},
		},
		"Unusable NVMe device": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
								NvmeState:   &devStateFaulty,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  uint64(100) * uint64(humanize.GByte),
				NvmeBytes: 0,
				Debug:     "not usable",
			},
		},
		"Unmounted SCM device": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
				{
					HostName: "bar[1,3]",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(0),
								AvailBytes:  uint64(0),
								UsableBytes: uint64(0),
							},
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(50) * uint64(humanize.GByte),
								UsableBytes: uint64(50) * uint64(humanize.GByte),
							},
							Rank: 3,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(400) * uint64(humanize.GByte),
								UsableBytes: uint64(400) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(300) * uint64(humanize.GByte),
								UsableBytes: uint64(300) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(3) * uint64(humanize.TByte),
								AvailBytes:  uint64(2) * uint64(humanize.TByte),
								UsableBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 3,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("is not mounted"),
			},
		},
		"SMD without SCM": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("without SCM device and at least one SMD device"),
			},
		},
		"no SCM": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(100) * uint64(humanize.GByte),
								AvailBytes:  uint64(100) * uint64(humanize.GByte),
								UsableBytes: uint64(100) * uint64(humanize.GByte),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  uint64(1) * uint64(humanize.TByte),
								AvailBytes:  uint64(1) * uint64(humanize.TByte),
								UsableBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			TgtRanks: []ranklist.Rank{1},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("No SCM storage space available"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockInvokerConfig := &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{
						Responses: []*HostResponse{
							{
								Addr:    "foo",
								Message: &mgmtpb.SystemQueryResp{},
								Error:   tc.ExpectedOutput.QueryError,
							},
						},
					},
					{
						Responses: []*HostResponse{},
					},
				},
			}
			for _, hostStorageConfig := range tc.HostsConfigArray {
				var hostResponse *HostResponse
				if hostStorageConfig.HostName == "" {
					hostResponse = new(HostResponse)
				} else {
					storageScanResp := MockStorageScanResp(t,
						hostStorageConfig.ScmConfig,
						hostStorageConfig.NvmeConfig)
					hostResponse = &HostResponse{
						Addr:    hostStorageConfig.HostName,
						Message: storageScanResp,
					}
				}
				scanResp := mockInvokerConfig.UnaryResponseSet[1]
				scanResp.Responses = append(scanResp.Responses, hostResponse)
			}
			mockInvoker := NewMockInvoker(log, mockInvokerConfig)

			scmBytes, nvmeBytes, err := GetMaxPoolSize(test.Context(t), log, mockInvoker, tc.TgtRanks)

			if tc.ExpectedOutput.Error != nil {
				test.AssertTrue(t, err != nil, "Expected error")
				test.CmpErr(t, tc.ExpectedOutput.Error, err)
				return
			}

			test.AssertTrue(t, err == nil,
				fmt.Sprintf("Expected no error: err=%q", err))
			test.AssertEqual(t,
				tc.ExpectedOutput.ScmBytes,
				scmBytes,
				fmt.Sprintf("Invalid SCM pool size: expected=%d got=%d",
					tc.ExpectedOutput.ScmBytes,
					scmBytes))

			test.AssertEqual(t,
				tc.ExpectedOutput.NvmeBytes,
				nvmeBytes,
				fmt.Sprintf("Invalid NVME pool size: expected=%d got=%d",
					tc.ExpectedOutput.NvmeBytes,
					nvmeBytes))
			if tc.ExpectedOutput.Debug != "" {
				test.AssertTrue(t, strings.Contains(buf.String(), tc.ExpectedOutput.Debug),
					"Missing log message: "+tc.ExpectedOutput.Debug)
			}
		})
	}
}
