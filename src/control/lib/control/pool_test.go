//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"reflect"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
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
				TargetIdx: []uint32{1, 2, 3},
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
				TargetIdx: []uint32{1, 2, 3},
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
				TargetIdx: []uint32{1, 2, 3},
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

func strVal(s string) daos.PoolPropertyValue {
	v := daos.PoolPropertyValue{}
	v.SetString(s)
	return v
}

func TestControl_PoolCreateReq_Convert(t *testing.T) {
	req := &PoolCreateReq{
		User:       "bob",
		UserGroup:  "work",
		NumSvcReps: 2,
		TotalBytes: 1,
		TierRatio:  []float64{0.06, 0.94},
		NumRanks:   3,
		Ranks:      []ranklist.Rank{1, 2, 3},
		TierBytes:  []uint64{humanize.GiByte, 10 * humanize.GiByte},
		Properties: []*daos.PoolProperty{
			{
				Name:   "label",
				Number: daos.PoolPropertyLabel,
				Value:  strVal("foo"),
			},
		},
	}
	reqPB := new(mgmtpb.PoolCreateReq)
	if err := convert.Types(req, reqPB); err != nil {
		t.Fatal(err)
	}
	expReqPB := &mgmtpb.PoolCreateReq{
		User:       "bob",
		UserGroup:  "work",
		NumSvcReps: 2,
		TotalBytes: 1,
		TierRatio:  []float64{0.06, 0.94},
		NumRanks:   3,
		Ranks:      []uint32{1, 2, 3},
		TierBytes:  []uint64{humanize.GiByte, 10 * humanize.GiByte},
		Properties: []*mgmtpb.PoolProperty{
			{Number: 1, Value: &mgmtpb.PoolProperty_Strval{"foo"}},
		},
	}

	cmpOpt := cmpopts.IgnoreUnexported(mgmtpb.PoolCreateReq{}, mgmtpb.PoolProperty{})
	if diff := cmp.Diff(expReqPB, reqPB, cmpOpt); diff != "" {
		t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
	}
}

