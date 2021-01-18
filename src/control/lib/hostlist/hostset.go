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
	"sync"
)

type (
	// HostSet is a special case of HostList which never contains duplicates
	// and is always sorted alphanumerically based on prefix.
	HostSet struct {
		sync.Mutex
		list *HostList
	}
)

// MarshalJSON outputs JSON representation of HostSet.
func (hs *HostSet) MarshalJSON() ([]byte, error) {
	return []byte(`"` + hs.RangedString() + `"`), nil
}

// MustCreateSet is like CreateSet but will panic on error.
func MustCreateSet(stringHosts string) *HostSet {
	hs, err := CreateSet(stringHosts)
	if err != nil {
		panic(err)
	}
	return hs
}

// CreateSet creates a new HostSet from the supplied string representation.
func CreateSet(stringHosts string) (*HostSet, error) {
	hl, err := Create(stringHosts)
	if err != nil {
		return nil, err
	}
	hl.Uniq()

	return &HostSet{list: hl}, nil
}

// initList will initialize the underlying *HostList if necessary
func (hs *HostSet) initList() {
	hs.Lock()
	defer hs.Unlock()

	if hs.list == nil {
		hs.list, _ = Create("")
	}
}

func (hs *HostSet) String() string {
	return hs.RangedString()
}

// RangedString returns a string containing a bracketed HostSet representation.
func (hs *HostSet) RangedString() string {
	hs.initList()
	return hs.list.RangedString()
}

// DerangedString returns a string containing the hostnames of
// every host in the HostSet, without any bracketing.
func (hs *HostSet) DerangedString() string {
	hs.initList()
	return hs.list.DerangedString()
}

// Slice returns a string slice containing the hostnames of
// every host in the HostSet.
func (hs *HostSet) Slice() []string {
	hs.initList()
	return hs.list.Slice()
}

// Insert adds a host or list of hosts to the HostSet.
// Returns the number of non-duplicate hosts successfully added.
func (hs *HostSet) Insert(stringHosts string) (int, error) {
	hs.initList()

	newList, err := Create(stringHosts)
	if err != nil {
		return -1, err
	}

	hs.Lock()
	defer hs.Unlock()

	startCount := hs.list.hostCount
	hs.list.PushList(newList)
	hs.list.Uniq()

	return int(hs.list.hostCount - startCount), nil
}

// Delete removes a host or list of hosts from the HostSet.
// Returns the number of hosts successfully removed.
func (hs *HostSet) Delete(stringHosts string) (int, error) {
	hs.initList()

	hs.Lock()
	defer hs.Unlock()

	startCount := hs.list.hostCount
	if _, err := hs.list.Delete(stringHosts); err != nil {
		return -1, err
	}
	hs.list.Uniq()

	return int(hs.list.hostCount - startCount), nil
}

// ReplaceSet replaces this HostSet with the contents
// of the supplied HostSet.
func (hs *HostSet) ReplaceSet(other *HostSet) {
	hs.initList()

	if other == nil {
		return
	}

	hs.Lock()
	defer hs.Unlock()
	other.Lock()
	defer other.Unlock()

	hs.list.ReplaceList(other.list)
}

// MergeSet merges the supplied HostSet into this one.
func (hs *HostSet) MergeSet(other *HostSet) error {
	hs.initList()

	if other == nil {
		return errors.New("nil HostSet")
	}

	hs.Lock()
	defer hs.Unlock()
	other.Lock()
	defer other.Unlock()

	hs.list.PushList(other.list)
	hs.list.Uniq()

	return nil
}

// Within returns true if all hosts in the supplied hosts are contained
// within the HostSet, false otherwise.
func (hs *HostSet) Within(stringHosts string) (bool, error) {
	hs.initList()
	return hs.list.Within(stringHosts)
}

// Intersects returns a *HostSet containing hosts which are in both this
// HostSet and the supplied hosts string.
func (hs *HostSet) Intersects(stringHosts string) (*HostSet, error) {
	hs.initList()
	intersection, err := hs.list.Intersects(stringHosts)
	if err != nil {
		return nil, err
	}
	intersection.Uniq()

	return &HostSet{list: intersection}, nil
}

// Shift returns the string representation of the first host pushed
// onto the HostSet and then removes it from the HostSet. Returns
// an error if the HostSet is empty
func (hs *HostSet) Shift() (string, error) {
	hs.initList()
	return hs.list.Shift()
}

// ShiftRange returns the string representation of the first
// bracketed list of hosts. All hosts in the returned list
// are removed from the HostSet. Returns an error if the HostSet
// is empty.
func (hs *HostSet) ShiftRange() (string, error) {
	hs.initList()
	return hs.list.ShiftRange()
}

// Pop returns the string representation of the last host pushed
// onto the HostSet and then removes it from the HostSet. Returns
// an error if the HostSet is empty
func (hs *HostSet) Pop() (string, error) {
	hs.initList()
	return hs.list.Pop()
}

// PopRange returns the string representation of the last
// bracketed list of hosts. All hosts in the returned list
// are removed from the HostSet. Returns an error if the HostSet
// is empty.
func (hs *HostSet) PopRange() (string, error) {
	hs.initList()
	return hs.list.PopRange()
}

// Count returns the number of hosts in the HostSet.
func (hs *HostSet) Count() int {
	hs.initList()
	return hs.list.Count()
}
