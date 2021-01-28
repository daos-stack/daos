//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package hostlist

import (
	"errors"
	"fmt"
	"strings"
)

type (
	hostRange struct {
		prefix  string
		suffix  string
		lo, hi  uint
		width   int
		isRange bool // default is a single host
	}

	hostRanges []*hostRange
)

// NB: These three methods implement sort.Interface to allow
// sorting a hostRange slice.
func (hrs hostRanges) Len() int {
	return len(hrs)
}

func (hrs hostRanges) Less(i, j int) bool {
	return hrs[i].cmp(hrs[j]) < 0
}

func (hrs hostRanges) Swap(i, j int) {
	hrs[i], hrs[j] = hrs[j], hrs[i]
}

func (hr *hostRange) canAppend(other *hostRange) bool {
	if hr.isRange && other.isRange &&
		hr.prefix == other.prefix &&
		hr.suffix == other.suffix &&
		hr.hi == other.lo-1 &&
		hr.combinesWidth(other) {
		return true
	}

	return false
}

func (hr *hostRange) count() int {
	return int(hr.hi - hr.lo + 1)
}

func (hr *hostRange) within(other *hostRange) bool {
	if hr.prefix == other.prefix && hr.suffix == other.suffix {
		return hr.isRange && other.isRange
	}

	return false
}

func (hr *hostRange) containsHost(hn *hostName) (off uint, cnt bool) {
	if !hr.isRange {
		if hr.prefix == hn.prefix {
			return 0, true
		}
		return
	}

	if !hn.hasNumber {
		return
	}

	if hr.prefix != hn.prefix || hr.suffix != hn.suffix {
		return
	}

	if hn.number >= hr.lo &&
		hn.number <= hr.hi &&
		hr.combinesWidth(&hostRange{lo: hn.number, width: hn.width}) {
		return hn.number - hr.lo, true
	}

	return
}

func zeroPadding(num uint, width int) int {
	numZeros := 1
	for num /= 10; num > 0; num /= 10 {
		numZeros++
	}
	if width > numZeros {
		return width - numZeros
	}
	return 0
}

func (hr *hostRange) combinesWidth(other *hostRange) bool {
	if hr.width == other.width {
		return true
	}

	// check to see if padding is compatible
	lpad := zeroPadding(hr.lo, hr.width)
	lrpad := zeroPadding(hr.lo, other.width)
	rpad := zeroPadding(other.lo, other.width)
	rlpad := zeroPadding(other.lo, hr.width)

	switch {
	case lpad != lrpad && rpad != rlpad:
		return false
	case lpad != lrpad:
		if rpad == rlpad {
			other.width = hr.width
			return true
		}
		return false
	case lpad == lrpad:
		hr.width = other.width
		return true
	}

	return false
}

func (hr *hostRange) cmpPrefix(other *hostRange) int {
	return strings.Compare(hr.prefix, other.prefix)
}

func (hr *hostRange) cmpSuffix(other *hostRange) int {
	return strings.Compare(hr.suffix, other.suffix)
}

func (hr *hostRange) cmp(other *hostRange) (cmpVal int) {
	if cmpVal = hr.cmpPrefix(other); cmpVal == 0 {
		if cmpVal = hr.cmpSuffix(other); cmpVal == 0 {
			if hr.combinesWidth(other) {
				cmpVal = int(hr.lo) - int(other.lo)
			} else {
				cmpVal = hr.width - other.width
			}
		}
	}

	return
}

func (hr *hostRange) join(other *hostRange) int {
	if hr.cmp(other) > 0 {
		return -1
	}

	if hr.prefix != other.prefix || hr.suffix != other.suffix || !hr.combinesWidth(other) {
		return -1
	}

	dupes := -1
	switch {
	case !hr.isRange && !other.isRange:
		dupes = 1
	case hr.hi == other.lo-1:
		hr.hi = other.hi
		dupes = 0
	case hr.hi >= other.lo:
		if hr.hi < other.hi {
			dupes = int(hr.hi) - int(other.lo) + 1
			hr.hi = other.hi
		} else {
			dupes = int(other.count())
		}
	}

	return dupes
}

func (hr *hostRange) deleteHost(n uint) (newHr *hostRange, err error) {
	if n < hr.lo || n > hr.hi {
		return nil, errors.New("index out of bounds in deleteHost()")
	}

	switch {
	case n == hr.lo:
		hr.lo++
	case n == hr.hi:
		hr.hi--
	default:
		// split into a new hostrange which gets
		// inserted after this one
		newHr = &hostRange{}
		*newHr = *hr
		hr.hi = n - 1
		newHr.lo = n + 1
	}

	return
}

func (hr *hostRange) rangedString() string {
	if !hr.isRange {
		return ""
	}

	if hr.lo == hr.hi {
		return fmt.Sprintf("%0*d", hr.width, hr.lo)
	}

	return fmt.Sprintf("%0*d-%0*d", hr.width, hr.lo, hr.width, hr.hi)
}

func (hr *hostRange) derangedString() string {
	if !hr.isRange {
		return hr.prefix
	}

	var bld strings.Builder

	for nr := hr.lo; nr <= hr.hi; nr++ {
		bld.WriteString(fmt.Sprintf("%s%0*d%s", hr.prefix, hr.width, nr, hr.suffix))
		if nr < hr.hi {
			bld.WriteString(",")
		}
	}

	return bld.String()
}
