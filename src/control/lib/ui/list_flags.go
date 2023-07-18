//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"fmt"
	"sort"
	"strings"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	_ flags.Unmarshaler = &RankSetFlag{}
	_ flags.Unmarshaler = &HostSetFlag{}
)

// RankSetFlag is a go-flags compatible flag type for
// handling inputs that can be converted to a system.RankSet.
type RankSetFlag struct {
	ranklist.RankSet
}

// Empty returns true if the flag was not set.
func (f RankSetFlag) Empty() bool {
	return f.Count() == 0
}

// UnmarshalFlag implements the go-flags.Unmarshaler
// interface.
func (f *RankSetFlag) UnmarshalFlag(fv string) error {
	rs, err := ranklist.CreateRankSet(fv)
	if err != nil {
		return err
	}
	f.Replace(rs)

	return nil
}

func (f *RankSetFlag) MarshalJSON() ([]byte, error) {
	return []byte(f.String()), nil
}

// HostSetFlag is a go-flags compatible flag type for
// handling inputs that can be converted to a hostlist.HostSet.
type HostSetFlag struct {
	hostlist.HostSet
}

// Empty returns true if the flag was not set.
func (f *HostSetFlag) Empty() bool {
	return f.Count() == 0
}

// UnmarshalFlag implements the go-flags.Unmarshaler
// interface.
func (f *HostSetFlag) UnmarshalFlag(fv string) error {
	rs, err := hostlist.CreateSet(fv)
	if err != nil {
		return err
	}
	f.Replace(rs)

	return nil
}

func (f *HostSetFlag) MarshalJSON() ([]byte, error) {
	return []byte(f.String()), nil
}

// MemberStateSetFlag is a go-flags compatible flag type for handling inputs that can be converted
// to a system.MemberState slice.
type MemberStateSetFlag struct {
	States system.MemberState
}

// Empty returns true if the flag was not set.
func (f *MemberStateSetFlag) Empty() bool {
	return f.States == 0
}

// States are specified as a comma separated list of AwaitFormat Starting Ready Joined Stopping
// Stopped Excluded AdminExcluded Errored Unresponsive
func memberStateMaskFromStrings(statesStr string) (system.MemberState, error) {
	var states []system.MemberState

	for _, tok := range strings.Split(statesStr, ",") {
		ms := system.MemberStateFromString(strings.TrimSpace(tok))
		if ms == system.MemberStateUnknown {
			return 0, errors.Errorf("invalid state name %q", tok)
		}
		states = append(states, ms)
	}

	mask, _ := system.MemberStates2Mask(states...)
	return mask, nil
}

// UnmarshalFlag implements the go-flags.Unmarshaler interface.
func (f *MemberStateSetFlag) UnmarshalFlag(fv string) error {
	if fv == "" {
		return nil
	}

	mask, err := memberStateMaskFromStrings(fv)
	if err != nil {
		return err
	}
	f.States = mask

	return nil
}

// MarshalJSON implements the json.Marshaler interface.
func (f *MemberStateSetFlag) MarshalJSON() ([]byte, error) {
	return []byte(fmt.Sprintf("%d", f.States)), nil
}

// Complete implements the go-flags.Completer interface and is used to suggest possible completions
// for the supplied input string.
func (f *MemberStateSetFlag) Complete(match string) (comps []flags.Completion) {
	s := system.MemberState(1)
	for s != system.MemberStateMax {
		if strings.HasPrefix(strings.ToLower(s.String()), strings.ToLower(match)) {
			comps = append(comps, flags.Completion{
				Item: s.String(),
			})
		}
		s = system.MemberState(int(s) << 1)
	}

	sort.Slice(comps, func(i, j int) bool {
		return comps[i].Item < comps[j].Item
	})

	return
}
