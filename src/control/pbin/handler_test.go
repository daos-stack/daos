//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pbin

import (
	"encoding/json"
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
)

func TestPbin_NewResponseWithError(t *testing.T) {
	expErr := errors.New("test error")

	resp := NewResponseWithError(expErr)

	common.CmpErr(t, expErr, resp.Error)

	if diff := cmp.Diff(json.RawMessage("null"), resp.Payload); diff != "" {
		t.Errorf("unexpected payload (-want, +got)\n%s\n", diff)
	}
}

func TestPbin_NewResponseWithPayload(t *testing.T) {
	payload := testPayload{Result: "here's the real result"}
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

func TestPbin_PingHandler(t *testing.T) {
	appName := "test_app"
	for name, tc := range map[string]struct {
		req        *Request
		expPayload *PingResp
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: PrivilegedHelperRequestFailed("nil request"),
		},
		"success": {
			req: &Request{Method: PingMethod},
			expPayload: &PingResp{
				Version: build.DaosVersion,
				AppName: appName,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			handler := &pingHandler{
				appName: appName,
			}

			resp := handler.Handle(nil, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}

			if tc.expPayload == nil {
				tc.expPayload = &PingResp{}
			}
			payload := &PingResp{}
			err := json.Unmarshal(resp.Payload, payload)
			if err != nil {
				t.Fatalf("couldn't unmarshal response payload")
			}
			if diff := cmp.Diff(tc.expPayload, payload); diff != "" {
				t.Errorf("got wrong payload (-want, +got)\n%s\n", diff)
			}
		})
	}
}
