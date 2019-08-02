//
// (C) Copyright 2018-2019 Intel Corporation.
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

package faults_test

import (
	"fmt"
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/faults"
)

func TestFaults(t *testing.T) {
	for _, tc := range []struct {
		name        string
		testErr     error
		expFaultStr string
		expFaultRes string
	}{
		{
			name:        "nil error",
			testErr:     nil,
			expFaultRes: "unknown: code = 0 resolution = \"no known resolution\"",
		},
		{
			name:        "normal error",
			testErr:     fmt.Errorf("not a fault"),
			expFaultStr: "not a fault",
			expFaultRes: "unknown: code = 0 resolution = \"no known resolution\"",
		},
		{
			name:        "empty fault",
			testErr:     &faults.Fault{},
			expFaultStr: faults.UnknownFault.Error(),
			expFaultRes: "unknown: code = 0 resolution = \"no known resolution\"",
		},
		{
			name: "fault without domain",
			testErr: &faults.Fault{
				Code:        123,
				Description: "the world is on fire",
				Resolution:  "go jump in the lake",
			},
			expFaultStr: "unknown: code = 123 description = \"the world is on fire\"",
			expFaultRes: "unknown: code = 123 resolution = \"go jump in the lake\"",
		},
		{
			name: "fault",
			testErr: &faults.Fault{
				Domain:      "test",
				Code:        123,
				Description: "the world is on fire",
				Resolution:  "go jump in the lake",
			},
			expFaultStr: "test: code = 123 description = \"the world is on fire\"",
			expFaultRes: "test: code = 123 resolution = \"go jump in the lake\"",
		},
		{
			name: "fault with funky domain",
			testErr: &faults.Fault{
				Domain:      "test why did:i put spaces?",
				Code:        123,
				Description: "the world is on fire",
				Resolution:  "go jump in the lake",
			},
			expFaultStr: "test_why_did_i_put_spaces?: code = 123 description = \"the world is on fire\"",
			expFaultRes: "test_why_did_i_put_spaces?: code = 123 resolution = \"go jump in the lake\"",
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if tc.testErr != nil {
				if tc.testErr.Error() != tc.expFaultStr {
					t.Fatalf("expected %q, got %q", tc.expFaultStr, tc.testErr)
				}
			}
			actual := faults.ShowResolutionFor(tc.testErr)
			if actual != tc.expFaultRes {
				t.Fatalf("expected %q, got %q", tc.expFaultRes, actual)
			}

			expHasRes := !strings.Contains(tc.expFaultRes, faults.ResolutionUnknown)
			actualHasRes := faults.HasResolution(tc.testErr)
			if actualHasRes != expHasRes {
				t.Fatalf("expected HasResolution() == %t, got %t", expHasRes, actualHasRes)
			}
		})
	}
}

func TestFaultComparison(t *testing.T) {
	testErr := &faults.Fault{
		Domain:      "test",
		Code:        1,
		Description: "test",
	}

	for _, tc := range []struct {
		name          string
		other         error
		expComparison bool
	}{
		{
			name:          "comparison with nil",
			other:         nil,
			expComparison: false,
		},
		{
			name:          "comparison with regular error",
			other:         fmt.Errorf("non-fault"),
			expComparison: false,
		},
		{
			name:          "comparison with self",
			other:         testErr,
			expComparison: true,
		},
		{
			name:          "comparison with other same code",
			other:         &faults.Fault{Code: testErr.Code},
			expComparison: true,
		},
		{
			name:          "comparison with other different code",
			other:         &faults.Fault{Code: testErr.Code + 1},
			expComparison: false,
		},
		{
			name:          "comparison with wrapped error",
			other:         errors.Wrap(&faults.Fault{Code: testErr.Code}, "foobar"),
			expComparison: true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			actual := testErr.Equals(tc.other)
			if actual != tc.expComparison {
				t.Fatalf("expected %t, but got %t", tc.expComparison, actual)
			}
		})
	}
}
