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
