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

package hostlist

import (
	"unicode"

	"github.com/pkg/errors"
)

// CreateNumericList creates a special HostList that contains numeric entries
// and ranges only (no hostname prefixes) from the supplied string representation.
func CreateNumericList(stringRanges string) (*HostList, error) {
	for _, r := range stringRanges {
		if unicode.IsLetter(r) {
			return nil, errors.New("unexpected alphabetic character(s)")
		}
		if unicode.IsSpace(r) {
			return nil, errors.New("unexpected whitespace character(s)")
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
