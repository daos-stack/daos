//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package hostlist

import (
	"errors"
	"fmt"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
)

var (
	ErrEmpty    = errors.New("hostlist is empty")
	ErrNotFound = errors.New("hostname not found")
)

const (
	// MaxRange is the largest host range (hi - lo) supported
	MaxRange = 16384
)

const (
	outerRangeSeparators = "\t, "
	innerRangeSeparator  = ","
	rangeOperator        = "-"
)

type (
	hostName struct {
		prefix    string
		suffix    string
		number    uint
		width     int
		hasNumber bool
	}

	// HostList is a list of hostnames optimized for a "prefixXXXX"
	// naming convention, where XXXX is a decimal, numeric suffix.
	HostList struct {
		sync.RWMutex
		ranges    hostRanges
		hostCount int
	}
)

func (hn *hostName) Parse(input string) error {
	// prefixN (default)
	re := regexp.MustCompile(`^([a-zA-Z]+)(\d+)?(.*)?`)
	if strings.Contains(input, "-") {
		// handle hosts in the format prefixN-N
		re = regexp.MustCompile(`^(\w+-?)(\d+)?(.*)?`)
	}

	matches := re.FindStringSubmatch(input)
	if matches == nil {
		// Try a special case for IP addresses, where the first three octets
		// are treated as a prefix, and the fourth is treated as a host number.
		re = regexp.MustCompile(`^(\d{1,3}\.\d{1,3}\.\d{1,3}\.)(\d{1,3})(.*)`)
		if matches = re.FindStringSubmatch(input); matches == nil {
			return fmt.Errorf("invalid hostname %q", input)
		}
	}

	if len(matches[1]) == 0 {
		return fmt.Errorf("invalid hostname (missing prefix) %q", input)
	}
	hn.prefix = matches[1]

	if len(matches[2]) > 0 {
		number, err := strconv.ParseUint(matches[2], 10, 0)
		if err != nil {
			return err
		}
		hn.number = uint(number)
		hn.hasNumber = true
		hn.width = len(matches[2])
	}
	hn.suffix = matches[3]

	return nil
}

func createHostName(input string) (*hostName, error) {
	hn := &hostName{}
	return hn, hn.Parse(input)
}

func nextToken(input, sep string) (output string, token string) {
	// Skip over any preceding separators.
	start := 0
	for i, ch := range input {
		if strings.ContainsRune(sep, ch) {
			continue
		}
		start = i
		break
	}
	token = input[start:]
	output = input[start:]

	// Scan for the next separator that's not inside brackets.
	// If we don't find one, that's the end of the string.
	level := 0
	for i, ch := range token {
		switch ch {
		case '[':
			level++
			continue
		case ']':
			level--
			continue
		}
		if level <= 0 && strings.ContainsRune(sep, ch) {
			token = token[:i]
			if len(output[i:]) > 0 {
				output = output[i+1:]
			}
			return
		}
	}

	output = output[len(token):]
	return
}

func parseRange(input, rangeOp string) (*hostRange, error) {
	lohi := strings.Split(input, rangeOp)
	if len(lohi) == 0 || len(lohi) > 2 {
		return nil, fmt.Errorf("invalid range: %q", input)
	}

	lo, err := strconv.ParseUint(strings.TrimSpace(lohi[0]), 10, 0)
	if err != nil {
		return nil, fmt.Errorf("invalid range %q (%q is not uint)", input, lohi[0])
	}

	hi := lo
	if len(lohi) == 2 {
		hi, err = strconv.ParseUint(strings.TrimSpace(lohi[1]), 10, 0)
		if err != nil {
			return nil, fmt.Errorf("invalid range %q (%q is not uint)", input, lohi[1])
		}
	}

	if hi < lo {
		return nil, fmt.Errorf("invalid range %q (%d < %d)", input, hi, lo)
	}
	if hi-lo > MaxRange {
		return nil, fmt.Errorf("invalid range %q (> %d hosts)", input, MaxRange)
	}

	return &hostRange{
		lo:      uint(lo),
		hi:      uint(hi),
		width:   len(lohi[0]),
		isRange: true,
	}, nil
}

