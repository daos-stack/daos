//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"bytes"
	"fmt"
	"sort"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// RankGroups maps a set of ranks to string value (group).
type RankGroups map[string]*ranklist.RankSet

const replaceSep = " "

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
	ranksSeen := make(map[ranklist.Rank]struct{})

	for _, m := range members {
		if _, exists := ranksSeen[m.Rank]; exists {
			return &ErrMemberExists{Rank: &m.Rank}
		}
		ranksSeen[m.Rank] = struct{}{}

		if _, exists := ranksInState[m.State]; !exists {
			ranksInState[m.State] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksInState[m.State], "%d,", m.Rank)
	}

	for state, ranksStrBuf := range ranksInState {
		rankSet, err := ranklist.CreateRankSet(
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
// Supplied fieldsep parameter is used to separate row field elements in the string that is used
// as the key for the rank groups.
func (rgs RankGroups) FromMemberResults(results MemberResults, fieldSep string) error {
	if rgs == nil || len(rgs) > 0 {
		return errors.New("expecting non-nil empty rank groups")
	}
	// Refuse to use separator pattern if it matches the replacement.
	if fieldSep == replaceSep {
		return errors.Errorf("illegal field separator %q", fieldSep)
	}

	ranksWithResult := make(map[string]*bytes.Buffer)
	ranksSeen := make(map[ranklist.Rank]struct{})

	for _, r := range results {
		if _, exists := ranksSeen[r.Rank]; exists {
			return errors.Wrap(&ErrMemberExists{Rank: &r.Rank},
				"duplicate result for rank")
		}
		ranksSeen[r.Rank] = struct{}{}

		msg := "OK"
		if r.Errored {
			msg = strings.Replace(r.Msg, fieldSep, replaceSep, -1)
		}
		if r.Action == "" {
			return errors.Errorf(
				"action field empty for rank %d result", r.Rank)
		}

		resStr := fmt.Sprintf("%s%s%s", r.Action, fieldSep, msg)
		if _, exists := ranksWithResult[resStr]; !exists {
			ranksWithResult[resStr] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksWithResult[resStr], "%d,", r.Rank)
	}

	for strResult, ranksStrBuf := range ranksWithResult {
		rankSet, err := ranklist.CreateRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return errors.WithMessage(err,
				"generating groups of ranks with same result")
		}
		rgs[strResult] = rankSet
	}

	return nil
}
