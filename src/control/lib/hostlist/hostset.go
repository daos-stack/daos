//
// (C) Copyright 2019 Intel Corporation.
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

import "sync"

type (
	// HostSet is a special case of HostList which never contains duplicates
	// and is always sorted alphanumerically based on prefix.
	HostSet struct {
		sync.Mutex
		list *HostList
	}
)

// CreateSet creates a new HostSet from the supplied string representation.
func CreateSet(stringHosts string) (*HostSet, error) {
	hl, err := Create(stringHosts)
	if err != nil {
		return nil, err
	}
	hl.Uniq()

	return &HostSet{list: hl}, nil
}

func (hs *HostSet) String() string {
	return hs.RangedString()
}

// RangedString returns a string containing a bracketed HostSet representation.
func (hs *HostSet) RangedString() string {
	return hs.list.RangedString()
}

// DerangedString returns a string containing the hostnames of
// every host in the HostSet, without any bracketing.
func (hs *HostSet) DerangedString() string {
	return hs.list.DerangedString()
}

// Insert adds a host or list of hosts to the HostSet.
// Returns the number of non-duplicate hosts successfully added.
func (hs *HostSet) Insert(stringHosts string) (int, error) {
	newList, err := Create(stringHosts)
	if err != nil {
		return -1, err
	}

	hs.Lock()
	defer hs.Unlock()

	startCount := hs.list.hostCount
	if err := hs.list.PushList(newList); err != nil {
		return -1, err
	}
	hs.list.Uniq()

	return int(hs.list.hostCount - startCount), nil
}

// Delete removes a host or list of hosts from the HostSet.
// Returns the number of hosts successfully removed.
func (hs *HostSet) Delete(stringHosts string) (int, error) {
	hs.Lock()
	defer hs.Unlock()

	startCount := hs.list.hostCount
	if _, err := hs.list.Delete(stringHosts); err != nil {
		return -1, err
	}
	hs.list.Uniq()

	return int(hs.list.hostCount - startCount), nil
}

// Within returns true if all hosts in the supplied hosts are contained
// within the HostSet, false otherwise.
func (hs *HostSet) Within(stringHosts string) (bool, error) {
	return hs.list.Within(stringHosts)
}

// Intersects returns a *HostSet containing hosts which are in both this
// HostSet and the supplied hosts string.
func (hs *HostSet) Intersects(stringHosts string) (*HostSet, error) {
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
	return hs.list.Shift()
}

// ShiftRange returns the string representation of the first
// bracketed list of hosts. All hosts in the returned list
// are removed from the HostSet. Returns an error if the HostSet
// is empty.
func (hs *HostSet) ShiftRange() (string, error) {
	return hs.list.ShiftRange()
}

// Pop returns the string representation of the last host pushed
// onto the HostSet and then removes it from the HostSet. Returns
// an error if the HostSet is empty
func (hs *HostSet) Pop() (string, error) {
	return hs.list.Pop()
}

// PopRange returns the string representation of the last
// bracketed list of hosts. All hosts in the returned list
// are removed from the HostSet. Returns an error if the HostSet
// is empty.
func (hs *HostSet) PopRange() (string, error) {
	return hs.list.PopRange()
}

// Count returns the number of hosts in the HostSet.
func (hs *HostSet) Count() int {
	return hs.list.Count()
}