func TestControl_poolCreateReqChkSizes(t *testing.T) {
	tierRatios := []float64{0.06, 0.94}
	sameTierRatios := []float64{0.80, 0.80}
	tierBytes := []uint64{humanize.GiByte * 6, humanize.GiByte * 94}

	for name, tc := range map[string]struct {
		req              PoolCreateReq
		getMaxScm        uint64
		getMaxNvme       uint64
		getMaxErr        error
		expNrGetMaxCalls int
		expReq           *PoolCreateReq
		expErr           error
	}{
		"empty request": {
			expErr: errors.New("unexpected param"),
		},
		"diff tier ratios but no total": {
			req: PoolCreateReq{
				TierRatio: tierRatios,
			},
			expErr: errors.New("no total size"),
		},
		"auto-total-size": {
			req: PoolCreateReq{
				TierRatio:  tierRatios,
				TotalBytes: humanize.GiByte * 20,
			},
			expReq: &PoolCreateReq{
				TierRatio:  tierRatios,
				TotalBytes: humanize.GiByte * 20,
			},
		},
		"auto-percentage-size; not enough capacity": {
			req: PoolCreateReq{
				TierRatio: sameTierRatios,
			},
			expErr: errors.New("Not enough SCM"),
		},
		"auto-percentage-size; no nvme": {
			req: PoolCreateReq{
				TierRatio: sameTierRatios,
			},
			getMaxScm:        100 * humanize.GiByte,
			expNrGetMaxCalls: 1,
			expReq: &PoolCreateReq{
				TierBytes: []uint64{80 * humanize.GiByte, 0},
			},
		},
		"auto-percentage-size": {
			req: PoolCreateReq{
				TierRatio: sameTierRatios,
			},
			getMaxScm:        100 * humanize.GiByte,
			getMaxNvme:       200 * humanize.GiByte,
			expNrGetMaxCalls: 1,
			expReq: &PoolCreateReq{
				TierBytes: []uint64{80 * humanize.GiByte, 160 * humanize.GiByte},
			},
		},
		"manual-size": {
			req: PoolCreateReq{
				TierBytes: tierBytes,
			},
			expReq: &PoolCreateReq{
				TierBytes: tierBytes,
			},
		},
		"tier bytes and ratio": {
			req: PoolCreateReq{
				TierBytes: tierBytes,
				TierRatio: tierRatios,
			},
			expErr: errors.New("unexpected param"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			nrGetMaxCalls := 0
			getMaxPoolSz := func() (uint64, uint64, error) {
				nrGetMaxCalls++
				return tc.getMaxScm, tc.getMaxNvme, tc.getMaxErr
			}

			gotErr := poolCreateReqChkSizes(log, getMaxPoolSz, &tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expNrGetMaxCalls, nrGetMaxCalls,
				"unexpected number of calls to getMaxPoolSize()")

			cmpOpt := cmpopts.IgnoreUnexported(PoolCreateReq{},
				daos.PoolPropertyValue{})
			if tc.expReq == nil {
				t.Fatalf("expected request not given in test case %s", name)
			}
			if diff := cmp.Diff(*tc.expReq, tc.req, cmpOpt); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolCreate(t *testing.T) {
	mockTierRatios := []float64{0.06, 0.94}
	mockTierBytes := []uint64{humanize.GiByte * 6, humanize.GiByte * 94}
	validReq := &PoolCreateReq{
		TierBytes: []uint64{
			humanize.GiByte * 6,
			humanize.GiByte * 10,
		},
	}
	customPoolUUID := test.MockPoolUUID()

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolCreateReq
		expResp *PoolCreateResp
		cmpUUID bool
		expErr  error
	}{
		"local failure": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"non-retryable failure": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.IOError, nil),
				},
			},
			expErr: daos.IOError,
		},
		"missing storage params": {
			req:    &PoolCreateReq{},
			expErr: errors.New("unexpected parameters"),
		},
		"bad storage params; unequal ratios with no total bytes": {
			req: &PoolCreateReq{
				TierRatio: mockTierRatios,
			},
			expErr: errors.New("different tier ratios with no total"),
		},
		"bad storage params; missing tier ratio": {
			req: &PoolCreateReq{
				TotalBytes: humanize.GiByte * 20,
			},
			expErr: errors.New("unexpected parameters"),
		},
		"bad storage params; incorrect length tier ratio": {
			req: &PoolCreateReq{
				TierRatio:  []float64{0.06},
				TotalBytes: humanize.GiByte * 20,
			},
			expErr: errors.New("unexpected parameters"),
		},
		"bad storage params; unexpected total bytes": {
			req: &PoolCreateReq{
				TierBytes:  mockTierBytes,
				TotalBytes: humanize.GiByte * 20,
			},
			expErr: errors.New("unexpected parameters"),
		},
		"bad storage params; unexpected tier ratio": {
			req: &PoolCreateReq{
				TierBytes: mockTierBytes,
				TierRatio: mockTierRatios,
			},
			expErr: errors.New("unexpected parameters"),
		},
		"bad storage params; incorrect length tier bytes": {
			req: &PoolCreateReq{
				TierBytes: []uint64{humanize.GiByte * 20},
			},
			expErr: errors.New("unexpected parameters"),
		},
		"bad label": {
			req: &PoolCreateReq{
				TierRatio:  mockTierRatios,
				TotalBytes: humanize.GiByte * 20,
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
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TimedOut, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"create -DER_GRPVER is retried": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.GroupVersionMismatch, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"create -DER_AGAIN is retried": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", daos.TryAgain, nil),
					MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{}),
				},
			},
			expResp: &PoolCreateResp{},
		},
		"create DataPlaneNotStarted error is retried": {
			req: validReq,
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
				TierRatio:  mockTierRatios,
				TotalBytes: humanize.GiByte * 20,
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
						SvcLdr:   1,
						SvcReps:  []uint32{0, 1, 2},
						TgtRanks: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				Leader:   1,
				SvcReps:  []uint32{0, 1, 2},
				TgtRanks: []uint32{0, 1, 2},
			},
		},
		"success no props": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolCreateResp{
						SvcLdr:   2,
						SvcReps:  []uint32{0, 1, 2},
						TgtRanks: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				Leader:   2,
				SvcReps:  []uint32{0, 1, 2},
				TgtRanks: []uint32{0, 1, 2},
			},
		},
		"custom UUID": {
			req: &PoolCreateReq{
				UUID:       customPoolUUID,
				TierRatio:  mockTierRatios,
				TotalBytes: humanize.GiByte * 20,
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
						SvcLdr:   1,
						SvcReps:  []uint32{0, 1, 2},
						TgtRanks: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				UUID:     customPoolUUID.String(),
				Leader:   1,
				SvcReps:  []uint32{0, 1, 2},
				TgtRanks: []uint32{0, 1, 2},
			},
			cmpUUID: true,
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

			gotResp, gotErr := PoolCreate(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			var cmpOpts cmp.Options
			if !tc.cmpUUID {
				cmpOpts = append(cmpOpts, cmpopts.IgnoreFields(PoolCreateResp{}, "UUID"))
			}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_UpdateState(t *testing.T) {
	poolUUID := test.MockPoolUUID()

	for name, tc := range map[string]struct {
		pqr      *PoolQueryResp
		expState string
	}{
		"Pool state as Ready": {
			pqr: &PoolQueryResp{
				Status: 0,
				PoolInfo: daos.PoolInfo{
					UUID:            poolUUID,
					TotalTargets:    1,
					DisabledTargets: 0,
				},
			},
			expState: daos.PoolServiceStateReady.String(),
		},
		"Pool state as Degraded": {
			pqr: &PoolQueryResp{
				Status: 0,
				PoolInfo: daos.PoolInfo{
					UUID:            poolUUID,
					TotalTargets:    1,
					DisabledTargets: 4,
					State:           daos.PoolServiceStateReady,
				},
			},
			expState: daos.PoolServiceStateDegraded.String(),
		},
		"Pool state as Unknown": {
			pqr: &PoolQueryResp{
				Status: 0,
				PoolInfo: daos.PoolInfo{
					UUID:         poolUUID,
					TotalTargets: 0,
					State:        daos.PoolServiceStateReady,
				},
			},
			expState: daos.PoolServiceStateUnknown.String(),
		},
		"Pool state as Default": {
			pqr: &PoolQueryResp{
				Status: 0,
				PoolInfo: daos.PoolInfo{
					UUID:         poolUUID,
					TotalTargets: 1,
					State:        daos.PoolServiceStateUnknown,
				},
			},
			expState: daos.PoolServiceStateReady.String(),
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

func TestControl_PoolQueryResp_MarshalJSON(t *testing.T) {
	poolUUID := test.MockPoolUUID()

	for name, tc := range map[string]struct {
		pqr *PoolQueryResp
		exp string
	}{
		"nil": {
			exp: "null",
		},
		"null rankset": {
			pqr: &PoolQueryResp{
				Status: 42,
				PoolInfo: daos.PoolInfo{
					QueryMask:        daos.DefaultPoolQueryMask,
					State:            daos.PoolServiceStateReady,
					UUID:             poolUUID,
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					ServiceLeader:    6,
					ServiceReplicas:  []ranklist.Rank{0, 1, 2},
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
			exp: `{"query_mask":"disabled_engines,rebuild,space","state":"Ready","uuid":"` + poolUUID.String() + `","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"svc_ldr":6,"svc_reps":[0,1,2],"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8,"status":42}`,
		},
		"valid rankset": {
			pqr: &PoolQueryResp{
				Status: 42,
				PoolInfo: daos.PoolInfo{
					QueryMask:        daos.DefaultPoolQueryMask,
					State:            daos.PoolServiceStateReady,
					UUID:             poolUUID,
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					ServiceLeader:    6,
					ServiceReplicas:  []ranklist.Rank{0, 1, 2},
					EnabledRanks:     ranklist.MustCreateRankSet("[0-3,5]"),
					DisabledRanks:    &ranklist.RankSet{},
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
			exp: `{"query_mask":"disabled_engines,rebuild,space","state":"Ready","uuid":"` + poolUUID.String() + `","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"svc_ldr":6,"svc_reps":[0,1,2],"rebuild":null,"tier_stats":null,"enabled_ranks":[0,1,2,3,5],"disabled_ranks":[],"pool_layout_ver":7,"upgrade_layout_ver":8,"status":42}`,
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

func TestControl_PoolQueryResp_UnmarshalJSON(t *testing.T) {
	poolUUID := test.MockPoolUUID()

	for name, tc := range map[string]struct {
		data    string
		expResp PoolQueryResp
		expErr  error
	}{
		"null rankset": {
			data: `{"enabled_ranks":null,"disabled_ranks":null,"status":0,"uuid":"` + poolUUID.String() + `","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"svc_ldr":6,"svc_reps":null,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
			expResp: PoolQueryResp{
				Status: 0,
				PoolInfo: daos.PoolInfo{
					UUID:             poolUUID,
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					ServiceLeader:    6,
					PoolLayoutVer:    7,
					UpgradeLayoutVer: 8,
				},
			},
		},
		"valid rankset": {
			data: `{"enabled_ranks":"[0,1-3,5]","disabled_ranks":"[]","status":0,"uuid":"` + poolUUID.String() + `","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"svc_ldr":6,"svc_reps":null,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
			expResp: PoolQueryResp{
				Status: 0,
				PoolInfo: daos.PoolInfo{
					UUID:             poolUUID,
					TotalTargets:     1,
					ActiveTargets:    2,
					TotalEngines:     3,
					DisabledTargets:  4,
					Version:          5,
					ServiceLeader:    6,
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
			data:   `{"enabled_ranks":"a cow goes quack","disabled_ranks":null,"status":0,"uuid":"` + poolUUID.String() + `","total_targets":1,"active_targets":2,"total_engines":3,"disabled_targets":4,"version":5,"leader":6,"svc_reps":null,"rebuild":null,"tier_stats":null,"pool_layout_ver":7,"upgrade_layout_ver":8}`,
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
	poolUUID := test.MockPoolUUID()

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
						Uuid:             poolUUID.String(),
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
								MediaType: mgmtpb.StorageMediaType(daos.StorageMediaTypeScm),
							},
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(daos.StorageMediaTypeNvme),
							},
						},
					},
				),
			},
			expResp: &PoolQueryResp{
				PoolInfo: daos.PoolInfo{
					UUID:             poolUUID,
					TotalTargets:     42,
					ActiveTargets:    16,
					DisabledTargets:  17,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					State:            daos.PoolServiceStateDegraded,
					Rebuild: &daos.PoolRebuildStatus{
						State:   daos.PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*daos.StorageUsageStats{
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: daos.StorageMediaTypeScm,
						},
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: daos.StorageMediaTypeNvme,
						},
					},
				},
			},
		},
		"query succeeds enabled ranks": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolQueryResp{
						Uuid:             poolUUID.String(),
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
								MediaType: mgmtpb.StorageMediaType(daos.StorageMediaTypeScm),
							},
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(daos.StorageMediaTypeNvme),
							},
						},
						EnabledRanks: "[0,1,2,3,5]",
					},
				),
			},
			expResp: &PoolQueryResp{
				PoolInfo: daos.PoolInfo{
					UUID:             poolUUID,
					TotalTargets:     42,
					ActiveTargets:    16,
					DisabledTargets:  17,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					State:            daos.PoolServiceStateDegraded,
					Rebuild: &daos.PoolRebuildStatus{
						State:   daos.PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*daos.StorageUsageStats{
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: daos.StorageMediaTypeScm,
						},
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: daos.StorageMediaTypeNvme,
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
						Uuid:             poolUUID.String(),
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
								MediaType: mgmtpb.StorageMediaType(daos.StorageMediaTypeScm),
							},
							{
								Total:     123456,
								Free:      0,
								Min:       1,
								Max:       2,
								Mean:      3,
								MediaType: mgmtpb.StorageMediaType(daos.StorageMediaTypeNvme),
							},
						},
						DisabledRanks: "[]",
					},
				),
			},
			expResp: &PoolQueryResp{
				PoolInfo: daos.PoolInfo{
					UUID:             poolUUID,
					TotalTargets:     42,
					ActiveTargets:    16,
					DisabledTargets:  17,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					State:            daos.PoolServiceStateDegraded,
					Rebuild: &daos.PoolRebuildStatus{
						State:   daos.PoolRebuildStateBusy,
						Objects: 1,
						Records: 2,
					},
					TierStats: []*daos.StorageUsageStats{
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: daos.StorageMediaTypeScm,
						},
						{
							Total:     123456,
							Free:      0,
							Min:       1,
							Max:       2,
							Mean:      3,
							MediaType: daos.StorageMediaTypeNvme,
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
			propWithVal("data_thresh", "")},
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
			Number: propWithVal("data_thresh", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{4096},
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
			Number: propWithVal("scrub_freq", "").Number,
			Value:  &mgmtpb.PoolProperty_Numval{1024},
		},
		{
			Number: propWithVal("scrub_thresh", "").Number,
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
				propWithVal("data_thresh", "4096"),
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("reintegration", "no_data_sync"),
				propWithVal("rp_pda", "2"),
				propWithVal("scrub", "timed"),
				propWithVal("scrub_freq", "1024"),
				propWithVal("scrub_thresh", "0"),
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
				propWithVal("data_thresh", "4096"),
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
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
				propWithVal("data_thresh", "4096"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("rp_pda", "2"),
				propWithVal("scrub", "timed"),
				propWithVal("scrub_freq", "1024"),
				propWithVal("scrub_thresh", "0"),
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
			expData: `[{"name":"checkpoint","description":"WAL checkpointing behavior","value":"timed"},{"name":"checkpoint_freq","description":"WAL checkpointing frequency, in seconds","value":10000},{"name":"checkpoint_thresh","description":"WAL checkpoint threshold, in percentage","value":20},{"name":"ec_cell_sz","description":"EC cell size","value":4096},{"name":"ec_pda","description":"Performance domain affinity level of EC","value":1},{"name":"global_version","description":"Global Version","value":1},{"name":"label","description":"Pool label","value":"foo"},{"name":"perf_domain","description":"Pool performance domain","value":"root"},{"name":"data_thresh","description":"Data bdev threshold size","value":4096},{"name":"rd_fac","description":"Pool redundancy factor","value":1},{"name":"reclaim","description":"Reclaim strategy","value":"disabled"},{"name":"rp_pda","description":"Performance domain affinity level of RP","value":2},{"name":"scrub","description":"Checksum scrubbing mode","value":"timed"},{"name":"scrub_freq","description":"Checksum scrubbing frequency","value":1024},{"name":"scrub_thresh","description":"Checksum scrubbing threshold","value":0},{"name":"self_heal","description":"Self-healing policy","value":"exclude"},{"name":"space_rb","description":"Rebuild space ratio","value":42},{"name":"svc_list","description":"Pool service replica list","value":[0,1,2,3]},{"name":"svc_rf","description":"Pool service redundancy factor","value":3},{"name":"upgrade_status","description":"Upgrade Status","value":"in progress"}]`,
		},
		"missing props; v2_2 pool": {
			resp: []*daos.PoolProperty{
				propWithNoVal("checkpoint"),
				propWithNoVal("checkpoint_freq"),
				propWithNoVal("checkpoint_thresh"),
				propWithVal("data_thresh", "4096"),
				propWithVal("ec_cell_sz", "4096"),
				propWithVal("ec_pda", "1"),
				propWithVal("global_version", "1"),
				propWithVal("label", "foo"),
				propWithVal("perf_domain", "root"),
				propWithVal("rd_fac", "1"),
				propWithVal("reclaim", "disabled"),
				propWithVal("rp_pda", "2"),
				propWithNoVal("scrub"),
				propWithNoVal("scrub_freq"),
				propWithNoVal("scrub_thresh"),
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

func TestControl_ListPools(t *testing.T) {
	queryResp := func(i int32) *mgmtpb.PoolQueryResp {
		total := uint32(42)
		disabled := uint32(0)
		rebuildState := mgmtpb.PoolRebuildStatus_IDLE
		if i%2 == 0 {
			disabled = total - 16
			rebuildState = mgmtpb.PoolRebuildStatus_BUSY
		}
		active := uint32(total - disabled)

		return &mgmtpb.PoolQueryResp{
			Uuid:            test.MockUUID(i),
			TotalTargets:    total,
			ActiveTargets:   active,
			DisabledTargets: disabled,
			Rebuild: &mgmtpb.PoolRebuildStatus{
				State:   rebuildState,
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
	expRebuildStatus := func(i uint32) *daos.PoolRebuildStatus {
		rebuildState := daos.PoolRebuildStateIdle
		if i%2 == 0 {
			rebuildState = daos.PoolRebuildStateBusy
		}
		return &daos.PoolRebuildStatus{
			State:   rebuildState,
			Objects: 1,
			Records: 2,
		}
	}
	expTierStats := []*daos.StorageUsageStats{
		{
			Total: 123456,
			Free:  0,
			Min:   1000,
			Max:   2000,
			Mean:  1500,
		},
		{
			Total: 1234567,
			Free:  600000,
			Min:   1000,
			Max:   2000,
			Mean:  15000,
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
			expResp: &ListPoolsResp{
				QueryErrors: make(map[uuid.UUID]*PoolQueryErr),
			},
		},
		"one pool": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:    test.MockUUID(1),
								SvcReps: []uint32{1, 3, 5, 8},
								State:   daos.PoolServiceStateReady.String(),
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						State:           daos.PoolServiceStateReady,
						UUID:            test.MockPoolUUID(1),
						TotalTargets:    42,
						ActiveTargets:   42,
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						Rebuild:         expRebuildStatus(1),
						TierStats:       expTierStats,
					},
				},
				QueryErrors: make(map[uuid.UUID]*PoolQueryErr),
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
								State:   daos.PoolServiceStateReady.String(),
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
								State:   daos.PoolServiceStateReady.String(),
							},
							{
								Uuid:    test.MockUUID(2),
								SvcReps: []uint32{1, 2, 3},
								State:   daos.PoolServiceStateReady.String(),
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						State:           daos.PoolServiceStateReady,
						UUID:            test.MockPoolUUID(1),
						TotalTargets:    42,
						ActiveTargets:   42,
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						Rebuild:         expRebuildStatus(1),
						TierStats:       expTierStats,
					},
					{
						State:           daos.PoolServiceStateDegraded,
						UUID:            test.MockPoolUUID(2),
						TotalTargets:    42,
						ActiveTargets:   16,
						DisabledTargets: 26,
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						Rebuild:         expRebuildStatus(2),
						TierStats:       expTierStats,
					},
				},
				QueryErrors: make(map[uuid.UUID]*PoolQueryErr),
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
								State:        daos.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
							{
								Uuid:         test.MockUUID(2),
								SvcReps:      []uint32{1, 2, 3},
								State:        daos.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
						},
					}),
					MockMSResponse("host1", errors.New("remote failed"), nil),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						State:           daos.PoolServiceStateReady,
						UUID:            test.MockPoolUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
					},
					{
						State:           daos.PoolServiceStateDegraded,
						UUID:            test.MockPoolUUID(2),
						TotalTargets:    42,
						ActiveTargets:   16,
						DisabledTargets: 26,
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						Rebuild:         expRebuildStatus(2),
						TierStats:       expTierStats,
					},
				},
				QueryErrors: map[uuid.UUID]*PoolQueryErr{
					test.MockPoolUUID(1): {
						Error: errors.New("remote failed"),
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
								State:        daos.PoolServiceStateReady.String(),
								RebuildState: "busy",
							},
							{
								Uuid:         test.MockUUID(2),
								SvcReps:      []uint32{1, 2, 3},
								State:        daos.PoolServiceStateReady.String(),
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
				Pools: []*daos.PoolInfo{
					{
						State:           daos.PoolServiceStateReady,
						UUID:            test.MockPoolUUID(1),
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
					},
					{
						State:           daos.PoolServiceStateDegraded,
						UUID:            test.MockPoolUUID(2),
						TotalTargets:    42,
						ActiveTargets:   16,
						DisabledTargets: 26,
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
						Rebuild:         expRebuildStatus(2),
						TierStats:       expTierStats,
					},
				},
				QueryErrors: map[uuid.UUID]*PoolQueryErr{
					test.MockPoolUUID(1): {
						Status: daos.NotInit,
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
								State:   daos.PoolServiceStateReady.String(),
							},
							{
								Uuid:         test.MockUUID(2),
								SvcReps:      []uint32{1, 2, 3},
								State:        daos.PoolServiceStateDestroying.String(),
								RebuildState: "busy",
							},
						},
					}),
					MockMSResponse("host1", nil, queryResp(1)),
					MockMSResponse("host1", nil, queryResp(2)),
				},
			},
			expResp: &ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						State:           daos.PoolServiceStateReady,
						UUID:            test.MockPoolUUID(1),
						TotalTargets:    42,
						ActiveTargets:   42,
						ServiceReplicas: []ranklist.Rank{1, 3, 5, 8},
						Rebuild:         expRebuildStatus(1),
						TierStats:       expTierStats,
					},
					{
						State:           daos.PoolServiceStateDestroying,
						UUID:            test.MockPoolUUID(2),
						ServiceReplicas: []ranklist.Rank{1, 2, 3},
					},
				},
				QueryErrors: make(map[uuid.UUID]*PoolQueryErr),
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

			cmpOpts := []cmp.Option{
				cmp.Comparer(test.CmpErrBool),
			}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_getMaxPoolSize(t *testing.T) {
	devStateFaulty := storage.NvmeStateFaulty
	devStateNew := storage.NvmeStateNew
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
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  100 * humanize.GByte,
				NvmeBytes: 1 * humanize.TByte,
			},
		},
		"single MD-on-SSD server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
								NvmeRole: &storage.BdevRoles{
									storage.OptionBits(storage.BdevRoleData),
								},
							},
							Rank: 0,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  2 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
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
				ScmBytes:  100 * humanize.GByte,
				NvmeBytes: 1 * humanize.TByte,
			},
		},
		"single Ephemeral server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
								NvmeRole:    &storage.BdevRoles{storage.OptionBits(0)},
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  100 * humanize.GByte,
				NvmeBytes: 1 * humanize.TByte,
			},
		},
		"double server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  50 * humanize.GByte,
								UsableBytes: 50 * humanize.GByte,
							},
							Rank: 3,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  400 * humanize.GByte,
								UsableBytes: 400 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  300 * humanize.GByte,
								UsableBytes: 300 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 3,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  50 * humanize.GByte,
				NvmeBytes: 700 * humanize.GByte,
			},
		},
		"double server; rank filter": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.GByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  50 * humanize.GByte,
								UsableBytes: 50 * humanize.GByte,
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.GByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 5,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  400 * humanize.GByte,
								UsableBytes: 400 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  300 * humanize.GByte,
								UsableBytes: 300 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.GByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 5,
						},
					},
				},
			},
			TgtRanks: []ranklist.Rank{0, 1, 2, 4},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  50 * humanize.GByte,
				NvmeBytes: 700 * humanize.GByte,
			},
		},
		"No NVMe; single server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  100 * humanize.GByte,
				NvmeBytes: uint64(0),
			},
		},
		"No NVMe; double server": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.GByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  50 * humanize.GByte,
								UsableBytes: 50 * humanize.GByte,
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.GByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 5,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  400 * humanize.GByte,
								UsableBytes: 400 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  300 * humanize.GByte,
								UsableBytes: 300 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.GByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 4,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 5,
						},
					},
				},
			},
			TgtRanks: []ranklist.Rank{0, 1, 2, 4},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  50 * humanize.GByte,
				NvmeBytes: uint64(0),
			},
		},
		"SCM:NVMe ratio": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.TByte,
								AvailBytes:  100 * humanize.TByte,
								UsableBytes: 100 * humanize.TByte,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  100 * humanize.GByte,
				NvmeBytes: 100 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 0,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
								NvmeState:   &devStateFaulty,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("not usable"),
			},
		},
		"New NVMe device": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
								NvmeState:   &devStateNew,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				ScmBytes:  100 * humanize.GByte,
				NvmeBytes: uint64(0),
			},
		},
		"Unmounted SCM device": {
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  50 * humanize.GByte,
								UsableBytes: 50 * humanize.GByte,
							},
							Rank: 3,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  400 * humanize.GByte,
								UsableBytes: 400 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  300 * humanize.GByte,
								UsableBytes: 300 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
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
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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

			scmBytes, nvmeBytes, err := getMaxPoolSize(test.Context(t), mockInvoker, tc.TgtRanks)

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
				fmt.Sprintf("Invalid NVMe pool size: expected=%d got=%d",
					tc.ExpectedOutput.NvmeBytes,
					nvmeBytes))
			if tc.ExpectedOutput.Debug != "" {
				test.AssertTrue(t, strings.Contains(buf.String(), tc.ExpectedOutput.Debug),
					"Missing log message: "+tc.ExpectedOutput.Debug)
			}
		})
	}
}

