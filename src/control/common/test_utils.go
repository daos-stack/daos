//
// (C) Copyright 2018 Intel Corporation.
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
	"fmt"
	"reflect"
	"sort"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/log"
)

// AssertTrue asserts b is true
func AssertTrue(t *testing.T, b bool, message string) {
	t.Helper()

	if !b {
		t.Fatal(message)
	}
}

// AssertFalse asserts b is false
func AssertFalse(t *testing.T, b bool, message string) {
	t.Helper()

	if b {
		t.Fatal(message)
	}
}

// AssertEqual asserts b is equal to a
//
// Whilst suitable in most situations, reflect.DeepEqual() may not be
// suitable for nontrivial struct element comparisons, go-cmp should
// then be used but will introduce a third party dep.
func AssertEqual(
	t *testing.T, a interface{}, b interface{}, message string) {
	t.Helper()

	if reflect.DeepEqual(a, b) {
		return
	}
	if len(message) > 0 {
		message += ", "
	}

	t.Fatalf(message+"%#v != %#v", a, b)
}

// AssertStringsEqual sorts string slices before comparing.
func AssertStringsEqual(
	t *testing.T, a []string, b []string, message string) {
	t.Helper()

	sort.Strings(a)
	sort.Strings(b)

	AssertEqual(t, a, b, message)
}

// ExpectError asserts error contains expected message
func ExpectError(
	t *testing.T, actualErr error, expectedMessage string, desc interface{}) {
	t.Helper()

	if actualErr == nil {
		t.Fatalf("Expected a non-nil error: %v", desc)
	} else if actualErr.Error() != expectedMessage {
		t.Fatalf(
			"Wrong error message. Expected: %s, Actual: %s (%v)",
			expectedMessage, actualErr.Error(), desc)
	}
}

// LoadTestFiles reads inputs and outputs from file and do basic sanity checks.
// Both files contain entries of multiple lines separated by blank line.
// Return inputs and outputs, both of which are slices of string slices.
func LoadTestFiles(inFile string, outFile string) (
	inputs [][]string, outputs [][]string, err error) {

	inputs, err = SplitFile(inFile)
	if err != nil {
		return
	}
	outputs, err = SplitFile(outFile)
	if err != nil {
		return
	}

	if len(inputs) < 1 {
		err = fmt.Errorf("no inputs read from file")
	} else if len(inputs) != len(outputs) {
		err = fmt.Errorf("number of inputs and outputs not equal")
	}

	return
}

// ShowLogOnFailure captures the log output in a buffer and displays it on test
// failure. Returns a closure which should be run via defer in the test function.
func ShowLogOnFailure(t *testing.T) func() {
	t.Helper()

	var buf strings.Builder
	log.NewDefaultLogger(log.Error, t.Name(), &buf)

	return func() {
		if t.Failed() {
			fmt.Printf("captured log output:\n%s", buf.String())
		}
	}
}
