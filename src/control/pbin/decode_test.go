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
package pbin

import (
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestPbin_decodeResponse(t *testing.T) {
	for name, tc := range map[string]struct {
		input  []byte
		expRes *Response
		expErr error
	}{
		"normal input": {
			input: []byte(`{"Error":null,"Payload":{"Controllers":[]}}`),
			expRes: &Response{
				Payload: json.RawMessage(`{"Error":null,"Payload":{"Controllers":[]}}`),
			},
		},
		"deal with SPDK spew": {
			input: []byte(`EAL:   Invalid NUMA socket, default to 0\nEAL:   Invalid NUMA socket` +
				`, default to 0\n{"Error":null,"Payload":{"Controllers":[]}}`),
			expRes: &Response{
				Payload: json.RawMessage(`{"Error":null,"Payload":{"Controllers":[]}}`),
			},
		},
		"some weird suffix": {
			input: []byte(`EAL:   Invalid NUMA socket, default to 0\nEAL:   Invalid NUMA socket` +
				`, default to 0\n{"Error":null,"Payload":{"Controllers":[]}}abbabananafish`),
			expRes: &Response{
				Payload: json.RawMessage(`{"Error":null,"Payload":{"Controllers":[]}}`),
			},
		},
		"unrecoverable nonsense": {
			input: []byte(`{"Error":null,"Payload":{"Controllers":[Hwæt. We Gardena in geardagum,` +
				`þeodcyninga, þrym gefrunon, hu ða æþelingas ellen fremedon.]}}`),
			expErr: errors.New("invalid character"),
		},
		"more nonsense": {
			input: []byte(`{"Error":null,"Payload":{"Controllers":[Hwæt. We Gardena in geardagum,` +
				`þeodcyninga, þrym gefrunon, hu ða æþelingas ellen fremedon.]`),
			expErr: errors.New("invalid character"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotRes, gotErr := decodeResponse(tc.input)

			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr == nil {
				return
			}

			if diff := cmp.Diff(tc.expRes, gotRes); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