type MockRequestsRecorderInvoker struct {
	MockInvoker
	Requests []UnaryRequest
}

func (invoker *MockRequestsRecorderInvoker) InvokeUnaryRPC(context context.Context, request UnaryRequest) (*UnaryResponse, error) {
	invoker.Requests = append(invoker.Requests, request)
	return invoker.MockInvoker.InvokeUnaryRPC(context, request)
}

func TestControl_PoolCreateAllCmd(t *testing.T) {
	type ExpectedOutput struct {
		PoolConfig MockPoolRespConfig
		WarningMsg string
		Error      error
	}

	for name, tc := range map[string]struct {
		StorageRatio     float64
		HostsConfigArray []MockHostStorageConfig
		TgtRanks         string
		ExpectedOutput   ExpectedOutput
	}{
		"single server": {
			StorageRatio: 1,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  100 * humanize.GByte,
					NvmeBytes: 1 * humanize.TByte,
				},
			},
		},
		"single server 30%": {
			StorageRatio: 0.3,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  30 * humanize.GByte,
					NvmeBytes: 300 * humanize.GByte,
				},
			},
		},
		"double server": {
			StorageRatio: 1,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  50 * humanize.GByte,
								UsableBytes: 50 * humanize.GByte,
							},
							Rank: 3,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  400 * humanize.GByte,
								UsableBytes: 400 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  300 * humanize.GByte,
								UsableBytes: 300 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 3,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0,1,2,3",
					ScmBytes:  50 * humanize.GByte,
					NvmeBytes: 700 * humanize.GByte,
				},
			},
		},
		"double server;rank filter": {
			StorageRatio: 1,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
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
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  50 * humanize.GByte,
								UsableBytes: 50 * humanize.GByte,
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 4,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 1,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  400 * humanize.GByte,
								UsableBytes: 400 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  300 * humanize.GByte,
								UsableBytes: 300 * humanize.GByte,
							},
							Rank: 2,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  2 * humanize.TByte,
								UsableBytes: 2 * humanize.TByte,
							},
							Rank: 3,
						},
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  3 * humanize.TByte,
								AvailBytes:  1 * humanize.GByte,
								UsableBytes: 1 * humanize.GByte,
							},
							Rank: 4,
						},
					},
				},
			},
			TgtRanks: "0,1,2,3",
			ExpectedOutput: ExpectedOutput{
				PoolConfig: MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0,1,2,3",
					ScmBytes:  50 * humanize.GByte,
					NvmeBytes: 700 * humanize.GByte,
				},
			},
		},
		"No NVME": {
			StorageRatio: 1,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  100 * humanize.GByte,
					NvmeBytes: uint64(0),
				},
				WarningMsg: "Creating DAOS pool without NVME storage",
			},
		},
		"SCM:NVME ratio": {
			StorageRatio: 1,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  100 * humanize.GByte,
								UsableBytes: 100 * humanize.GByte,
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.TByte,
								AvailBytes:  100 * humanize.TByte,
								UsableBytes: 100 * humanize.TByte,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  100 * humanize.GByte,
					NvmeBytes: 100 * humanize.TByte,
				},
				WarningMsg: "SCM:NVMe ratio is less than",
			},
		},
		"single server error 1%": {
			StorageRatio: 0.01,
			HostsConfigArray: []MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []MockScmConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  100 * humanize.GByte,
								AvailBytes:  uint64(1),
								UsableBytes: uint64(1),
							},
							Rank: 0,
						},
					},
					NvmeConfig: []MockNvmeConfig{
						{
							MockStorageConfig: MockStorageConfig{
								TotalBytes:  1 * humanize.TByte,
								AvailBytes:  1 * humanize.TByte,
								UsableBytes: 1 * humanize.TByte,
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("Not enough SCM storage available with ratio 1%"),
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
							},
						},
					},
				},
			}

			unaryResponse := new(UnaryResponse)
			for _, hostStorageConfig := range tc.HostsConfigArray {
				storageScanResp := MockStorageScanResp(t,
					hostStorageConfig.ScmConfig,
					hostStorageConfig.NvmeConfig)
				hostResponse := &HostResponse{
					Addr:    hostStorageConfig.HostName,
					Message: storageScanResp,
				}
				unaryResponse.Responses = append(unaryResponse.Responses, hostResponse)
			}
			mockInvokerConfig.UnaryResponseSet = append(mockInvokerConfig.UnaryResponseSet, unaryResponse)

			if tc.ExpectedOutput.PoolConfig.Ranks != "" {
				poolCreateResp := MockPoolCreateResp(t, &tc.ExpectedOutput.PoolConfig)
				hostResponse := &HostResponse{
					Addr:    tc.ExpectedOutput.PoolConfig.HostName,
					Message: poolCreateResp,
				}
				unaryResponse = new(UnaryResponse)
				unaryResponse.Responses = append(unaryResponse.Responses, hostResponse)
				mockInvokerConfig.UnaryResponseSet = append(mockInvokerConfig.UnaryResponseSet, unaryResponse)
			}

			mockInvoker := &MockRequestsRecorderInvoker{
				MockInvoker: *NewMockInvoker(log, mockInvokerConfig),
				Requests:    []UnaryRequest{},
			}

			req := &PoolCreateReq{}

			if tc.StorageRatio != 0 {
				req.TierRatio = []float64{tc.StorageRatio, tc.StorageRatio}
			}
			if tc.TgtRanks != "" {
				req.Ranks = ranklist.RanksFromUint32(mockRanks(tc.TgtRanks))
			}

			_, gotErr := PoolCreate(context.Background(), mockInvoker, req)
			test.CmpErr(t, tc.ExpectedOutput.Error, gotErr)
			if gotErr != nil {
				return
			}

			test.AssertEqual(t, len(mockInvoker.Requests), 3, "Invalid number of request sent")
			test.AssertTrue(t,
				reflect.TypeOf(mockInvoker.Requests[0]) == reflect.TypeOf(&SystemQueryReq{}),
				"Invalid request type: wanted="+reflect.TypeOf(&SystemQueryReq{}).String()+
					" got="+reflect.TypeOf(mockInvoker.Requests[0]).String())
			test.AssertTrue(t,
				reflect.TypeOf(mockInvoker.Requests[1]) == reflect.TypeOf(&StorageScanReq{}),
				"Invalid request type: wanted="+reflect.TypeOf(&StorageScanReq{}).String()+
					" got="+reflect.TypeOf(mockInvoker.Requests[1]).String())
			test.AssertTrue(t,
				reflect.TypeOf(mockInvoker.Requests[2]) == reflect.TypeOf(&PoolCreateReq{}),
				"Invalid request type: wanted="+reflect.TypeOf(&PoolCreateReq{}).String()+
					" got="+reflect.TypeOf(mockInvoker.Requests[2]).String())
			poolCreateRequest := mockInvoker.Requests[2].(*PoolCreateReq)
			test.AssertEqual(t,
				poolCreateRequest.TierBytes[0],
				tc.ExpectedOutput.PoolConfig.ScmBytes,
				"Invalid size of allocated SCM")
			test.AssertEqual(t,
				poolCreateRequest.TierBytes[1],
				tc.ExpectedOutput.PoolConfig.NvmeBytes,
				"Invalid size of allocated NVME")
			test.AssertEqual(t,
				poolCreateRequest.TotalBytes,
				uint64(0),
				"Invalid size of TotalBytes attribute: disabled with manual allocation")
			if tc.TgtRanks != "" {
				test.AssertEqual(t,
					ranklist.RankList(poolCreateRequest.Ranks).String(),
					tc.ExpectedOutput.PoolConfig.Ranks,
					"Invalid list of Ranks")
			} else {
				test.AssertEqual(t,
					ranklist.RankList(poolCreateRequest.Ranks).String(),
					"",
					"Invalid list of Ranks")
			}
			test.AssertTrue(t,
				poolCreateRequest.TierRatio == nil,
				"Invalid size of TierRatio attribute: disabled with manual allocation")
		})
	}
}
