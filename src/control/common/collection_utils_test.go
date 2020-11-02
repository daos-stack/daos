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

package common

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func TestCommon_ParseNumberListUint32(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOutput []uint32
		expErr    error
	}{
		"empty":              {"", []uint32{}, nil},
		"valid single":       {"0", []uint32{0}, nil},
		"valid multiple":     {"0,1,2,3", []uint32{0, 1, 2, 3}, nil},
		"invalid alphabetic": {"0,A,", nil, errors.New("unable to parse")},
		"invalid negative":   {"-1", nil, errors.New("invalid input")},
		"invalid float":      {"5.5", nil, errors.New("invalid input")},
		"overflows uint32":   {"4294967296", nil, errors.New("invalid input")},
	} {
		t.Run(name, func(t *testing.T) {
			var gotOutput []uint32
			gotErr := ParseNumberList(tc.input, &gotOutput)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expOutput, gotOutput); diff != "" {
				t.Fatalf("unexpected integer list (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCommon_ParseNumberListInt(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOutput []int
		expErr    error
	}{
		"empty":              {"", []int{}, nil},
		"valid single":       {"0", []int{0}, nil},
		"valid multiple":     {"0,1,2,3", []int{0, 1, 2, 3}, nil},
		"valid negative":     {"-1", []int{-1}, nil},
		"invalid alphabetic": {"0,A,", nil, errors.New("unable to parse")},
	} {
		t.Run(name, func(t *testing.T) {
			var gotOutput []int
			gotErr := ParseNumberList(tc.input, &gotOutput)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expOutput, gotOutput); diff != "" {
				t.Fatalf("unexpected integer list (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCommon_ParseNumberListFloat64(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOutput []float64
		expErr    error
	}{
		"empty":              {"", []float64{}, nil},
		"valid single":       {"0", []float64{0}, nil},
		"valid multiple":     {"0,1,2,3", []float64{0, 1, 2, 3}, nil},
		"valid float":        {"5.5", []float64{5.5}, nil},
		"valid negative":     {"-1", []float64{-1}, nil},
		"invalid alphabetic": {"0,A,", nil, errors.New("unable to parse")},
	} {
		t.Run(name, func(t *testing.T) {
			var gotOutput []float64
			gotErr := ParseNumberList(tc.input, &gotOutput)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expOutput, gotOutput); diff != "" {
				t.Fatalf("unexpected integer list (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCommon_ParseNumberListBadReceiver(t *testing.T) {
	gotErr := ParseNumberList("1,2,3", nil)
	CmpErr(t, errors.New("json: Unmarshal"), gotErr)
}

func TestCommon_StringSliceHasDuplicates(t *testing.T) {
	for name, tc := range map[string]struct {
		input     []string
		expResult bool
	}{
		"nil": {
			expResult: false,
		},
		"empty": {
			input:     []string{},
			expResult: false,
		},
		"single item": {
			input:     []string{"testing"},
			expResult: false,
		},
		"duplicates next to each other": {
			input:     []string{"testing", "testing", "one", "two", "three"},
			expResult: true,
		},
		"duplicates away from each other": {
			input:     []string{"one", "two", "testing", "one", "two"},
			expResult: true,
		},
		"no duplicates in list": {
			input:     []string{"testing", "test", "one", "two", "three", "four"},
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := StringSliceHasDuplicates(tc.input)

			if result != tc.expResult {
				t.Fatalf("expected %v, got %v", tc.expResult, result)
			}
		})
	}
}

func TestCommon_FilterStringMatches(t *testing.T) {
	for name, tc := range map[string]struct {
		filterStr string
		actualStr string
		expResult bool
	}{
		"no filter": {
			actualStr: "a string that shouldn't be filtered",
			expResult: true,
		},
		"exact match": {
			filterStr: "What fools these mortals be!",
			actualStr: "What fools these mortals be!",
			expResult: true,
		},
		"no match": {
			filterStr: "another fine mess",
			actualStr: "What fools these mortals be!",
			expResult: false,
		},
		"partial match doesn't count": {
			filterStr: "What",
			actualStr: "What fools these mortals be!",
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := FilterStringMatches(tc.filterStr, tc.actualStr)
			if result != tc.expResult {
				t.Fatalf("expected %v, got %v", tc.expResult, result)
			}
		})
	}
}

func TestCommon_PercentageString(t *testing.T) {
	for name, tc := range map[string]struct {
		partial   uint64
		total     uint64
		expResult string
	}{
		"zero values": {
			partial:   0,
			total:     0,
			expResult: "N/A",
		},
		"zero total": {
			partial:   1,
			total:     0,
			expResult: "N/A",
		},
		"zero partial": {
			partial:   0,
			total:     1,
			expResult: "0 %",
		},
		"full partial": {
			partial:   1000,
			total:     1000,
			expResult: "100 %",
		},
		"excess partial": {
			partial:   2000,
			total:     1000,
			expResult: "200 %",
		},
		"quarter partial": {
			partial:   250,
			total:     1000,
			expResult: "25 %",
		},
		"half partial": {
			partial:   500,
			total:     1000,
			expResult: "50 %",
		},
		"3 quarters partial": {
			partial:   750,
			total:     1000,
			expResult: "75 %",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := PercentageString(tc.partial, tc.total)
			if result != tc.expResult {
				t.Fatalf("expected %v, got %v", tc.expResult, result)
			}
		})
	}
}
