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
package proto_test

import (
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

func TestProto_FaultFromMeta(t *testing.T) {
	for name, tc := range map[string]struct {
		meta     map[string]string
		expFault *fault.Fault
		expErr   error
	}{
		"success": {
			meta: map[string]string{
				"Domain":      "Domain",
				"Code":        "42",
				"Description": "Description",
				"Resolution":  "Resolution",
			},
			expFault: &fault.Fault{
				Domain:      "Domain",
				Code:        code.Code(42),
				Description: "Description",
				Resolution:  "Resolution",
			},
		},
		"empty meta": {
			expFault: &fault.Fault{},
		},
		"weird code": {
			meta: map[string]string{
				"Code": "bananas",
			},
			expErr: errors.New("strconv.Atoi"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotFault, gotErr := proto.FaultFromMeta(tc.meta)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expFault, gotFault); diff != "" {
				t.Fatalf("unexpected Fault (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProto_MetaFromFault(t *testing.T) {
	for name, tc := range map[string]struct {
		fault   *fault.Fault
		expMeta map[string]string
	}{
		"success": {
			fault: &fault.Fault{
				Domain:      "Domain",
				Code:        code.Code(42),
				Description: "Description",
				Resolution:  "Resolution",
			},
			expMeta: map[string]string{
				"Domain":      "Domain",
				"Code":        "42",
				"Description": "Description",
				"Reason":      "",
				"Resolution":  "Resolution",
			},
		},
		"empty fault": {
			fault: &fault.Fault{},
			expMeta: map[string]string{
				"Domain":      "",
				"Code":        "0",
				"Description": "",
				"Reason":      "",
				"Resolution":  "",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotMeta := proto.MetaFromFault(tc.fault)
			if diff := cmp.Diff(tc.expMeta, gotMeta); diff != "" {
				t.Fatalf("unexpected meta (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProto_AnnotateError(t *testing.T) {
	testFault := &fault.Fault{
		Domain:      "Domain",
		Code:        code.Code(42),
		Description: "Description",
		Resolution:  "Resolution",
	}
	testStatus := drpc.DaosInvalidInput

	for name, tc := range map[string]struct {
		err    error
		expErr error
	}{
		"wrap/unwrap Fault": {
			err: testFault,
		},
		"wrap/unwrap DaoStatus": {
			err: testStatus,
		},
		"non-fault err": {
			err:    errors.New("not a fault"),
			expErr: status.New(codes.Unknown, "not a fault").Err(),
		},
		"nil error": {}, // should just pass through
	} {
		t.Run(name, func(t *testing.T) {
			aErr := proto.AnnotateError(tc.err)

			var gotErr error
			switch tc.err.(type) {
			case *fault.Fault:
				var gotFault *fault.Fault
				gotFault, gotErr = proto.UnwrapFault(status.Convert(aErr))
				common.CmpErr(t, tc.expErr, gotErr)
				if diff := cmp.Diff(testFault, gotFault); diff != "" {
					t.Fatalf("unexpected fault: (-want, +got):\n%s\n", diff)
				}
			case drpc.DaosStatus:
				var gotStatus drpc.DaosStatus
				gotStatus, gotErr = proto.UnwrapDaosStatus(status.Convert(aErr))
				common.CmpErr(t, tc.expErr, gotErr)
				if gotStatus != testStatus {
					t.Fatalf("unexpected status: %d != %d", testStatus, gotStatus)
				}
			default:
				_, gotErr = proto.UnwrapFault(status.Convert(aErr))
				common.CmpErr(t, tc.expErr, gotErr)
				_, gotErr = proto.UnwrapDaosStatus(status.Convert(aErr))
				common.CmpErr(t, tc.expErr, gotErr)
			}
		})
	}
}
