//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestControl_ContSetOwner(t *testing.T) {
	testPoolUUID := uuid.New().String()
	testContUUID := uuid.New().String()

	validReq := &ContSetOwnerReq{
		PoolUUID: testPoolUUID,
		ContUUID: testContUUID,
		User:     "someuser@",
		Group:    "somegroup@",
	}

	for name, tc := range map[string]struct {
		mic    *MockInvokerConfig
		req    *ContSetOwnerReq
		expErr error
	}{
		"nil request": {
			req:    nil,
			expErr: errors.New("nil request"),
		},
		"bad container UUID": {
			req: &ContSetOwnerReq{
				PoolUUID: testPoolUUID,
				ContUUID: "junk",
			},
			expErr: errors.New("invalid UUID"),
		},
		"bad pool UUID": {
			req: &ContSetOwnerReq{
				PoolUUID: "garbage",
				ContUUID: testContUUID,
			},
			expErr: errors.New("invalid UUID"),
		},
		"no container UUID": {
			req: &ContSetOwnerReq{
				PoolUUID: testPoolUUID,
			},
			expErr: errors.New("invalid UUID"),
		},
		"no pool UUID": {
			req: &ContSetOwnerReq{
				ContUUID: testContUUID,
			},
			expErr: errors.New("invalid UUID"),
		},
		"no user or group": {
			req: &ContSetOwnerReq{
				PoolUUID: testPoolUUID,
				ContUUID: testContUUID,
			},
			expErr: errors.New("no user or group specified"),
		},
		"both user and group success": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.ContSetOwnerResp{},
				),
			},
		},
		"user-only success": {
			req: &ContSetOwnerReq{
				PoolUUID: testPoolUUID,
				ContUUID: testContUUID,
				User:     "someuser@",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.ContSetOwnerResp{},
				),
			},
		},
		"group-only success": {
			req: &ContSetOwnerReq{
				PoolUUID: testPoolUUID,
				ContUUID: testContUUID,
				Group:    "somegroup@",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.ContSetOwnerResp{},
				),
			},
		},
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

			gotErr := ContSetOwner(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