func parseRanges(input, rangeOp string) (ranges []*hostRange, err error) {
	for _, shr := range strings.Split(input, innerRangeSeparator) {
		r, err := parseRange(shr, rangeOp)
		if err != nil {
			return nil, err
		}
		ranges = append(ranges, r)
	}

	return
}

func parseBracketedHostList(input, rangeSep, rangeOp string, nameOptional bool) (*HostList, error) {
	hl := &HostList{}

	if len(input) == 0 {
		return hl, nil
	}

	done := false
	for scanStr, tok := nextToken(input, rangeSep); !done; scanStr, tok = nextToken(scanStr, rangeSep) {
		if len(scanStr) == 0 {
			done = true
		}

		var leftIndex, rightIndex int
		if leftIndex = strings.IndexRune(tok, '['); leftIndex == -1 {
			if !nameOptional {
				if err := hl.PushHost(tok); err != nil {
					return nil, err
				}
			}
			continue
		}

		if rightIndex = strings.IndexRune(tok, ']'); rightIndex == -1 {
			return nil, fmt.Errorf("invalid range %q", tok)
		}

		if rightIndex <= leftIndex {
			return nil, fmt.Errorf("invalid range %q", tok)
		}

		ranges, err := parseRanges(tok[leftIndex+1:rightIndex], rangeOp)
		if err != nil {
			return nil, err
		}

		prefix := tok[:leftIndex]
		if len(prefix) == 0 && !nameOptional {
			return nil, fmt.Errorf("invalid range: %q", tok)
		}
		var suffix string
		if len(tok[rightIndex:]) > 0 {
			suffix = tok[rightIndex+1:]
		}

		for _, hr := range ranges {
			hr.prefix = prefix
			hr.suffix = suffix
			hl.pushRange(hr)
		}
	}

	return hl, nil
}

// MustCreate is like Create but will panic on error.
func MustCreate(stringHosts string) *HostList {
	hl, err := Create(stringHosts)
	if err != nil {
		panic(err)
	}
	return hl
}

// Create creates a new HostList from the supplied string representation.
func Create(stringHosts string) (*HostList, error) {
	return parseBracketedHostList(stringHosts, outerRangeSeparators,
		rangeOperator, false)
}

// String returns a ranged string representation of the HostList.
func (hl *HostList) String() string {
	if hl == nil {
		return "nil hostlist"
	}
	return hl.RangedString()
}

// RangedString returns a string containing a bracketed HostList representation.
func (hl *HostList) RangedString() string {
	hl.RLock()
	defer hl.RUnlock()

	var bld strings.Builder
	var open bool
	var next *hostRange

	for i, hr := range hl.ranges {
		next = nil
		if i+1 < len(hl.ranges) {
			next = hl.ranges[i+1]
		}

		if !open {
			bld.WriteString(hr.prefix)
			if hr.count() > 1 || (next != nil && hr.within(next)) {
				open = true
				bld.WriteString("[")
			}
		}

		bld.WriteString(hr.rangedString())

		if open {
			if next != nil && hr.within(next) {
				bld.WriteString(",")
				continue
			} else {
				open = false
				bld.WriteString("]")
			}
		}

		if !open && len(hr.suffix) > 0 {
			bld.WriteString(hr.suffix)
		}

		if i < len(hl.ranges)-1 {
			bld.WriteString(",")
		}
	}

	return bld.String()
}

// DerangedString returns a string containing the hostnames of
// every host in the HostList, without any bracketing.
func (hl *HostList) DerangedString() string {
	hl.RLock()
	defer hl.RUnlock()

	var bld strings.Builder

	for i, hr := range hl.ranges {
		bld.WriteString(hr.derangedString())
		if i < len(hl.ranges)-1 {
			bld.WriteString(",")
		}
	}

	return bld.String()
}

