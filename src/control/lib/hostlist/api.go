//
// (C) Copyright 2019-2020 Intel Corporation.
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

package hostlist

// This file contains package-level convenience functions for working with
// hostlist strings.

// Expand converts a ranged host string into an expanded string
// of all hosts in the supplied range(s).
func Expand(stringHosts string) (string, error) {
	hs, err := CreateSet(stringHosts)
	if err != nil {
		return "", err
	}

	return hs.DerangedString(), nil
}

// Compress converts the supplied host list into a string
// of ranged host strings.
func Compress(stringHosts string) (string, error) {
	hs, err := CreateSet(stringHosts)
	if err != nil {
		return "", err
	}

	return hs.RangedString(), nil
}

// Count returns the number of distinct hosts in the supplied host list.
func Count(stringHosts string) (int, error) {
	hs, err := CreateSet(stringHosts)
	if err != nil {
		return -1, err
	}

	return hs.Count(), nil
}
