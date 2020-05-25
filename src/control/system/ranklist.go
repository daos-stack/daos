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
	"bytes"
	"fmt"
	"sort"
	"strings"
	"unicode"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/pkg/errors"
)

// RankSet embodies HostSet type.
type RankSet struct {
	hostlist.HostSet
}

func fixBrackets(stringRanks string, remove bool) string {
	if remove {
		return strings.Trim(stringRanks, "[]")
	}

	if !strings.HasPrefix(stringRanks, "[") {
		stringRanks = "[" + stringRanks
	}
	if !strings.HasSuffix(stringRanks, "]") {
		stringRanks += "]"
	}

	return stringRanks
}

// NewRankSet creates a new HostList with ranks rather than hostnames from the
// supplied string representation.
func NewRankSet(stringRanks string) (*RankSet, error) {
	for _, r := range stringRanks {
		if unicode.IsLetter(r) {
			return nil, errors.Errorf(
				"expecting no alphabetic characters, got '%s'",
				stringRanks)
		}
	}

	if len(stringRanks) > 0 {
		stringRanks = fixBrackets(stringRanks, false)
	}

	// add enclosing brackets to input so CreateSet works without hostnames
	hs, err := hostlist.CreateNumberSet(stringRanks)
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

// Ranks returns a slice of Rank from a RankSet.
func (rs *RankSet) Ranks() ([]Rank, error) {
	var ranks []uint32
	err := common.ParseNumberList(
		fixBrackets(rs.HostSet.DerangedString(), true),
		&ranks)
	if err != nil {
		return nil, err
	}

	return RanksFromUint32(ranks), nil
}

// RankGroups maps a set of ranks to string value (group).
type RankGroups map[string]*RankSet

// Keys returns sorted group names.
func (rsg RankGroups) Keys() []string {
	keys := make([]string, 0, len(rsg))

	for key := range rsg {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	return keys
}

func (rsg RankGroups) String() string {
	var buf bytes.Buffer

	padding := 0
	keys := rsg.Keys()
	for _, key := range keys {
		valStr := rsg[key].String()
		if len(valStr) > padding {
			padding = len(valStr)
		}
	}

	for _, key := range rsg.Keys() {
		fmt.Fprintf(&buf, "%*s: %s\n", padding, rsg[key], key)
	}

	return buf.String()
}
