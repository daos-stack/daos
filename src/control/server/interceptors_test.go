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
