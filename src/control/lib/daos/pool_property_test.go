//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos_test

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestControl_PoolPropertyValue(t *testing.T) {
	strPtr := func(in string) *string {
		return &in
	}
	numPtr := func(in uint64) *uint64 {
		return &in
	}

	for name, tc := range map[string]struct {
		val    *daos.PoolPropertyValue
		strVal *string
		numVal *uint64
		expErr error
		expStr string
	}{
		"nil": {
			expErr: errors.New("not set"),
			expStr: "value not set",
		},
		"not set": {
			val:    &daos.PoolPropertyValue{},
			expErr: errors.New("not set"),
			expStr: "value not set",
		},
		"string value": {
			val:    &daos.PoolPropertyValue{},
			strVal: strPtr("hi"),
			expErr: errors.New("not uint64"),
			expStr: "hi",
		},
		"number value": {
			val:    &daos.PoolPropertyValue{},
			numVal: numPtr(42),
			expStr: "42",
		},
	} {
		t.Run(name, func(t *testing.T) {
			v := tc.val

			if tc.strVal != nil {
				v.SetString(*tc.strVal)
			} else if tc.numVal != nil {
				v.SetNumber(*tc.numVal)
			}

			gotStr := v.String()
			if diff := cmp.Diff(tc.expStr, gotStr); diff != "" {
				t.Fatalf("unexpected String() result (-want, +got):\n%s\n", diff)
			}

			gotNum, gotErr := v.GetNumber()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			test.AssertEqual(t, *tc.numVal, gotNum, "unexpected GetNumber() result")
		})
	}
}

func TestControl_PoolProperties(t *testing.T) {
	for name, tc := range map[string]struct {
		name    string
		value   string
		expErr  error
		expStr  string
		expJson []byte
	}{
		"label-valid": {
			name:    "label",
			value:   "valid",
			expStr:  "label:valid",
			expJson: []byte(`{"name":"label","description":"Pool label","value":"valid"}`),
		},
		"label-invalid": {
			name:   "label",
			value:  "bad label",
			expErr: errors.New("invalid label"),
		},
		"reclaim-disabled": {
			name:    "reclaim",
			value:   "disabled",
			expStr:  "reclaim:disabled",
			expJson: []byte(`{"name":"reclaim","description":"Reclaim strategy","value":"disabled"}`),
		},
		"reclaim-time": {
			name:    "reclaim",
			value:   "time",
			expStr:  "reclaim:time",
			expJson: []byte(`{"name":"reclaim","description":"Reclaim strategy","value":"time"}`),
		},
		"reclaim-lazy": {
			name:    "reclaim",
			value:   "lazy",
			expStr:  "reclaim:lazy",
			expJson: []byte(`{"name":"reclaim","description":"Reclaim strategy","value":"lazy"}`),
		},
		"reclaim-invalid": {
			name:   "reclaim",
			value:  "wat",
			expErr: errors.New("invalid"),
		},
		"ec_cell_sz-valid": {
			name:    "ec_cell_sz",
			value:   "1MiB",
			expStr:  "ec_cell_sz:1.0 MiB",
			expJson: []byte(`{"name":"ec_cell_sz","description":"EC cell size","value":1048576}`),
		},
		"ec_cell_sz-invalid": {
			name:   "ec_cell_sz",
			value:  "wat",
			expErr: errors.New("invalid"),
		},
		"rd_fac-valid": {
			name:    "rd_fac",
			value:   "1",
			expStr:  "rd_fac:1",
			expJson: []byte(`{"name":"rd_fac","description":"Pool redundancy factor","value":1}`),
		},
		"rd_fac-invalid": {
			name:   "rd_fac",
			value:  "100",
			expErr: errors.New("invalid"),
		},
		"space_rb-valid": {
			name:    "space_rb",
			value:   "25",
			expStr:  "space_rb:25%",
			expJson: []byte(`{"name":"space_rb","description":"Rebuild space ratio","value":25}`),
		},
		"space_rb-invalid": {
			name:   "space_rb",
			value:  "wat",
			expErr: errors.New("invalid"),
		},
		"space_rb-gt100": {
			name:   "space_rb",
			value:  "101",
			expErr: errors.New("invalid"),
		},
		"self_heal-exclude": {
			name:    "self_heal",
			value:   "exclude",
			expStr:  "self_heal:exclude",
			expJson: []byte(`{"name":"self_heal","description":"Self-healing policy","value":"exclude"}`),
		},
		"self_heal-rebuild": {
			name:    "self_heal",
			value:   "rebuild",
			expStr:  "self_heal:rebuild",
			expJson: []byte(`{"name":"self_heal","description":"Self-healing policy","value":"rebuild"}`),
		},
		"self_heal-exclude,rebuild": {
			name:    "self_heal",
			value:   "exclude,rebuild",
			expStr:  "self_heal:exclude,rebuild",
			expJson: []byte(`{"name":"self_heal","description":"Self-healing policy","value":"exclude,rebuild"}`),
		},
		"self_heal-rebuild,exclude": {
			name:    "self_heal",
			value:   "rebuild,exclude",
			expStr:  "self_heal:exclude,rebuild",
			expJson: []byte(`{"name":"self_heal","description":"Self-healing policy","value":"exclude,rebuild"}`),
		},
		"self_heal-invalid": {
			name:   "self_heal",
			value:  "wat",
			expErr: errors.New("invalid"),
		},
		"ec_pda-valid": {
			name:    "ec_pda",
			value:   "1",
			expStr:  "ec_pda:1",
			expJson: []byte(`{"name":"ec_pda","description":"Performance domain affinity level of EC","value":1}`),
		},
		"ec_pda-invalid": {
			name:   "ec_pda",
			value:  "-1",
			expErr: errors.New("invalid"),
		},
		"rp_pda-valid": {
			name:    "rp_pda",
			value:   "2",
			expStr:  "rp_pda:2",
			expJson: []byte(`{"name":"rp_pda","description":"Performance domain affinity level of RP","value":2}`),
		},
		"rp_pda-invalid": {
			name:   "rp_pda",
			value:  "-1",
			expErr: errors.New("invalid"),
		},
		"policy-valid": {
			name:    "policy",
			value:   "type=io_size",
			expStr:  "policy:type=io_size",
			expJson: []byte(`{"name":"policy","description":"Tier placement policy","value":"type=io_size"}`),
		},
		"policy-invalid": {
			name:   "policy",
			value:  "deadd00d",
			expErr: errors.New("invalid"),
		},
		"perf_domain-valid": {
			name:    "perf_domain",
			value:   "group",
			expStr:  "perf_domain:group",
			expJson: []byte(`{"name":"perf_domain","description":"Pool performance domain","value":"group"}`),
		},
		"perf_domain-invalid": {
			name:   "perf_domain",
			value:  "bad domain",
			expErr: errors.New(`invalid value "bad domain" for perf_domain (valid: group,root)`),
		},
	} {
		t.Run(name, func(t *testing.T) {
			prop, err := daos.PoolProperties().GetProperty(tc.name)
			if err != nil {
				t.Fatal(err)
			}
			gotErr := prop.SetValue(tc.value)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expStr, prop.String()); diff != "" {
				t.Fatalf("unexpected String() value (-want, +got):\n%s\n", diff)
			}
			gotJson, err := prop.MarshalJSON()
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expJson, gotJson); diff != "" {
				t.Fatalf("unexpected MarshalJSON() value (-want, +got):\n%s\n", diff)
			}
		})
	}
}
