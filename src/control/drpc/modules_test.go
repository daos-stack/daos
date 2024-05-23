//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestMarshal_Success(t *testing.T) {
	message := &Call{Module: 1, Method: 2, Sequence: 3}

	result, err := Marshal(message)

	if err != nil {
		t.Errorf("Expected no error, got: %+v", err)
	}

	// Unmarshaled result should match original
	pMsg := &Call{}
	_ = proto.Unmarshal(result, pMsg)

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(message, pMsg, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}
