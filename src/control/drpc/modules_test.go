//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
)

func TestMarshal_Failed(t *testing.T) {
	result, err := Marshal(nil)

	common.CmpErr(t, MarshalingFailure(), err)

	if result != nil {
		t.Errorf("Expected no marshaled result, got: %+v", result)
	}
}

func TestMarshal_Success(t *testing.T) {
	message := &Call{Module: 1, Method: 2, Sequence: 3}

	result, err := Marshal(message)

	if err != nil {
		t.Errorf("Expected no error, got: %+v", err)
	}

	// Unmarshaled result should match original
	pMsg := &Call{}
	_ = proto.Unmarshal(result, pMsg)

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(message, pMsg, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}
