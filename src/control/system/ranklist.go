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

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

type RankSet struct {
	set hostlist.HostSet
}

// Create creates a new HostList with ranks Rather than hostnames from the
// supplied string representation.
func Create(stringRanks string) (*hostlist.HostList, error) {
	return parseBracketedHostList(stringNumbers, outerRangeSeparators, rangeOperator, true)
}

// addRankPrefix is a hack, but don't want to modify the hostlist library to
// accept invalid hostnames.
func addRankPrefix(rank Rank) string {
	return fmt.Sprintf("r-%d", rank)
}

// removeRankPrefixes is a hack, but don't want to modify the hostlist library to
// accept invalid hostnames.
func removeRankPrefixes(in string) string {
	return strings.Replace(in, "r-", "", -1)
}

//func (rs *RankSet) MarshalJSON() ([]byte, error) {
//	return []byte(`"` + rs.RangedString() + `"`), nil
//}
//
//// CreateSet creates a new RankSet from the supplied string representation.
//func CreateSet(stringRanks string) (*RankSet, error) {
//	hl, err := Create(stringRanks)
//	if err != nil {
//		return nil, err
//	}
//	hl.Uniq()
//
//	return &RankSet{list: hl}, nil
//}
//
//// initList will initialize the underlying *HostList if necessary
//func (rs *HostSet) initList() {
//	rs.Lock()
//	defer rs.Unlock()
//
//	if rs.list == nil {
//		rs.list, _ = Create("")
//	}
//}
//
//func (rs *RankSet) String() string {
//	return rs.RangedString()
//}
//
//// RangedString returns a string containing a bracketed RankSet representation.
//func (rs *RankSet) RangedString() string {
//	return removeRankPrefixes(rs.set.RangedString())
//}
//
//// DerangedString returns a string containing the rank of
//// every rank in the RankSet, without any bracketing.
//func (rs *RankSet) DerangedString() string {
//	return removeRankPrefixes(rs.set.DerangedString())
//}
//
//// Insert adds a rank or list of ranks to the RankSet.
//// Returns the number of non-duplicate ranks successfully added.
//func (rs *RankSet) Insert(stringRanks string) (int, error) {
//	rs.initList()
//
//	newList, err := Create(stringRanks)
//	if err != nil {
//		return -1, err
//	}
//
//	rs.Lock()
//	defer rs.Unlock()
//
//	startCount := rs.set.hostCount
//	if err := rs.set.PushList(newList); err != nil {
//		return -1, err
//	}
//	rs.list.Uniq()
//
//	return int(rs.set.hostCount - startCount), nil
//}
//
//// RankGroups maps a set of ranks to a string key value.
//type RankGroups map[string]*RankSet
//
//func (rg RankGroups) Keys() []string {
//	keys := make([]string, 0, len(rg))
//
//	for key := range rg {
//		keys = append(keys, key)
//	}
//
//	sort.Strings(keys)
//	return keys
//}
//
//func (rg RankGroups) AddRank(key, rank string) error {
//	if _, exists := rg[key]; !exists {
//		rg[key] = new(RankSet)
//	}
//
//	_, err := rg[key].Insert(rank)
//	return err
//}
//
//func (rg RankGroups) String() string {
//	var buf bytes.Buffer
//
//	padding := 0
//	keys := rg.Keys()
//	for _, key := range keys {
//		valStr := rg[key].String()
//		if len(valStr) > padding {
//			padding = len(valStr)
//		}
//	}
//
//	for _, key := range rg.Keys() {
//		fmt.Fprintf(&buf, "%*s: %s\n", padding, rg[key], key)
//	}
//
//	return buf.String()
//}