// Push adds a string representation of hostnames to this HostList.
func (hl *HostList) Push(stringHosts string) error {
	other, err := Create(stringHosts)
	if err != nil {
		return err
	}

	hl.PushList(other)
	return nil
}

// PushHost adds a single host to this HostList.
func (hl *HostList) PushHost(stringHost string) error {
	hn, err := createHostName(stringHost)
	if err != nil {
		return err
	}

	hl.Lock()
	defer hl.Unlock()

	if hn.hasNumber {
		hl.pushRange(&hostRange{
			prefix:  hn.prefix,
			suffix:  hn.suffix,
			lo:      hn.number,
			hi:      hn.number,
			width:   hn.width,
			isRange: true,
		})
		return nil
	}

	hl.pushRange(&hostRange{
		prefix: stringHost,
	})
	return nil
}

func (hl *HostList) pushRange(hr *hostRange) {
	if len(hl.ranges) > 0 {
		tail := hl.ranges[len(hl.ranges)-1]
		if tail.canAppend(hr) {
			tail.hi = hr.hi
			hl.hostCount += hr.count()
			return
		}
	}

	hl.ranges = append(hl.ranges, hr)
	hl.hostCount += hr.count()
}

// ReplaceList replaces this HostList's contents with the supplied HostList.
func (hl *HostList) ReplaceList(other *HostList) {
	if other == nil {
		return
	}

	hl.Lock()
	defer hl.Unlock()
	other.RLock()
	defer other.RUnlock()

	hl.ranges = nil
	hl.hostCount = 0
	hl.pushList(other)
}

func (hl *HostList) pushList(other *HostList) {
	for _, hr := range other.ranges {
		// Make copies of the ranges to ensure that they are independent.
		newRange := new(hostRange)
		*newRange = *hr
		hl.pushRange(newRange)
	}
}

// PushList adds the supplied HostList onto this HostList.
func (hl *HostList) PushList(other *HostList) {
	if other == nil {
		return
	}

	hl.Lock()
	defer hl.Unlock()
	other.RLock()
	defer other.RUnlock()

	hl.pushList(other)
}

func fmtRangeHost(hr *hostRange, num uint) string {
	return fmt.Sprintf("%s%0*d%s", hr.prefix, hr.width, num, hr.suffix)
}

// Pop returns the string representation of the last host pushed
// onto the HostList and then removes it from the HostList. Returns
// an error if the HostList is empty.
func (hl *HostList) Pop() (hostName string, err error) {
	hl.Lock()
	defer hl.Unlock()

	if len(hl.ranges) == 0 {
		return "", ErrEmpty
	}

	hl.hostCount--

	tailIdx := len(hl.ranges) - 1
	tail := hl.ranges[tailIdx]
	if !tail.isRange {
		hostName = tail.prefix
		hl.ranges = hl.ranges[:tailIdx]
		return
	}

	hostName = fmtRangeHost(tail, tail.hi)
	if tail.hi > 0 {
		tail.hi--
	}
	if tail.hi < tail.lo || (tail.hi == tail.lo && tail.lo == 0) {
		hl.ranges = hl.ranges[:tailIdx]
	}

	return
}

// Shift returns the string representation of the first host pushed
// onto the HostList and then removes it from the HostList. Returns
// an error if the HostList is empty.
func (hl *HostList) Shift() (hostName string, err error) {
	hl.Lock()
	defer hl.Unlock()

	if len(hl.ranges) == 0 {
		return "", ErrEmpty
	}

	hl.hostCount--

	head := hl.ranges[0]
	if !head.isRange {
		hostName = head.prefix
		hl.ranges = hl.ranges[1:]
		return
	}

	hostName = fmtRangeHost(head, head.lo)
	head.lo++
	if head.lo > head.hi {
		hl.ranges = hl.ranges[1:]
	}

	return
}

