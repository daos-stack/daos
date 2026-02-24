//
// (C) Copyright 2019-2021 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ranklist

import (
	"math"
	"strconv"

	"github.com/pkg/errors"
)

type (
	// Rank is used to uniquely identify a server within a cluster.
	Rank uint32
)

const (
	// MaxRank is the largest valid Rank value.
	MaxRank Rank = math.MaxUint32 - 1
	// NilRank is an undefined Rank (0 is a valid Rank).
	NilRank Rank = math.MaxUint32

	// NilRankStr is a string representing an undefined Rank.
	NilRankStr string = "NilRank"
)

// NewRankPtr creates a Rank representation of
// the given uint32 and returns a pointer to it.
func NewRankPtr(in uint32) *Rank {
	r := Rank(in)
	return &r
}

// NewRankFromString parses a rank number or "NilRank" and creates a new Rank pointer.
func NewRankFromString(value string) (*Rank, error) {
	if value == "" {
		return nil, errors.New("cannot create rank from empty string")
	}

	if value == NilRankStr {
		return NewRankPtr(uint32(NilRank)), nil
	}

	rankNum, err := strconv.ParseUint(value, 10, 32)
	if err != nil {
		return nil, errors.Wrapf(err, "invalid rank %q", value)
	}
	return NewRankPtr(uint32(rankNum)), nil
}

func (r *Rank) String() string {
	switch {
	case r == nil:
		return NilRankStr
	case r.Equals(NilRank):
		return NilRankStr
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

// RanksToUint32 is a convenience method to convert a slice of system ranks to a slice of uint32s.
func RanksToUint32(in []Rank) []uint32 {
	out := make([]uint32, len(in))
	for i := range in {
		out[i] = in[i].Uint32()
	}

	return out
}

// RanksFromUint32 is a convenience method to convert a slice of uint32s to a slice of system ranks.
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
