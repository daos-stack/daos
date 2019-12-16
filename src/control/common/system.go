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

package common

import (
	"fmt"
	"net"
	"sort"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// MemberState represents the activity state of DAOS system members.
//go:generate stringer -type=MemberState
type MemberState int

const (
	MemberStateStarted MemberState = iota
	MemberStateStopping
	MemberStateErrored
	MemberStateUnresponsive
)

// SystemMember refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type SystemMember struct {
	Rank  uint32
	UUID  string
	Addr  net.Addr
	State MemberState
}

func (sm SystemMember) String() string {
	return fmt.Sprintf("%s/%s/%d", sm.Addr, sm.UUID, sm.Rank)
}

func NewSystemMember(rank uint32, uuid string, addr net.Addr) *SystemMember {
	return &SystemMember{Rank: rank, UUID: uuid, Addr: addr}
}

type SystemMembers []*SystemMember

// SystemMemberResult refers to the result of an action on a SystemMember
// identified by string representation "address/uuid/rank".
type SystemMemberResult struct {
	ID     string
	Action string
	Err    error
}

func NewSystemMemberResult(memberID, action string) *SystemMemberResult {
	return &SystemMemberResult{ID: memberID, Action: action}
}

type SystemMemberResults []*SystemMemberResult

func (smr SystemMemberResults) HasErrors() bool {
	for _, res := range smr {
		if res.Err != nil {
			return true
		}
	}
	return false
}

// Membership tracks details of system members.
type Membership struct {
	sync.RWMutex
	log     logging.Logger
	members map[uint32]*SystemMember
}

// Add adds member to membership, returns member count.
func (m *Membership) Add(member *SystemMember) (int, error) {
	m.Lock()
	defer m.Unlock()

	if value, found := m.members[member.Rank]; found {
		return -1, errors.Errorf("member %s already exists", value)
	}

	m.members[member.Rank] = member

	return len(m.members), nil
}

// Update updates existing member in membership, returns error.
func (m *Membership) Update(member *SystemMember) error {
	m.Lock()
	defer m.Unlock()

	if _, found := m.members[member.Rank]; !found {
		return errors.Errorf("member with rank %d not found", member.Rank)
	}

	m.members[member.Rank] = member

	return nil
}

// Remove removes member from membership, idempotent.
func (m *Membership) Remove(rank uint32) {
	m.Lock()
	defer m.Unlock()

	delete(m.members, rank)
}

// Get retrieves member from membership based on UUID.
func (m *Membership) Get(rank uint32) (member *SystemMember, err error) {
	m.RLock()
	defer m.RUnlock()

	member, found := m.members[rank]
	if !found {
		return nil, errors.Errorf("member with rank %d not found", rank)
	}

	return
}

// Ranks returns ordered member ranks.
func (m *Membership) Ranks() (ranks []uint32) {
	m.RLock()
	defer m.RUnlock()

	for rank, _ := range m.members {
		ranks = append(ranks, rank)
	}

	sort.Slice(ranks, func(i, j int) bool { return ranks[i] < ranks[j] })

	return
}

// Members returns all system members.
func (m *Membership) Members() (ms SystemMembers) {
	m.RLock()
	defer m.RUnlock()

	for _, r := range m.Ranks() {
		m, err := m.Get(r)
		if err != nil {
			panic(err) // should never happen
		}
		ms = append(ms, m)
	}

	return ms
}

func NewMembership(log logging.Logger) *Membership {
	return &Membership{members: make(map[uint32]*SystemMember), log: log}
}