func (hl *HostList) getNthHostRange(n int) (int, *hostRange, int, error) {
	if hl.hostCount == 0 {
		return -1, nil, 0, ErrEmpty
	}

	if n < 0 {
		return -1, nil, 0, errors.New("index can't be < 0")
	}

	if n >= hl.hostCount {
		return -1, nil, 0, errors.New("index must be < hostCount")
	}

	var counted int
	for idx, hr := range hl.ranges {
		rangeCount := hr.count()

		if n <= rangeCount-1+counted {
			return idx, hr, counted, nil
		}
		counted += rangeCount
	}

	// notreached?
	return -1, nil, 0, errors.New("unknown error")
}

// Nth returns the string representation of the n-th host in
// the HostList. Returns an error if the index is invalid.
func (hl *HostList) Nth(n int) (string, error) {
	hl.RLock()
	defer hl.RUnlock()

	_, hr, depth, err := hl.getNthHostRange(n)
	if err != nil {
		return "", err
	}

	if hr.isRange {
		return fmtRangeHost(hr, hr.lo+uint(n)-uint(depth)), nil
	}
	return hr.prefix, nil
}

func fmtRange(hr *hostRange) (rs string) {
	rs = hr.prefix

	if hr.isRange {
		var lBr, rBr string
		if hr.count() > 1 {
			lBr = "["
			rBr = "]"
		}
		rs = fmt.Sprintf("%s%s%s%s%s", hr.prefix, lBr, hr.rangedString(), rBr, hr.suffix)
	}

	return
}

// PopRange returns the string representation of the last
// bracketed list of hosts. All hosts in the returned list
// are removed from the HostList. Returns an error if the
// HostList is empty.
func (hl *HostList) PopRange() (hostRange string, err error) {
	hl.Lock()
	defer hl.Unlock()

	if len(hl.ranges) == 0 {
		return "", ErrEmpty
	}

	tailIdx := len(hl.ranges) - 1
	tail := hl.ranges[tailIdx]
	hostRange = fmtRange(tail)

	hl.ranges = hl.ranges[:tailIdx]
	hl.hostCount -= tail.count()

	return
}

// ShiftRange returns the string representation of the first
// bracketed list of hosts. All hosts in the returned list
// are removed from the HostList. Returns an error if the
// HostList is empty.
func (hl *HostList) ShiftRange() (hostRange string, err error) {
	hl.Lock()
	defer hl.Unlock()

	if len(hl.ranges) == 0 {
		return "", ErrEmpty
	}

	head := hl.ranges[0]
	hostRange = fmtRange(head)

	hl.ranges = hl.ranges[1:]
	hl.hostCount -= head.count()

	return
}

// Find searches the HostList for the given hostname. Returns
// the index of the host and true if found; -1 and false if not.
func (hl *HostList) Find(stringHost string) (int, bool) {
	hn, err := createHostName(stringHost)
	if err != nil {
		return -1, false
	}

	hl.RLock()
	defer hl.RUnlock()

	var counted int
	for _, hr := range hl.ranges {
		if offset, contains := hr.containsHost(hn); contains {
			return counted + int(offset), true
		}
		counted += hr.count()
	}

	return -1, false
}

// Delete removes all hosts in the supplied string representation of
// a HostList. Returns the number of hosts successfully deleted.
func (hl *HostList) Delete(stringHosts string) (int, error) {
	if hl.IsEmpty() {
		return 0, ErrEmpty
	}

	tmp, err := Create(stringHosts)
	if err != nil {
		return 0, err
	}

	deleted := 0
	for host, err := tmp.Pop(); err == nil; host, err = tmp.Pop() {
		if delErr := hl.DeleteHost(host); delErr == nil {
			deleted++
		}
	}

	return deleted, nil
}

