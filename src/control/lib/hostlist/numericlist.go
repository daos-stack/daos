//
// (C) Copyright 2020-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hostlist

import (
	"fmt"
	"unicode"

	"github.com/pkg/errors"
)

// NumericList implements a subset of the HostList interface and is designed
// for use with lists of numbers.
type NumericList struct {
	hl *HostList
}

// Add adds the supplied numeric entry to the NumericList.
func (nl *NumericList) Add(i uint) {
	nl.hl.Lock()
	defer nl.hl.Unlock()

	nl.hl.pushRange(&hostRange{lo: i, hi: i, width: 1, isRange: true})
}

// Delete removes the supplied numeric entry from the NumericList. No-op if
// the entry is not present.
func (nl *NumericList) Delete(i uint) {
	nl.hl.Lock()
	defer nl.hl.Unlock()

	for idx, hr := range nl.hl.ranges {
		_, found := hr.containsHost(&hostName{number: i, hasNumber: true})
		if !found {
			continue
		}

		if hr.count() == 1 {
			if err := nl.hl.deleteRangeAt(idx); err != nil {
				panic(fmt.Sprintf("internal error: %s", err))
			}
			return
		}

		new, err := hr.deleteHost(i)
		if err != nil {
			panic(fmt.Sprintf("internal error: %s", err))
		}
		nl.hl.hostCount--

		if new != nil {
			nl.hl.insertRangeAt(idx, new)
		}
		return
	}
}

// Slice returns a slice of the numeric entries in the NumericList.
func (nl *NumericList) Slice() (out []uint) {
	nl.hl.RLock()
	defer nl.hl.RUnlock()

	for _, hr := range nl.hl.ranges {
		for i := hr.lo; i <= hr.hi; i++ {
			out = append(out, i)
		}
	}

	return
}

// Uniq sorts and removes duplicate entries from the NumericList.
func (nl *NumericList) Uniq() {
	nl.hl.Uniq()
}

func (nl *NumericList) String() string {
	return nl.hl.String()
}

func (nl *NumericList) RangedString() string {
	return nl.hl.RangedString()
}

// Count returns the number of numeric entries in the NumericList.
func (nl *NumericList) Count() int {
	return nl.hl.Count()
}

// Merge merges the contents of this NumericList with those of the other NumericList.
func (nl *NumericList) Merge(other *NumericList) {
	nl.hl.PushList(other.hl)
}

// Replace replaces the contents of this NumericList with those from the other NumericList.
func (nl *NumericList) Replace(other *NumericList) {
	nl.hl.ReplaceList(other.hl)
}

// Contains returns true if value is present in NumericList.
func (nl *NumericList) Contains(i uint) bool {
	if nl == nil || nl.hl == nil {
		return false
	}

	for _, n := range nl.Slice() {
		if n == i {
			return true
		}
	}

	return false
}

// NewNumericList creates an initialized NumericList with
// optional starting values.
func NewNumericList(values ...uint) *NumericList {
	nl := &NumericList{hl: &HostList{}}
	for _, v := range values {
		nl.Add(v)
	}
	return nl
}

// CreateNumericList creates a NumericList that contains numeric entries
// and ranges from the supplied string representation.
func CreateNumericList(stringRanges string) (*NumericList, error) {
	for _, r := range stringRanges {
		if unicode.IsSpace(r) {
			return nil, errors.New("unexpected whitespace character(s)")
		}
		if unicode.IsLetter(r) {
			return nil, errors.New("unexpected alphabetic character(s)")
		}
	}
	if stringRanges[0] != '[' && stringRanges[len(stringRanges)-1] != ']' {
		return nil, errors.New("missing brackets around numeric ranges")
	}

	hl, err := parseBracketedHostList(stringRanges, outerRangeSeparators,
		rangeOperator, true)
	if err != nil {
		return nil, err
	}
	return &NumericList{hl: hl}, nil
}

// NumericSet is a special case of a NumericList that contains unique numeric
// entries.
type NumericSet struct {
	NumericList
}

// Add adds the supplied numeric entry to the NumericSet.
func (ns *NumericSet) Add(i uint) {
	ns.NumericList.Add(i)
	ns.Uniq()
}

// Delete removes the supplied numeric entry from the NumericSet. No-op if
// the entry is not present.
func (ns *NumericSet) Delete(i uint) {
	ns.NumericList.Delete(i)
	ns.Uniq()
}

// Merge merges the contents of this NumericSet with those of the other NumericSet.
func (ns *NumericSet) Merge(other *NumericSet) {
	ns.hl.PushList(other.hl)
	ns.Uniq()
}

// Replace replaces the contents of this NumericSet with those from the other NumericSet.
func (ns *NumericSet) Replace(other *NumericSet) {
	ns.hl.ReplaceList(other.hl)
	ns.Uniq()
}

// Contains returns true if value is present in NumericSet.
func (ns *NumericSet) Contains(i uint) bool {
	if ns == nil {
		return false
	}

	return ns.NumericList.Contains(i)
}

// NewNumericSet creates an initialized NumericSet with optional
// starting values.
func NewNumericSet(values ...uint) *NumericSet {
	ns := &NumericSet{NumericList: NumericList{hl: &HostList{}}}
	for _, v := range values {
		ns.NumericList.Add(v)
	}
	ns.Uniq()

	return ns
}

// CreateNumericSet creates a NumericSet containing unique numeric entries
// and ranges from the supplied string representation.
func CreateNumericSet(stringRanges string) (*NumericSet, error) {
	nl, err := CreateNumericList(stringRanges)
	if err != nil {
		return nil, errors.Wrapf(err,
			"creating numeric set from %q", stringRanges)
	}
	nl.Uniq()

	return &NumericSet{NumericList: NumericList{hl: nl.hl}}, nil
}
