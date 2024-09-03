//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestControl_ContSetOwner(t *testing.T) {
	testPoolUUID := uuid.New().String()
	testContUUID := uuid.New().String()

	validReq := &ContSetOwnerReq{
		PoolID: testPoolUUID,
		ContID: testContUUID,
		User:   "someuser@",
		Group:  "somegroup@",
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
		"no container ID": {
			req: &ContSetOwnerReq{
				PoolID: testPoolUUID,
			},
			expErr: errors.New("container label or UUID"),
		},
		"no pool ID": {
			req: &ContSetOwnerReq{
				ContID: testContUUID,
			},
			expErr: errors.New("pool label or UUID"),
		},
		"no user or group": {
			req: &ContSetOwnerReq{
				PoolID: testPoolUUID,
				ContID: testContUUID,
			},
			expErr: errors.New("no user or group specified"),
		},
		"both user and group success": {
			req: validReq,
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.DaosResp{},
				),
			},
		},
		"user-only success": {
			req: &ContSetOwnerReq{
				PoolID: testPoolUUID,
				ContID: testContUUID,
				User:   "someuser@",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.DaosResp{},
				),
			},
		},
		"group-only success": {
			req: &ContSetOwnerReq{
				PoolID: testPoolUUID,
				ContID: testContUUID,
				Group:  "somegroup@",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.DaosResp{},
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
		"labels OK": {
			req: &ContSetOwnerReq{
				PoolID: "pool1",
				ContID: "cont1",
				User:   "someuser@",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.DaosResp{},
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

			gotErr := ContSetOwner(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
