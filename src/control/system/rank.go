//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"math"
	"strconv"
	"strings"

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
func RanksToUint32(in []Rank) []uint32 {
	out := make([]uint32, len(in))
	for i := range in {
		out[i] = in[i].Uint32()
	}

	return out
}

// RanksFromUint32 is a convenience method to convert this
// slice of uint32 ranks to a slice of system ranks.
func RanksFromUint32(in []uint32) []Rank {
	out := make([]Rank, len(in))
	for i := range in {
		out[i] = Rank(in[i])
	}

	return out
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
