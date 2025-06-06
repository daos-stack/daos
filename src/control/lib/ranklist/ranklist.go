//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ranklist

import (
	"encoding/json"
	"math/bits"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func init() {
	// Can't imagine where this would be true, but better safe than sorry...
	if bits.UintSize < 32 {
		panic("uint on this system is not large enough to hold a Rank")
	}
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

// RankList provides convenience methods for working with Rank slices.
type RankList []Rank

func (rl RankList) String() string {
	rs := make([]string, len(rl))
	for i, r := range rl {
		rs[i] = r.String()
	}
	return strings.Join(rs, ",")
}

// RankSet implements a set of unique ranks in a condensed format.
type RankSet struct {
	ns *hostlist.NumericSet
}

// NewRankSet returns an initialized RankSet.
func NewRankSet() *RankSet {
	return &RankSet{ns: hostlist.NewNumericSet()}
}

func (rs *RankSet) String() string {
	if rs == nil || rs.ns == nil {
		return ""
	}
	return fixBrackets(rs.ns.String(), true)
}

// RangedString returns a ranged string representation of the RankSet.
func (rs *RankSet) RangedString() string {
	if rs == nil || rs.ns == nil {
		return ""
	}
	return rs.ns.RangedString()
}

// Count returns the number of ranks in the set.
func (rs *RankSet) Count() int {
	if rs == nil || rs.ns == nil {
		return 0
	}
	return rs.ns.Count()
}

// Merge merge the supplied RankSet into the receiver.
func (rs *RankSet) Merge(other *RankSet) {
	if rs == nil || other == nil {
		return
	}

	if rs.ns == nil {
		rs.ns = other.ns
		return
	}

	rs.ns.Merge(other.ns)
}

// Replace replaces the contents of the receiver with the supplied RankSet.
func (rs *RankSet) Replace(other *RankSet) {
	if rs == nil || other == nil || other.ns == nil {
		return
	}

	if rs.ns == nil {
		rs.ns = other.ns
		return
	}

	rs.ns.Replace(other.ns)
}

// Add adds rank to an existing RankSet.
func (rs *RankSet) Add(rank Rank) {
	if rs == nil || rs.ns == nil {
		rs.ns = hostlist.NewNumericSet()
	}
	rs.ns.Add(uint(rank))
}

// Delete removes the specified rank from the RankSet.
func (rs *RankSet) Delete(rank Rank) {
	if rs == nil || rs.ns == nil {
		return
	}
	rs.ns.Delete(uint(rank))
}

// Ranks returns a slice of Rank from a RankSet.
func (rs *RankSet) Ranks() (out []Rank) {
	out = make([]Rank, 0, rs.Count())

	if rs == nil || rs.ns == nil {
		return
	}

	for _, rVal := range rs.ns.Slice() {
		out = append(out, Rank(rVal))
	}

	return
}

// Ranks returns true if Rank found in RankSet.
func (rs *RankSet) Contains(r Rank) bool {
	if rs == nil || rs.ns == nil {
		return false
	}

	return rs.ns.Contains(uint(r))
}

func (rs *RankSet) MarshalJSON() ([]byte, error) {
	if rs == nil {
		return json.Marshal(nil)
	}
	return json.Marshal(rs.Ranks())
}

func (rs *RankSet) UnmarshalJSON(data []byte) error {
	if rs == nil {
		return errors.New("nil RankSet")
	}

	var ranks []Rank
	if err := json.Unmarshal(data, &ranks); err == nil {
		rs.Replace(RankSetFromRanks(ranks))
		return nil
	}

	// If the input doesn't parse as a JSON array, try parsing
	// it as a ranged string.
	trimmed := strings.Trim(string(data), "\"")
	if trimmed == "[]" {
		rs.Replace(&RankSet{})
		return nil
	}

	newRs, err := CreateRankSet(trimmed)
	if err != nil {
		return err
	}
	rs.Replace(newRs)

	return nil
}

// MustCreateRankSet is like CreateRankSet but will panic on error.
func MustCreateRankSet(stringRanks string) *RankSet {
	rs, err := CreateRankSet(stringRanks)
	if err != nil {
		panic(err)
	}
	return rs
}

// CreateRankSet creates a new HostList with ranks rather than hostnames from the
// supplied string representation.
func CreateRankSet(stringRanks string) (*RankSet, error) {
	rs := NewRankSet()

	if len(stringRanks) < 1 {
		return rs, nil
	}

	stringRanks = fixBrackets(stringRanks, false)

	// add enclosing brackets to input so CreateSet works without hostnames
	ns, err := hostlist.CreateNumericSet(stringRanks)
	if err != nil {
		return nil, err
	}
	rs.ns.Merge(ns)

	return rs, nil
}

// RankSetFromRanks returns a RankSet created from the supplied Rank slice.
func RankSetFromRanks(ranks RankList) *RankSet {
	rs := NewRankSet()

	for _, r := range ranks {
		rs.Add(r)
	}

	return rs
}

// ParseRanks takes a string representation of a list of ranks e.g. 1-4,6 and
// returns a slice of Rank type or error.
func ParseRanks(stringRanks string) ([]Rank, error) {
	rs, err := CreateRankSet(stringRanks)
	if err != nil {
		return nil, errors.Wrapf(err, "creating rank set from '%s'", stringRanks)
	}

	return rs.Ranks(), nil
}
