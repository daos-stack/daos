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

package helper

import (
	"encoding/json"
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
)

func TestHelper_NewResponseWithError(t *testing.T) {
	expErr := errors.New("test error")

	resp := NewResponseWithError(expErr)

	common.CmpErr(t, expErr, resp.Error)

	if diff := cmp.Diff(json.RawMessage("null"), resp.Payload); diff != "" {
		t.Errorf("unexpected payload (-want, +got)\n%s\n", diff)
	}
}

func TestHelper_NewResponseWithPayload(t *testing.T) {
	payload := testPayload{result: "here's the real result"}
	expPayloadBytes, err := json.Marshal(payload)
	if err != nil {
		t.Fatalf("couldn't marshal payload: %v", err)
	}
	expPayload := json.RawMessage(expPayloadBytes)

	resp := NewResponseWithPayload(payload)

	if resp.Error != nil {
		t.Errorf("unexpected error (wanted nil): %v", resp.Error)
	}

	if diff := cmp.Diff(expPayload, resp.Payload); diff != "" {
		t.Errorf("unexpected payload (-want, +got)\n%s\n", diff)
	}
}
