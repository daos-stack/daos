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

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

var (
	MockACL = &AccessControlList{
		Entries: []string{
			"A::OWNER@:rw",
			"A:G:GROUP@:rw",
		},
		Owner:      "owner@",
		OwnerGroup: "group@",
	}
)

func TestControl_PoolGetACL(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolGetACLReq
		expResp *PoolGetACLResp
		expErr  error
	}{
		"local failure": {
			req: &PoolGetACLReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolGetACLReq{
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolGetACLReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("", nil, &mgmtpb.ACLResp{
					OwnerUser:  MockACL.Owner,
					OwnerGroup: MockACL.OwnerGroup,
					ACL:        MockACL.Entries,
				}),
			},
			req: &PoolGetACLReq{
				UUID: common.MockUUID(),
			},
			expResp: &PoolGetACLResp{ACL: MockACL},
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

			gotResp, gotErr := PoolGetACL(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected ACL (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolOverwriteACL(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolOverwriteACLReq
		expResp *PoolOverwriteACLResp
		expErr  error
	}{
		"local failure": {
			req: &PoolOverwriteACLReq{
				ACL:  MockACL,
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolOverwriteACLReq{
				ACL:  MockACL,
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolOverwriteACLReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"empty ACL": {
			req: &PoolOverwriteACLReq{
				UUID: common.MockUUID(),
			},
			expErr: errors.New("empty ACL"),
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("", nil, &mgmtpb.ACLResp{
					OwnerUser:  MockACL.Owner,
					OwnerGroup: MockACL.OwnerGroup,
					ACL:        MockACL.Entries,
				}),
			},
			req: &PoolOverwriteACLReq{
				ACL:  MockACL,
				UUID: common.MockUUID(),
			},
			expResp: &PoolOverwriteACLResp{ACL: MockACL},
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

			gotResp, gotErr := PoolOverwriteACL(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected ACL (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolUpdateACL(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolUpdateACLReq
		expResp *PoolUpdateACLResp
		expErr  error
	}{
		"local failure": {
			req: &PoolUpdateACLReq{
				ACL:  MockACL,
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolUpdateACLReq{
				ACL:  MockACL,
				UUID: common.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolUpdateACLReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"empty ACL": {
			req: &PoolUpdateACLReq{
				UUID: common.MockUUID(),
			},
			expErr: errors.New("empty ACL"),
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("", nil, &mgmtpb.ACLResp{
					OwnerUser:  MockACL.Owner,
					OwnerGroup: MockACL.OwnerGroup,
					ACL:        MockACL.Entries,
				}),
			},
			req: &PoolUpdateACLReq{
				ACL:  MockACL,
				UUID: common.MockUUID(),
			},
			expResp: &PoolUpdateACLResp{ACL: MockACL},
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

			gotResp, gotErr := PoolUpdateACL(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected ACL (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_PoolDeleteACL(t *testing.T) {
	testPrincipal := "Skinner@"

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *PoolDeleteACLReq
		expResp *PoolDeleteACLResp
		expErr  error
	}{
		"local failure": {
			req: &PoolDeleteACLReq{
				UUID:      common.MockUUID(),
				Principal: testPrincipal,
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolDeleteACLReq{
				UUID:      common.MockUUID(),
				Principal: testPrincipal,
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"invalid UUID": {
			req: &PoolDeleteACLReq{
				UUID:      "bad",
				Principal: testPrincipal,
			},
			expErr: errors.New("invalid UUID"),
		},
		"empty principal": {
			req: &PoolDeleteACLReq{
				UUID: common.MockUUID(),
			},
			expErr: errors.New("no principal provided"),
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("", nil, &mgmtpb.ACLResp{
					OwnerUser:  MockACL.Owner,
					OwnerGroup: MockACL.OwnerGroup,
				}),
			},
			req: &PoolDeleteACLReq{
				UUID:      common.MockUUID(),
				Principal: testPrincipal,
			},
			expResp: &PoolDeleteACLResp{
				ACL: &AccessControlList{
					Owner:      MockACL.Owner,
					OwnerGroup: MockACL.OwnerGroup,
				},
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

			gotResp, gotErr := PoolDeleteACL(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected ACL (-want, +got):\n%s\n", diff)
			}
		})
	}
}
