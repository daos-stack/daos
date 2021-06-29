//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"strconv"
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
					MockMSResponse("host1", drpc.DaosIOError, nil),
				},
			},
			expErr: drpc.DaosIOError,
		},
		"mixture of auto/manual storage params": {
			req: &PoolCreateReq{
				TotalBytes: 10,
				ScmBytes:   20,
			},
			expErr: errors.New("can't mix"),
		},
		"missing storage params": {
			req:    &PoolCreateReq{},
			expErr: errors.New("0 SCM"),
		},
		"create -DER_TIMEDOUT is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosTimedOut, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"-DER_GRPVER is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosGroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"-DER_AGAIN is retried": {
			req: &PoolCreateReq{TotalBytes: 10},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", drpc.DaosTryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"success": {
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
						Scm: &mgmtpb.StorageUsageStats{
							Total: 123456,
							Free:  0,
							Min:   1,
							Max:   2,
							Mean:  3,
						},
						Nvme: &mgmtpb.StorageUsageStats{
							Total: 123456,
							Free:  0,
							Min:   1,
							Max:   2,
							Mean:  3,
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
					Scm: &StorageUsageStats{
						Total: 123456,
						Free:  0,
						Min:   1,
						Max:   2,
						Mean:  3,
					},
					Nvme: &StorageUsageStats{
						Total: 123456,
						Free:  0,
						Min:   1,
						Max:   2,
						Mean:  3,
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

func TestPoolSetProp(t *testing.T) {
	const (
		testPropName          = "test-prop"
		testPropValStr        = "test-val"
		testPropValNum uint64 = 42
	)
	defaultReq := &PoolSetPropReq{
		UUID:     common.MockUUID(),
		Property: testPropName,
		Value:    testPropValStr,
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolSetPropReq
		expResp *PoolSetPropResp
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
			req: &PoolSetPropReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"empty request property": {
			req: &PoolSetPropReq{
				UUID:     common.MockUUID(),
				Property: "",
			},
			expErr: errors.New("invalid property name"),
		},
		"invalid request value": {
			req: &PoolSetPropReq{
				UUID:     common.MockUUID(),
				Property: testPropName,
			},
			expErr: errors.New("unhandled property value"),
		},
		"wrong response message": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &MockMessage{}),
			},
			expErr: errors.New("unable to extract"),
		},
		"invalid response property": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolSetPropResp{
						Property: nil,
						Value:    nil,
					},
				),
			},
			expErr: errors.New("unable to represent response value"),
		},
		"invalid response value": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolSetPropResp{
						Property: &mgmtpb.PoolSetPropResp_Name{
							Name: testPropName,
						},
						Value: nil,
					},
				),
			},
			expErr: errors.New("unable to represent response value"),
		},
		"successful string property": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolSetPropResp{
						Property: &mgmtpb.PoolSetPropResp_Name{
							Name: testPropName,
						},
						Value: &mgmtpb.PoolSetPropResp_Strval{
							Strval: testPropValStr,
						},
					},
				),
			},
			req: &PoolSetPropReq{
				UUID:     common.MockUUID(),
				Property: testPropName,
				Value:    testPropValStr,
			},
			expResp: &PoolSetPropResp{
				UUID:     common.MockUUID(),
				Property: testPropName,
				Value:    testPropValStr,
			},
		},
		"successful numeric property": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolSetPropResp{
						Property: &mgmtpb.PoolSetPropResp_Name{
							Name: testPropName,
						},
						Value: &mgmtpb.PoolSetPropResp_Numval{
							Numval: testPropValNum,
						},
					},
				),
			},
			req: &PoolSetPropReq{
				UUID:     common.MockUUID(),
				Property: testPropName,
				Value:    testPropValNum,
			},
			expResp: &PoolSetPropResp{
				UUID:     common.MockUUID(),
				Property: testPropName,
				Value:    strconv.FormatUint(testPropValNum, 10),
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

			gotResp, gotErr := PoolSetProp(ctx, mi, req)
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
			resp.Scm = tc.scmStats
			resp.Nvme = tc.nvmeStats
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
			Scm: &mgmtpb.StorageUsageStats{
				Total: 123456,
				Free:  0,
				Min:   1000,
				Max:   2000,
				Mean:  1500,
			},
			Nvme: &mgmtpb.StorageUsageStats{
				Total: 1234567,
				Free:  600000,
				Min:   1000,
				Max:   2000,
				Mean:  15000,
			},
		}
	}
	queryRespNilScm := queryResp(1)
	queryRespNilScm.Scm = nil
	queryRespNilNvme := queryResp(1)
	queryRespNilNvme.Nvme = nil
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
		"one pool; nil scm in query response": {
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
					MockMSResponse("host1", nil, queryRespNilScm),
				},
			},
			expErr: errors.New("missing scm"),
		},
		"one pool; nil nvme in query response": {
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
					MockMSResponse("host1", nil, queryRespNilNvme),
				},
			},
			expErr: errors.New("missing nvme"),
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
					MockMSResponse("host1", nil, &mgmtpb.PoolQueryResp{Status: -1}),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*Pool{
					{
						UUID:            common.MockUUID(1),
						ServiceReplicas: []system.Rank{1, 3, 5, 8},
						QueryStatus:     -1,
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