// DeleteHost removes the first host that matches the supplied hostname.
// Returns an error if the HostList is empty or the hostname is not found.
func (hl *HostList) DeleteHost(stringHost string) error {
	if hl.IsEmpty() {
		return ErrEmpty
	}

	if idx, found := hl.Find(stringHost); found {
		return hl.DeleteNth(idx)
	}

	return ErrNotFound
}

func (hl *HostList) deleteRangeAt(idx int) error {
	if idx < 0 || idx >= len(hl.ranges) {
		return errors.New("invalid index in deleteRangeAt()")
	}

	hr := hl.ranges[idx]
	hl.hostCount -= hr.count()
	// optimized delete (allows deleted hostRange to be released for GC)
	if idx < len(hl.ranges)-1 {
		copy(hl.ranges[idx:], hl.ranges[idx+1:])
	}
	hl.ranges[len(hl.ranges)-1] = nil
	hl.ranges = hl.ranges[:len(hl.ranges)-1]

	return nil
}

func (hl *HostList) insertRangeAt(idx int, hr *hostRange) {
	// optimized insert (avoids extra slice/copy)
	hl.ranges = append(hl.ranges, &hostRange{})
	copy(hl.ranges[idx+1:], hl.ranges[idx:])
	hl.ranges[idx] = hr
}

// DeleteNth removes the host at position N in the HostList. Returns
// an error if the HostList is empty or the index is incorrect.
func (hl *HostList) DeleteNth(n int) error {
	hl.Lock()
	defer hl.Unlock()

	idx, hr, depth, err := hl.getNthHostRange(n)
	if err != nil {
		return err
	}

	if !hr.isRange {
		return hl.deleteRangeAt(idx)
	}

	hostNum := hr.lo + uint(n) - uint(depth)
	newHr, err := hr.deleteHost(hostNum)
	if err != nil {
		return err
	}

	hl.hostCount -= 1

	if newHr != nil {
		hl.insertRangeAt(idx+1, newHr)
	}

	if hr.count() == 0 {
		return hl.deleteRangeAt(idx)
	}
	return nil
}

// Within returns true if all hosts in the supplied hosts are contained
// within the HostList, false otherwise.
func (hl *HostList) Within(stringHosts string) (bool, error) {
	toFind, err := Create(stringHosts)
	if err != nil {
		return false, err
	}

	for host, err := toFind.Pop(); err == nil; host, err = toFind.Pop() {
		if _, found := hl.Find(host); !found {
			return false, nil
		}
	}

	return true, nil
}

// Intersects returns a *HostList containing hosts which are in both this
// HostList and the supplied hosts string.
func (hl *HostList) Intersects(stringHosts string) (*HostList, error) {
	toFind, err := Create(stringHosts)
	if err != nil {
		return nil, err
	}
	intersection, err := Create("")
	if err != nil {
		return nil, err
	}

	for host, err := toFind.Pop(); err == nil; host, err = toFind.Pop() {
		if _, found := hl.Find(host); found {
			if err := intersection.PushHost(host); err != nil {
				return nil, err
			}
		}
	}

	return intersection, nil
}

// Count returns the number of hosts in the HostList.
func (hl *HostList) Count() int {
	hl.RLock()
	defer hl.RUnlock()

	return int(hl.hostCount)
}

// IsEmpty returns true if the HostList has zero hosts.
func (hl *HostList) IsEmpty() bool {
	return hl.Count() <= 0
}

// Uniq forces a sort operation on the HostList and removes duplicates.
func (hl *HostList) Uniq() {
	hl.Lock()
	defer hl.Unlock()

	if len(hl.ranges) <= 1 {
		return
	}

	sort.Sort(hl.ranges)

	for i := 1; i < len(hl.ranges); {
		prev := hl.ranges[i-1]
		cur := hl.ranges[i]
		dupes := prev.join(cur)
		if dupes >= 0 {
			hl.hostCount -= dupes
			hl.ranges = append(hl.ranges[:i], hl.ranges[i+1:]...)
		} else {
			i++
		}
	}
}
