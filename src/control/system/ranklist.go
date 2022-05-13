//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"bytes"
	"fmt"
	"math/bits"
	"sort"
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
	if rs == nil || other == nil {
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
	if rs.ns == nil {
		rs.ns = hostlist.NewNumericSet()
	}
	rs.ns.Add(uint(rank))
}

// Delete removes the specified rank from the RankSet.
func (rs *RankSet) Delete(rank Rank) {
	if rs.ns == nil {
		return
	}
	rs.ns.Delete(uint(rank))
}

// Ranks returns a slice of Rank from a RankSet.
func (rs *RankSet) Ranks() (out []Rank) {
	out = make([]Rank, 0, rs.Count())

	if rs.ns == nil {
		return
	}

	for _, rVal := range rs.ns.Slice() {
		out = append(out, Rank(rVal))
	}

	return
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

// RankGroups maps a set of ranks to string value (group).
type RankGroups map[string]*RankSet

// Keys returns sorted group names.
//
// Sort first by number of ranks in grouping then by alphabetical order of
// group name.
func (rgs RankGroups) Keys() []string {
	keys := make([]string, 0, len(rgs))

	for key := range rgs {
		keys = append(keys, key)
	}
	sort.Slice(keys, func(i, j int) bool {
		ci := rgs[keys[i]].Count()
		cj := rgs[keys[j]].Count()
		if ci == cj {
			return keys[i] < keys[j]
		}

		return ci > cj
	})

	return keys
}

func (rgs RankGroups) String() string {
	var buf bytes.Buffer

	padding := 0
	keys := rgs.Keys()
	for _, key := range keys {
		valStr := rgs[key].String()
		if len(valStr) > padding {
			padding = len(valStr)
		}
	}

	for _, key := range rgs.Keys() {
		fmt.Fprintf(&buf, "%*s: %s\n", padding, rgs[key], key)
	}

	return buf.String()
}

// FromMembers initializes groupings of ranks that are at a particular state
// from a slice of system members.
func (rgs RankGroups) FromMembers(members Members) error {
	if rgs == nil || len(rgs) > 0 {
		return errors.New("expecting non-nil empty rank groups")
	}

	ranksInState := make(map[MemberState]*bytes.Buffer)
	ranksSeen := make(map[Rank]struct{})

	for _, m := range members {
		if _, exists := ranksSeen[m.Rank]; exists {
			return &ErrMemberExists{Rank: &m.Rank}
		}
		ranksSeen[m.Rank] = struct{}{}

		if _, exists := ranksInState[m.State()]; !exists {
			ranksInState[m.State()] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksInState[m.State()], "%d,", m.Rank)
	}

	for state, ranksStrBuf := range ranksInState {
		rankSet, err := CreateRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return errors.WithMessage(err,
				"generating groups of ranks at state")
		}
		rgs[state.String()] = rankSet
	}

	return nil
}

// FromMemberResults initializes groupings of ranks that had a particular result
// from a requested action, populated from a slice of system member results.
//
// Supplied rowFieldsep parameter is used to separate row field elements in
// the string that is used as the key for the rank groups.
func (rgs RankGroups) FromMemberResults(results MemberResults, rowFieldSep string) error {
	if rgs == nil || len(rgs) > 0 {
		return errors.New("expecting non-nil empty rank groups")
	}

	ranksWithResult := make(map[string]*bytes.Buffer)
	ranksSeen := make(map[Rank]struct{})

	for _, r := range results {
		if _, exists := ranksSeen[r.Rank]; exists {
			return errors.Wrap(&ErrMemberExists{Rank: &r.Rank},
				"duplicate result for rank")
		}
		ranksSeen[r.Rank] = struct{}{}

		msg := "OK"
		if r.Errored {
			msg = r.Msg
		}
		if r.Action == "" {
			return errors.Errorf(
				"action field empty for rank %d result", r.Rank)
		}

		resStr := fmt.Sprintf("%s%s%s", r.Action, rowFieldSep, msg)
		if _, exists := ranksWithResult[resStr]; !exists {
			ranksWithResult[resStr] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksWithResult[resStr], "%d,", r.Rank)
	}

	for strResult, ranksStrBuf := range ranksWithResult {
		rankSet, err := CreateRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return errors.WithMessage(err,
				"generating groups of ranks with same result")
		}
		rgs[strResult] = rankSet
	}

	return nil
}
