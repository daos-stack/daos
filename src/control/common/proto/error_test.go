//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	"github.com/daos-stack/daos/src/control/system"
)

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
	testNotReplica := &system.ErrNotReplica{
		Replicas: []string{"a", "b", "c"},
	}
	testNotLeader := &system.ErrNotLeader{
		LeaderHint: "foo.bar.baz",
		Replicas:   []string{"a", "b", "c"},
	}

	for name, tc := range map[string]struct {
		err    error
		expErr error
	}{
		"wrap/unwrap Fault": {
			err:    testFault,
			expErr: testFault,
		},
		"wrap/unwrap DaosStatus": {
			err:    testStatus,
			expErr: testStatus,
		},
		"wrap/unwrap ErrNotReplica": {
			err:    testNotReplica,
			expErr: testNotReplica,
		},
		"wrap/unwrap ErrNotLeader": {
			err:    testNotLeader,
			expErr: testNotLeader,
		},
		"non-fault err": {
			err:    errors.New("not a fault"),
			expErr: status.New(codes.Unknown, "not a fault").Err(),
		},
		"nil error": {}, // should just pass through
	} {
		t.Run(name, func(t *testing.T) {
			aErr := proto.AnnotateError(tc.err)

			gotErr := proto.UnwrapError(status.Convert(aErr))
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr == nil {
				return
			}
		})
	}
}
