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

func TestPoolProperty_FindPoolProperty(t *testing.T) {
	label := &PoolProperty{Number: 1, Value: &PoolProperty_Strval{Strval: "test"}}
	rdFac := &PoolProperty{Number: 2, Value: &PoolProperty_Numval{Numval: 1}}
	rdFacDup := &PoolProperty{Number: 2, Value: &PoolProperty_Numval{Numval: 2}}

	for name, tc := range map[string]struct {
		props    []*PoolProperty
		number   uint32
		expProp  *PoolProperty
		expFound bool
	}{
		"nil slice": {
			number: 1,
		},
		"empty slice": {
			props:  []*PoolProperty{},
			number: 1,
		},
		"not found": {
			props:  []*PoolProperty{label},
			number: 2,
		},
		"found": {
			props:    []*PoolProperty{label, rdFac},
			number:   2,
			expProp:  rdFac,
			expFound: true,
		},
		"nil entry skipped": {
			props:    []*PoolProperty{nil, rdFac},
			number:   2,
			expProp:  rdFac,
			expFound: true,
		},
		"first match wins": {
			props:    []*PoolProperty{rdFac, rdFacDup},
			number:   2,
			expProp:  rdFac,
			expFound: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotProp, gotFound := FindPoolProperty(tc.props, tc.number)

			if gotFound != tc.expFound {
				t.Fatalf("expected found=%t, got %t", tc.expFound, gotFound)
			}
			if diff := cmp.Diff(tc.expProp, gotProp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
