//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
