//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

func TestControl_PoolDestroy(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolDestroyReq
		expErr error
	}{
		"local failure": {
			req: &PoolDestroyReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolDestroyReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolDestroyReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"-DER_GRPVER is retried": {
			req: &PoolDestroyReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosGroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
				},
			},
		},
		"-DER_AGAIN is retried": {
			req: &PoolDestroyReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosTryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
				},
			},
		},
		"success": {
			req: &PoolDestroyReq{
				UUID: common.MockUUID(),
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
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotErr := PoolDestroy(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
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
				UUID:      common.MockUUID(),
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
				UUID:      common.MockUUID(),
				Rank:      2,
				Targetidx: []uint32{1, 2, 3},
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolDrainReq{
				UUID:      "bad",
				Rank:      2,
				Targetidx: []uint32{1, 2, 3},
			},
			expErr: errors.New("invalid UUID"),
		},
		"success": {
			req: &PoolDrainReq{
				UUID:      common.MockUUID(),
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
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotErr := PoolDrain(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
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
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolEvictReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolEvictReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"success": {
			req: &PoolEvictReq{
				UUID: common.MockUUID(),
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
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotErr := PoolEvict(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
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
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"non-retryable failure": {
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosIOError, nil),
				},
			},
			expErr: drpc.DaosIOError,
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
						Number: drpc.PoolPropertyLabel,
						Value:  PoolPropertyValue{"yikes!"},
					},
				},
			},
			expErr: errors.New("invalid label"),
		},
		"create -DER_TIMEDOUT is retried": {
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosTimedOut, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{
				Policy: "type=io_size",
			},
		},
		"create -DER_GRPVER is retried": {
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosGroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{
				Policy: "type=io_size",
			},
		},
		"create -DER_AGAIN is retried": {
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosTryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{
				Policy: "type=io_size",
			},
		},
		"success": {
			req: &PoolCreateReq{
				TotalBytes: 10,
				Properties: []*PoolProperty{
					{
						Name:   "label",
						Number: drpc.PoolPropertyLabel,
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
			req: &PoolCreateReq{TotalBytes: 10, PolicyString: "type=io_size"},
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
				Policy:   "type=io_size",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := PoolCreate(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
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
		"invalid UUID": {
			req: &PoolQueryReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"query succeeds": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolQueryResp{
						Uuid:            common.MockUUID(),
						TotalTargets:    42,
						ActiveTargets:   16,
						DisabledTargets: 17,
						Rebuild: &mgmtpb.PoolRebuildStatus{
							State:   mgmtpb.PoolRebuildStatus_BUSY,
							Objects: 1,
							Records: 2,
						},
						TierStats: []*mgmtpb.StorageUsageStats{
							{
								Total: 123456,
								Free:  0,
								Min:   1,
								Max:   2,
								Mean:  3,
							},
							{
								Total: 123456,
								Free:  0,
								Min:   1,
								Max:   2,
								Mean:  3,
							},
						},
					},
				),
			},
			expResp: &PoolQueryResp{
				UUID: common.MockUUID(),
				PoolInfo: PoolInfo{
					TotalTargets:    42,
					ActiveTargets:   16,
					DisabledTargets: 17,
					Rebuild: &PoolRebuildStatus{
						State:   PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*StorageUsageStats{
						{
							Total: 123456,
							Free:  0,
							Min:   1,
							Max:   2,
							Mean:  3,
						},
						{
							Total: 123456,
							Free:  0,
							Min:   1,
							Max:   2,
							Mean:  3,
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			req := tc.req
			if req == nil {
				req = &PoolQueryReq{
					UUID: common.MockUUID(),
				}
			}
			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := PoolQuery(ctx, mi, req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
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
		UUID:       common.MockUUID(),
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
		"invalid UUID": {
			req: &PoolSetPropReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"empty request properties": {
			req: &PoolSetPropReq{
				UUID: common.MockUUID(),
			},
			expErr: errors.New("empty properties list"),
		},
		"unknown property": {
			req: &PoolSetPropReq{
				UUID: common.MockUUID(),
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
				UUID: common.MockUUID(),
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
				UUID: common.MockUUID(),
				Properties: []*PoolProperty{
					propWithVal("label", "ok"),
					propWithVal("space_rb", "5"),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

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
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestPoolGetProp(t *testing.T) {
	defaultReq := &PoolGetPropReq{
		UUID:       common.MockUUID(),
		Properties: []*PoolProperty{propWithVal("label", "")},
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
		"invalid UUID": {
			req: &PoolGetPropReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"nil prop in request": {
			req: &PoolGetPropReq{
				UUID: common.MockUUID(),
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
				UUID: common.MockUUID(),
			},
			expErr: errors.New("got > 1"),
		},
		"missing prop in response": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: []*mgmtpb.PoolProperty{
						{
							Number: propWithVal("label", "").Number,
							Value:  &mgmtpb.PoolProperty_Strval{"foo"},
						},
					},
				}),
			},
			req: &PoolGetPropReq{
				UUID: common.MockUUID(),
				Properties: []*PoolProperty{
					propWithVal("label", ""),
					propWithVal("space_rb", ""),
				},
			},
			expErr: errors.New("unable to find prop"),
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
				UUID: common.MockUUID(),
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
							Number: propWithVal("reclaim", "").Number,
							Value:  &mgmtpb.PoolProperty_Numval{drpc.PoolSpaceReclaimDisabled},
						},
						{
							Number: propWithVal("self_heal", "").Number,
							Value:  &mgmtpb.PoolProperty_Numval{drpc.PoolSelfHealingAutoExclude},
						},
						{
							Number: propWithVal("ec_cell_sz", "").Number,
							Value:  &mgmtpb.PoolProperty_Numval{1024},
						},
					},
				}),
			},
			req: &PoolGetPropReq{
				UUID: common.MockUUID(),
			},
			expResp: []*PoolProperty{
				propWithVal("ec_cell_sz", "1024"),
				propWithVal("label", "foo"),
				propWithVal("reclaim", "disabled"),
				propWithVal("self_heal", "exclude"),
				propWithVal("space_rb", "42"),
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
				UUID: common.MockUUID(),
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
			defer common.ShowBufferOnFailure(t, buf)

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
			common.CmpErr(t, tc.expErr, gotErr)
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
			Uuid:            common.MockUUID(i),
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
								Uuid:    common.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            common.MockUUID(1),
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
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
								Uuid:    common.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
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
								Uuid:    common.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
							},
							{
								Uuid:    common.MockUUID(2),
								SvcReps: []uint32{1, 2, 3},
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
						UUID:            common.MockUUID(1),
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
					},
					{
						UUID:            common.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
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
								Uuid:    common.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
							},
							{
								Uuid:    common.MockUUID(2),
								SvcReps: []uint32{1, 2, 3},
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
						UUID:            common.MockUUID(1),
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						QueryErrorMsg:   "remote failed",
					},
					{
						UUID:            common.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
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
								Uuid:    common.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
							},
							{
								Uuid:    common.MockUUID(2),
								SvcReps: []uint32{1, 2, 3},
							},
						},
					}),
					MockMSResponse("host1", nil, &mgmtpb.PoolQueryResp{
						Status: int32(drpc.DaosNotInit),
					}),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            common.MockUUID(1),
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						QueryStatusMsg:  "DER_UNINIT(-1015): Device or resource not initialized",
					},
					{
						UUID:            common.MockUUID(2),
						ServiceReplicas: []system.Rank{1, 2, 3},
						TargetsTotal:    42,
						TargetsDisabled: 17,
						Usage:           expUsage,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

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
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
