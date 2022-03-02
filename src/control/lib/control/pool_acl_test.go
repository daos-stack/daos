//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
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
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolGetACLReq{
				ID: test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
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
				ID: test.MockUUID(),
			},
			expResp: &PoolGetACLResp{ACL: MockACL},
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

			gotResp, gotErr := PoolGetACL(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
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
				ACL: MockACL,
				ID:  test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolOverwriteACLReq{
				ACL: MockACL,
				ID:  test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"empty ACL": {
			req: &PoolOverwriteACLReq{
				ID: test.MockUUID(),
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
				ACL: MockACL,
				ID:  test.MockUUID(),
			},
			expResp: &PoolOverwriteACLResp{ACL: MockACL},
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

			gotResp, gotErr := PoolOverwriteACL(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
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
				ACL: MockACL,
				ID:  test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolUpdateACLReq{
				ACL: MockACL,
				ID:  test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"empty ACL": {
			req: &PoolUpdateACLReq{
				ID: test.MockUUID(),
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
				ACL: MockACL,
				ID:  test.MockUUID(),
			},
			expResp: &PoolUpdateACLResp{ACL: MockACL},
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

			gotResp, gotErr := PoolUpdateACL(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
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
				ID:        test.MockUUID(),
				Principal: testPrincipal,
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &PoolDeleteACLReq{
				ID:        test.MockUUID(),
				Principal: testPrincipal,
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"empty principal": {
			req: &PoolDeleteACLReq{
				ID: test.MockUUID(),
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
				ID:        test.MockUUID(),
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
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := PoolDeleteACL(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected ACL (-want, +got):\n%s\n", diff)
			}
		})
	}
}
