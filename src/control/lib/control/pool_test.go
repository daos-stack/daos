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

package control

import (
	"context"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestControl_PoolDestroy(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *PoolDestroyReq
		expErr error
	}{
		"local failure": {
			req: &PoolDestroyReq{
				UUID: MockUUID,
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolDestroyReq{
				UUID: MockUUID,
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
		"success": {
			req: &PoolDestroyReq{
				UUID: MockUUID,
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
				UUID:      MockUUID,
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
				UUID:      MockUUID,
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
				UUID:      MockUUID,
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
				UUID: MockUUID,
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolEvictReq{
				UUID: MockUUID,
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
				UUID: MockUUID,
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
			req: &PoolCreateReq{},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolCreateReq{},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"success": {
			req: &PoolCreateReq{},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.PoolCreateResp{
						Svcreps: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				SvcReps: []uint32{0, 1, 2},
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
						Uuid:            MockUUID,
						Totaltargets:    42,
						Activetargets:   16,
						Disabledtargets: 17,
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
				UUID: MockUUID,
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
					UUID: MockUUID,
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
		UUID:     MockUUID,
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
				UUID:     MockUUID,
				Property: "",
			},
			expErr: errors.New("invalid property name"),
		},
		"invalid request value": {
			req: &PoolSetPropReq{
				UUID:     MockUUID,
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
				UUID:     MockUUID,
				Property: testPropName,
				Value:    testPropValStr,
			},
			expResp: &PoolSetPropResp{
				UUID:     MockUUID,
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
				UUID:     MockUUID,
				Property: testPropName,
				Value:    testPropValNum,
			},
			expResp: &PoolSetPropResp{
				UUID:     MockUUID,
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

func TestControl_ListPools(t *testing.T) {
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
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.ListPoolsResp{
						Pools: []*mgmtpb.ListPoolsResp_Pool{
							{
								Uuid:    MockUUID,
								Svcreps: []uint32{1, 3, 5, 8},
							},
						},
					},
				),
			},
			expResp: &ListPoolsResp{
				Pools: []*common.PoolDiscovery{
					{
						UUID:        MockUUID,
						SvcReplicas: []uint32{1, 3, 5, 8},
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
