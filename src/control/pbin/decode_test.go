//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
