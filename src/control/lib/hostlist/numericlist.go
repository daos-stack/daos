//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hostlist

import (
	"unicode"

	"github.com/pkg/errors"
)

// CreateNumericList creates a special HostList that contains numeric entries
// and ranges only (no hostname prefixes) from the supplied string representation.
func CreateNumericList(stringRanges string) (*HostList, error) {
	for _, r := range stringRanges {
		if unicode.IsSpace(r) {
			return nil, errors.New("unexpected whitespace character(s)")
		}
		if unicode.IsLetter(r) {
			return nil, errors.New("unexpected alphabetic character(s)")
		}
	}

	return parseBracketedHostList(stringRanges, outerRangeSeparators,
		rangeOperator, true)
}

// CreateNumericSet creates a special HostSet containing numeric entries
// and ranges only (no hostname prefixes) from the supplied string representation.
func CreateNumericSet(stringRanges string) (*HostSet, error) {
	hl, err := CreateNumericList(stringRanges)
	if err != nil {
		return nil, errors.Wrapf(err,
			"creating numeric set from %q", stringRanges)
	}
	hl.Uniq()

	return &HostSet{list: hl}, nil
}
