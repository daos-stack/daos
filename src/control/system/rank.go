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

package system

import (
	"math"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/pkg/errors"
)

type (
	// Rank is used to uniquely identify a server within a cluster.
	Rank uint32

	// RankList provides convenience methods for working with Rank slices.
	RankList []Rank
)

const (
	// MaxRank is the largest valid Rank value.
	MaxRank Rank = math.MaxUint32 - 1
	// NilRank is an undefined Rank (0 is a valid Rank).
	NilRank Rank = math.MaxUint32
)

// NewRankPtr creates a Rank representation of
// the given uint32 and returns a pointer to it.
func NewRankPtr(in uint32) *Rank {
	r := Rank(in)
	return &r
}

func (r *Rank) String() string {
	switch {
	case r == nil:
		return "NilRank"
	case r.Equals(NilRank):
		return "NilRank"
	default:
		return strconv.FormatUint(uint64(*r), 10)
	}
}

// Uint32 returns a uint32 representation of the Rank.
func (r *Rank) Uint32() uint32 {
	if r == nil {
		return uint32(NilRank)
	}
	return uint32(*r)
}

// UnmarshalYAML converts YAML representation into a system Rank.
func (r *Rank) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var i uint32
	if err := unmarshal(&i); err != nil {
		return err
	}
	if err := checkRank(Rank(i)); err != nil {
		return err
	}
	*r = Rank(i)
	return nil
}

// Equals compares this rank to the given rank.
func (r *Rank) Equals(other Rank) bool {
	if r == nil {
		return other.Equals(NilRank)
	}
	return *r == other
}

func checkRank(r Rank) error {
	if r == NilRank {
		return errors.Errorf("rank %d out of range [0, %d]", r, MaxRank)
	}
	return nil
}

// InList checks rank is present in provided rank list.
func (r *Rank) InList(ranks []Rank) bool {
	for _, rank := range ranks {
		if r.Equals(rank) {
			return true
		}
	}

	return false
}

// RemoveFromList removes given rank from provided list
// and returns modified list.
//
// Ignores miss in list.
func (r *Rank) RemoveFromList(ranks []Rank) []Rank {
	rankList := make([]Rank, 0, len(ranks))
	for _, rank := range ranks {
		if r.Equals(rank) {
			continue // skip this rank
		}
		rankList = append(rankList, rank)
	}

	return rankList
}

func (rl RankList) String() string {
	rs := make([]string, len(rl))
	for i, r := range rl {
		rs[i] = r.String()
	}
	return strings.Join(rs, ",")
}

// RanksToUint32 is a convenience method to convert this
// slice of system ranks to a slice of uint32 ranks.
func RanksToUint32(ranks []Rank) (uint32Ranks []uint32) {
	if ranks == nil {
		ranks = []Rank{}
	}
	if err := convert.Types(ranks, &uint32Ranks); err != nil {
		return nil
	}

	return
}

// RanksFromUint32 is a convenience method to convert this
// slice of uint32 ranks to a slice of system ranks.
func RanksFromUint32(ranks []uint32) (sysRanks []Rank) {
	if ranks == nil {
		ranks = []uint32{}
	}
	if err := convert.Types(ranks, &sysRanks); err != nil {
		return nil
	}

	return
}

// CheckRankMembership compares two Rank slices and returns a
// Rank slice with any ranks found in the second slice that do
// not exist in the first slice.
func CheckRankMembership(members, toTest []Rank) (missing []Rank) {
	mm := make(map[Rank]struct{})
	for _, m := range members {
		mm[m] = struct{}{}
	}

	for _, m := range toTest {
		if _, found := mm[m]; !found {
			missing = append(missing, m)
		}
	}

	return
}
