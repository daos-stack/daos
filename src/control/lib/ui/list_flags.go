//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
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

// MarshalFlag implements the go-flags.Marshaler interface.
func (f *HostSetFlag) MarshalJSON() ([]byte, error) {
	return []byte(f.String()), nil
}
