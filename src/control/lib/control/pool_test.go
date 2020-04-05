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
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

// mockMSResponse returns a UnaryResponse to simulate a response from a MS
// replica.
func mockMSResponse(addr string, err error, msg proto.Message) *UnaryResponse {
	return &UnaryResponse{
		fromMS: true,
		Responses: []*HostResponse{
			{
				Addr:    addr,
				Message: msg,
				Error:   err,
			},
		},
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
				UnaryResponse: mockMSResponse("host1", errors.New("remote failed"), nil),
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
				UnaryResponse: mockMSResponse("host1", nil,
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
				UnaryResponse: mockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolCreateReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"success": {
			req: &PoolCreateReq{
				UUID: MockUUID,
			},
			mic: &MockInvokerConfig{
				UnaryResponse: mockMSResponse("host1", nil,
					&mgmtpb.PoolCreateResp{
						Svcreps: []uint32{0, 1, 2},
					},
				),
			},
			expResp: &PoolCreateResp{
				UUID:    MockUUID,
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

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
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
				UnaryResponse: mockMSResponse("host1", errors.New("remote failed"), nil),
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
				UnaryResponse: mockMSResponse("host1", nil,
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
				UUID:            MockUUID,
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
