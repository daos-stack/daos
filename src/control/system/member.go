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

package system

import (
	"fmt"
	"net"
	"sort"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// MemberState represents the activity state of DAOS system members.
type MemberState int

const (
	MemberStateUnknown MemberState = iota
	MemberStateStarted
	MemberStateStopping
	MemberStateErrored
	MemberStateUnresponsive
)

func (ms MemberState) String() string {
	return [...]string{"Unknown", "Started", "Stopping", "Errored", "Unresponsive"}[ms]
}

// Member refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type Member struct {
	Rank  uint32
	UUID  string
	Addr  net.Addr
	state MemberState
}

func (sm *Member) String() string {
	return fmt.Sprintf("%s/%d", sm.Addr, sm.Rank)
}

func (sm *Member) State() MemberState {
	return sm.state
}

func (sm *Member) SetState(s MemberState) {
	sm.state = s
}

func NewMember(rank uint32, uuid string, addr net.Addr, state MemberState) *Member {
	return &Member{Rank: rank, UUID: uuid, Addr: addr, state: state}
}

type Members []*Member

// MemberResult refers to the result of an action on a Member
// identified by string representation "address/uuid/rank".
type MemberResult struct {
	ID     string
	Action string
	Err    error
}

func NewMemberResult(memberID, action string, err error) *MemberResult {
	return &MemberResult{ID: memberID, Action: action, Err: err}
}

type MemberResults []*MemberResult

func (smr MemberResults) HasErrors() bool {
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
	members map[uint32]*Member
}

// Add adds member to membership, returns member count.
func (m *Membership) Add(member *Member) (int, error) {
	m.Lock()
	defer m.Unlock()

	if value, found := m.members[member.Rank]; found {
		return -1, errors.Errorf("member %s already exists", value)
	}

	m.members[member.Rank] = member

	return len(m.members), nil
}

// SetMemberState updates existing member state in membership, returns error.
func (m *Membership) SetMemberState(rank uint32, state MemberState) error {
	m.Lock()
	defer m.Unlock()

	if _, found := m.members[rank]; !found {
		return errors.Errorf("member with rank %d not found", rank)
	}

	m.members[rank].SetState(state)

	return nil
}

// Remove removes member from membership, idempotent.
func (m *Membership) Remove(rank uint32) {
	m.Lock()
	defer m.Unlock()

	delete(m.members, rank)
}

// Get retrieves member from membership based on UUID.
func (m *Membership) Get(rank uint32) (*Member, error) {
	m.RLock()
	defer m.RUnlock()

	member, found := m.members[rank]
	if !found {
		return nil, errors.Errorf("member with rank %d not found", rank)
	}

	return member, nil
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
func (m *Membership) Members() (ms Members) {
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
	return &Membership{members: make(map[uint32]*Member), log: log}
}
