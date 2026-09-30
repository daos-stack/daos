//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package mgmt

import (
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/testing/protocmp"
)

func TestPoolProperty_SetValueBytes(t *testing.T) {
	p := &PoolProperty{Number: 1}
	data := []byte("test-cert-data")

	p.SetValueBytes(data)

	got := p.GetByteval()
	if diff := cmp.Diff(data, got); diff != "" {
		t.Fatalf("unexpected byteval (-want, +got):\n%s\n", diff)
	}
}

func TestPoolProperty_UnmarshalJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		input string
		exp   *PoolProperty
	}{
		"string value": {
			input: `{"number": 1, "value": {"strval": "hello"}}`,
			exp: &PoolProperty{
				Number: 1,
				Value:  &PoolProperty_Strval{Strval: "hello"},
			},
		},
		"number value": {
			input: `{"number": 2, "value": {"numval": 42}}`,
			exp: &PoolProperty{
				Number: 2,
				Value:  &PoolProperty_Numval{Numval: 42},
			},
		},
		"byte value": {
			// "dGVzdC1jZXJ0LWRhdGE=" is base64 for "test-cert-data"
			input: `{"number": 3, "value": {"byteval": "dGVzdC1jZXJ0LWRhdGE="}}`,
			exp: &PoolProperty{
				Number: 3,
				Value:  &PoolProperty_Byteval{Byteval: []byte("test-cert-data")},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			got := &PoolProperty{}
			if err := json.Unmarshal([]byte(tc.input), got); err != nil {
				t.Fatalf("unexpected error: %s", err)
			}
			if diff := cmp.Diff(tc.exp, got, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
