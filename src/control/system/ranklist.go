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

package system

import (
	"fmt"
	"strings"
	"unicode"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/pkg/errors"
)

// RankSet embodies HostSet type
type RankSet struct {
	hostlist.HostSet
}

func fixBrackets(stringRanks string, remove bool) string {
	hasPrefix := strings.HasPrefix(stringRanks, "[")
	hasSuffix := strings.HasSuffix(stringRanks, "]")
	if remove {
		if hasPrefix {
			stringRanks = stringRanks[1:]
		}
		if hasSuffix {
			stringRanks = stringRanks[:len(stringRanks)-1]
		}

		return stringRanks
	}

	if !hasPrefix {
		stringRanks = "[" + stringRanks
	}
	if !hasSuffix {
		stringRanks += "]"
	}

	return stringRanks
}

// CreateSet creates a new HostList with ranks rather than hostnames from the
// supplied string representation.
func CreateSet(stringRanks string) (*RankSet, error) {
	for _, r := range stringRanks {
		if unicode.IsLetter(r) {
			return nil, errors.Errorf(
				"expecting no alphabetic characters, got '%s'",
				stringRanks)
		}
	}

	stringRanks = fixBrackets(stringRanks, false)

	fmt.Printf(" r '%s'", stringRanks)
	hs, err := hostlist.CreateSet(stringRanks, true)
	if err != nil {
		return nil, err
	}
	// copying locks ok because original hs is discarded
	rs := RankSet{HostSet: *hs}

	return &rs, nil
}

func (rs *RankSet) String() string {
	return fixBrackets(rs.HostSet.String(), true)
}
