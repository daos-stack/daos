//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
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

var memberStateSetSep = ","

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
	var mask system.MemberState

	for _, tok := range strings.Split(statesStr, memberStateSetSep) {
		ms := system.MemberStateFromString(strings.TrimSpace(tok))
		if ms == system.MemberStateUnknown {
			return 0, errors.Errorf("invalid state name %q", tok)
		}
		mask |= ms
	}

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

// Complete implements the go-flags.Completer interface and is used to suggest possible completions
// for the supplied input string. Handle multiple member state,... completions.
func (f *MemberStateSetFlag) Complete(match string) (comps []flags.Completion) {
	var prefix string
	stateStrs := strings.Split(match, memberStateSetSep)
	if len(stateStrs) > 1 {
		match = stateStrs[len(stateStrs)-1]
		prefix = strings.Join(stateStrs[0:len(stateStrs)-1], memberStateSetSep)
		prefix += memberStateSetSep
	}

	s := system.MemberState(1)
	for s != system.MemberStateMax {
		hasMatch := strings.HasPrefix(strings.ToLower(s.String()), strings.ToLower(match))
		stateNotInPrefix := !strings.Contains(prefix, s.String())

		// If state has already been supplied, don't suggest it again.
		if hasMatch && stateNotInPrefix {
			comps = append(comps, flags.Completion{
				Item: prefix + s.String(),
			})
		}

		s = system.MemberState(int(s) << 1)
	}

	sort.Slice(comps, func(i, j int) bool {
		return comps[i].Item < comps[j].Item
	})

	return
}
