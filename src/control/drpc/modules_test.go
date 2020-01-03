//
// (C) Copyright 2019 Intel Corporation.
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
