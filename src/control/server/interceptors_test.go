//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
)

type testStatus struct {
	Status int32
}

func (ts *testStatus) GetStatus() int32 {
	return ts.Status
}

func TestServer_unaryStatusInterceptor(t *testing.T) {
	for name, tc := range map[string]struct {
		handlerResp interface{}
		handlerErr  error
		expErr      error
	}{
		"handler error": {
			handlerErr: errors.New("whoops"),
			expErr:     errors.New("whoops"),
		},
		"DAOS status 0": {
			handlerResp: &testStatus{},
		},
		"DAOS status -1005": {
			handlerResp: &testStatus{
				Status: -1005,
			},
			expErr: drpc.DaosNonexistant,
		},
		"non-status resp": {
			handlerResp: 42,
		},
	} {
		t.Run(name, func(t *testing.T) {
			handler := func(ctx context.Context, req interface{}) (interface{}, error) {
				return tc.handlerResp, tc.handlerErr
			}
			gotResp, gotErr := unaryStatusInterceptor(context.TODO(), nil, nil, handler)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.handlerResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
