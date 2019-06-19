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

package common

import "unicode"

// Include returns true if string target in slice.
func Include(ss []string, target string) bool {
	return Index(ss, target) >= 0
}

// Index returns first index of target string,
// -1 if no match
func Index(ss []string, target string) int {
	for i, s := range ss {
		if s == target {
			return i
		}
	}
	return -1
}

// All returns true if all strings returns true from f.
func All(ss []string, f func(string) bool) bool {
	for _, s := range ss {
		if !f(s) {
			return false
		}
	}
	return true
}

// Any returns true if any of the strings in the slice returns true from f.
func Any(ss []string, f func(string) bool) bool {
	for _, s := range ss {
		if f(s) {
			return true
		}
	}
	return false
}

// Map returns new slice with f applied to each string in original.
func Map(ss []string, f func(string) string) (nss []string) {
	nss = make([]string, len(ss))
	for i, s := range ss {
		nss[i] = f(s)
	}
	return
}

// Filter returns new slice with only strings that return true from f.
func Filter(ss []string, f func(string) bool) (nss []string) {
	for _, s := range ss {
		if f(s) {
			nss = append(nss, s)
		}
	}
	return
}

// IsAlphabetic checks of a string just contains alphabetic characters.
func IsAlphabetic(s string) bool {
	for _, r := range s {
		if !unicode.IsLetter(r) {
			return false
		}
	}
	return true
}
