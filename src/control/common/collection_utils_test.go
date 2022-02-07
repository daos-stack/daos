//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func TestCommon_NewStringSet(t *testing.T) {
	for name, tc := range map[string]struct {
		in         []string
		expStrings []string
	}{
		"no input": {},
		"one string": {
			in:         []string{"one"},
			expStrings: []string{"one"},
		},
		"multiple strings": {
			in:         []string{"one", "two", "three"},
			expStrings: []string{"one", "two", "three"},
		},
		"deduplicated": {
			in:         []string{"one", "two", "three", "one"},
			expStrings: []string{"one", "two", "three"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := NewStringSet(tc.in...)

			for _, exp := range tc.expStrings {
				if _, found := result[exp]; !found {
					t.Fatalf("string %q not found", exp)
				}
			}
			AssertEqual(t, len(tc.expStrings), len(result), "wrong number of strings")
		})
	}
}

func TestCommon_StringSet_AddUnique(t *testing.T) {
	for name, tc := range map[string]struct {
		set    StringSet
		in     []string
		expStr string
		expErr error
	}{
		"one dupe": {
			set:    NewStringSet("one"),
			in:     []string{"one"},
			expErr: errors.New("duplicate strings: one"),
		},
		"two dupes": {
			set:    NewStringSet("two", "one"),
			in:     []string{"one", "two"},
			expErr: errors.New("duplicate strings: one, two"),
		},
		"no dupes": {
			set:    NewStringSet("two"),
			in:     []string{"one"},
			expStr: "one, two",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := tc.set.AddUnique(tc.in...)

			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expStr, tc.set.String(), "wrong string")
		})
	}
}

func TestCommon_StringSet_Add(t *testing.T) {
	for name, tc := range map[string]struct {
		set        StringSet
		in         []string
		expStrings []string
	}{
		"nil": {
			in: []string{"one"},
		},
		"empty, no input": {
			set: NewStringSet(),
		},
		"empty, add one": {
			set:        NewStringSet(),
			in:         []string{"one"},
			expStrings: []string{"one"},
		},
		"empty, add multiple": {
			set:        NewStringSet(),
			in:         []string{"one", "two", "three"},
			expStrings: []string{"one", "two", "three"},
		},
		"no input": {
			set:        NewStringSet("one"),
			expStrings: []string{"one"},
		},
		"add one": {
			set:        NewStringSet("one"),
			in:         []string{"two"},
			expStrings: []string{"one", "two"},
		},
		"add multiple": {
			set:        NewStringSet("two"),
			in:         []string{"one", "three"},
			expStrings: []string{"one", "two", "three"},
		},
		"deduplicated": {
			set:        NewStringSet("one", "two", "three"),
			in:         []string{"one", "two", "four"},
			expStrings: []string{"one", "two", "three", "four"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.set.Add(tc.in...)

			for _, exp := range tc.expStrings {
				if _, found := tc.set[exp]; !found {
					t.Fatalf("string %q not found", exp)
				}
			}
			AssertEqual(t, len(tc.expStrings), len(tc.set), "wrong number of strings")
		})
	}
}

func TestCommon_StringSet_Has(t *testing.T) {
	for name, tc := range map[string]struct {
		set       StringSet
		input     string
		expResult bool
	}{
		"nil": {
			input: "something",
		},
		"empty": {
			set:   NewStringSet(),
			input: "something",
		},
		"has value": {
			set:       NewStringSet("one", "two", "three"),
			input:     "two",
			expResult: true,
		},
		"doesn't have value": {
			set:   NewStringSet("one", "two", "three"),
			input: "something",
		},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.expResult, tc.set.Has(tc.input), "")
		})
	}
}

func TestCommon_StringSet_ToSlice(t *testing.T) {
	for name, tc := range map[string]struct {
		set       StringSet
		expResult []string
	}{
		"nil": {
			expResult: []string{},
		},
		"empty": {
			set:       NewStringSet(),
			expResult: []string{},
		},
		"one item": {
			set:       NewStringSet("something"),
			expResult: []string{"something"},
		},
		"multiple": {
			set:       NewStringSet("something", "nothing", "alpha", "beta"),
			expResult: []string{"alpha", "beta", "nothing", "something"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.set.ToSlice()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestCommon_StringSet_String(t *testing.T) {
	for name, tc := range map[string]struct {
		set       StringSet
		expResult string
	}{
		"nil": {},
		"empty": {
			set:       NewStringSet(),
			expResult: "",
		},
		"one item": {
			set:       NewStringSet("something"),
			expResult: "something",
		},
		"multiple": {
			set:       NewStringSet("something", "nothing", "alpha", "beta"),
			expResult: "alpha, beta, nothing, something",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.set.String()

			AssertEqual(t, tc.expResult, result, "")
		})
	}
}

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

func TestCommon_TokenizeCommaSeparatedString(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expResult []string
	}{
		"empty": {
			expResult: []string{},
		},
		"well-formed": {
			input:     "one,two,three",
			expResult: []string{"one", "two", "three"},
		},
		"trim spaces": {
			input:     "one, two, three\t\n",
			expResult: []string{"one", "two", "three"},
		},
		"just commas": {
			input:     ",,",
			expResult: []string{},
		},
		"remove empty strings": {
			input:     ",one, ,,two,three,",
			expResult: []string{"one", "two", "three"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := TokenizeCommaSeparatedString(tc.input)

			AssertEqual(t, tc.expResult, result, "")
		})
	}
}
