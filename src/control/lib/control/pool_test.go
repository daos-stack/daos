//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
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
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func rankSetCmpOpt() []cmp.Option {
	return []cmp.Option{
		cmp.Transformer("RankSet", func(in *system.RankSet) *[]system.Rank {
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

			ctx := context.TODO()
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

			ctx := context.TODO()
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

			ctx := context.TODO()
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

			ctx := context.TODO()
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
				Properties: []*PoolProperty{
					{
						Name:   "label",
						Number: daos.PoolPropertyLabel,
						Value:  PoolPropertyValue{"yikes!"},
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
		"success": {
			req: &PoolCreateReq{
				TotalBytes: 10,
				Properties: []*PoolProperty{
					{
						Name:   "label",
						Number: daos.PoolPropertyLabel,
						Value:  PoolPropertyValue{"foo"},
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

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

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
			exp: `{"enabled_ranks":null,"disabled_ranks":null,"status":0,"uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
		},
		"valid rankset": {
			pqr: &PoolQueryResp{
				Status: 0,
				UUID:   "foo",
				PoolInfo: PoolInfo{
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					Leader:           6,
					EnabledRanks:     system.MustCreateRankSet("[0-3,5]"),
					DisabledRanks:    &system.RankSet{},
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
			exp: `{"enabled_ranks":[0,1,2,3,5],"disabled_ranks":[],"status":0,"uuid":"foo","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
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
					EnabledRanks:     system.MustCreateRankSet("[0-3,5]"),
					DisabledRanks:    &system.RankSet{},
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
				UUID: test.MockUUID(),
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
				UUID: test.MockUUID(),
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
					EnabledRanks: system.MustCreateRankSet("[0-3,5]"),
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
				UUID: test.MockUUID(),
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
					DisabledRanks: &system.RankSet{},
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

			ctx := context.TODO()
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

func propWithVal(key, val string) *PoolProperty {
	hdlr := PoolProperties()[key]
	prop := hdlr.GetProperty(key)
	if val != "" {
		if err := prop.SetValue(val); err != nil {
			panic(err)
		}
	}
	return prop
}

func TestPoolSetProp(t *testing.T) {
	defaultReq := &PoolSetPropReq{
		ID:         test.MockUUID(),
		Properties: []*PoolProperty{propWithVal("label", "foo")},
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
				Properties: []*PoolProperty{
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
				Properties: []*PoolProperty{
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
				Properties: []*PoolProperty{
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

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotErr := PoolSetProp(ctx, mi, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestPoolGetProp(t *testing.T) {
	defaultReq := &PoolGetPropReq{
		ID: test.MockUUID(),
		Properties: []*PoolProperty{propWithVal("label", ""),
			propWithVal("policy", "type=io_size")},
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolGetPropReq
		expResp []*PoolProperty
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
				Properties: []*PoolProperty{
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
				Properties: []*PoolProperty{
					propWithVal("label", ""),
				},
			},
			expErr: errors.New("unable to represent"),
		},
		"all props requested": {
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
							Number: propWithVal("rf", "").Number,
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
					},
				}),
			},
			req: &PoolGetPropReq{
				ID: test.MockUUID(),
			},
			expResp: []*PoolProperty{
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("policy", "type=io_size"),
				propWithVal("reclaim", "disabled"),
				propWithVal("rf", "1"),
				propWithVal("rp_pda", "2"),
				propWithVal("self_heal", "exclude"),
				propWithVal("space_rb", "42"),
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
				Properties: []*PoolProperty{
					propWithVal("label", ""),
					propWithVal("space_rb", ""),
				},
			},
			expResp: []*PoolProperty{
				propWithVal("label", "foo"),
				propWithVal("space_rb", "42"),
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

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := PoolGetProp(ctx, mi, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(PoolProperty{}),
				cmp.Comparer(func(a, b PoolPropertyValue) bool {
					return a.String() == b.String()
				}),
			}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
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
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateReady.String(),
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
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateReady.String(),
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateReady.String(),
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
					MockMSResponse("host1", errors.New("remote failed"), nil),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            test.MockUUID(1),
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						QueryErrorMsg:   "remote failed",
						State:           system.PoolServiceStateReady.String(),
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateReady.String(),
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
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						QueryStatusMsg:  "DER_UNINIT(-1015): Device or resource not initialized",
						State:           system.PoolServiceStateReady.String(),
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateReady.String(),
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
								Uuid:    test.MockUUID(2),
								SvcReps: []uint32{1, 2, 3},
								State:   system.PoolServiceStateDestroying.String(),
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
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
						State:           system.PoolServiceStateReady.String(),
					},
					{
						UUID:            test.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						State:           system.PoolServiceStateDestroying.String(),
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

			ctx := context.TODO()
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
	type ExpectedOutput struct {
		ScmBytes  uint64
		NvmeBytes uint64
		Error     error
		Debug     string
	}

	for name, tc := range map[string]struct {
		HostsConfigArray []MockHostStorageConfig
		ExpectedOutput   ExpectedOutput
	}{
		"single server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
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
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
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
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
						},
						{ // Check if not mounted SCM is well managed
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(0),
								AvailBytes: uint64(0),
							},
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(50) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(400) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(300) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(3) * uint64(humanize.TByte),
								AvailBytes: uint64(2) * uint64(humanize.TByte),
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
		"No NVME": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
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
		"SCM:NVME ratio": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.TByte),
								AvailBytes: uint64(100) * uint64(humanize.TByte),
							},
							Rank: 1,
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
		"No DAOS server": {
			HostsConfigArray: []MockHostStorageConfig{},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("No DAOS server available"),
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
		"Unusable NVMe device": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
								NvmeState:  new(storage.NvmeDevState),
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
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockInvokerConfig := &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{},
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
				mockInvokerConfig.UnaryResponse.Responses = append(mockInvokerConfig.UnaryResponse.Responses,
					hostResponse)
			}
			mockInvoker := NewMockInvoker(log, mockInvokerConfig)

			scmBytes, nvmeBytes, err := GetMaxPoolSize(context.TODO(), log, mockInvoker)

			if tc.ExpectedOutput.Error != nil {
				test.AssertTrue(t, err != nil, "Expected error")
				test.CmpErr(t, tc.ExpectedOutput.Error, err)
				return
			}

			test.AssertTrue(t, err == nil, "Expected no error")
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
