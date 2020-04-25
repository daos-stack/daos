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

package system

import (
	"encoding/json"
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
	// MemberStateUnknown is the default invalid state.
	MemberStateUnknown MemberState = iota
	// MemberStateStarting indicates the member has started but is not
	// ready.
	MemberStateStarting
	// MemberStateReady indicates the member has setup successfully.
	MemberStateReady
	// MemberStateJoined indicates the member has joined the system.
	MemberStateJoined
	// MemberStateStopping indicates prep-shutdown successfully run.
	MemberStateStopping
	// MemberStateStopped indicates process has been stopped.
	MemberStateStopped
	// MemberStateEvicted indicates rank has been evicted from DAOS system.
	MemberStateEvicted
	// MemberStateErrored indicates the process stopped with errors.
	MemberStateErrored
	// MemberStateUnresponsive indicates the process is not responding.
	MemberStateUnresponsive
)

func (ms MemberState) String() string {
	return [...]string{
		"Unknown",
		"Starting",
		"Ready",
		"Joined",
		"Stopping",
		"Stopped",
		"Evicted",
		"Errored",
		"Unresponsive",
	}[ms]
}

// Member refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type Member struct {
	Rank  Rank
	UUID  string
	Addr  net.Addr
	state MemberState
}

// MarshalJSON marshals system.Member to JSON.
func (sm *Member) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON Member
	return json.Marshal(&struct {
		Addr  string
		State int
		*toJSON
	}{
		Addr:   sm.Addr.String(),
		State:  int(sm.state),
		toJSON: (*toJSON)(sm),
	})
}

// UnmarshalJSON unmarshals system.Member from JSON.
func (sm *Member) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON Member
	from := &struct {
		Addr  string
		State int
		*fromJSON
	}{
		fromJSON: (*fromJSON)(sm),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	addr, err := net.ResolveTCPAddr("tcp", from.Addr)
	if err != nil {
		return err
	}
	sm.Addr = addr

	sm.state = MemberState(from.State)

	return nil
}

func (sm *Member) String() string {
	return fmt.Sprintf("%s/%d", sm.Addr, sm.Rank)
}

// State retrieves member state.
func (sm *Member) State() MemberState {
	return sm.state
}

// SetState sets member state.
func (sm *Member) SetState(s MemberState) {
	sm.state = s
}

// NewMember returns a reference to a new member struct.
func NewMember(rank Rank, uuid string, addr net.Addr, state MemberState) *Member {
	return &Member{Rank: rank, UUID: uuid, Addr: addr, state: state}
}

// Members is a type alias for a slice of member references
type Members []*Member

// MemberResult refers to the result of an action on a Member identified
// its string representation "address/rank".
type MemberResult struct {
	Rank    Rank
	Action  string
	Errored bool
	Msg     string
	State   MemberState
}

// MarshalJSON marshals system.MemberResult to JSON.
func (mr *MemberResult) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON MemberResult
	return json.Marshal(&struct {
		State int
		*toJSON
	}{
		State:  int(mr.State),
		toJSON: (*toJSON)(mr),
	})
}

// UnmarshalJSON unmarshals system.MemberResult from JSON.
func (mr *MemberResult) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON MemberResult
	from := &struct {
		State int
		*fromJSON
	}{
		fromJSON: (*fromJSON)(mr),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	mr.State = MemberState(from.State)

	return nil
}

// NewMemberResult returns a reference to a new member result struct.
func NewMemberResult(rank Rank, action string, err error, state MemberState) *MemberResult {
	result := MemberResult{Rank: rank, Action: action, State: state}
	if err != nil {
		result.Errored = true
		result.Msg = err.Error()
	}

	return &result
}

// MemberResults is a type alias for a slice of member result references.
type MemberResults []*MemberResult

// HasErrors returns true if any of the member results errored.
func (smr MemberResults) HasErrors() bool {
	for _, res := range smr {
		if res.Errored {
			return true
		}
	}

	return false
}

// Membership tracks details of system members.
type Membership struct {
	sync.RWMutex
	log     logging.Logger
	members map[Rank]*Member
}

// Add adds member to membership, returns member count.
func (m *Membership) Add(member *Member) (int, error) {
	m.Lock()
	defer m.Unlock()

	if value, found := m.members[member.Rank]; found {
		return -1, errors.Wrapf(FaultMemberExists, "member %s", value)
	}

	m.members[member.Rank] = member

	return len(m.members), nil
}

// SetMemberState updates existing member state in membership.
func (m *Membership) SetMemberState(rank Rank, state MemberState) error {
	m.Lock()
	defer m.Unlock()

	if _, found := m.members[rank]; !found {
		return errors.Wrapf(FaultMemberMissing, "rank %d", rank)
	}

	m.members[rank].SetState(state)

	return nil
}

// AddOrUpdate adds member to membership or updates member state if member
// already exists in membership. Returns flag for whether member was created and
// the previous state if updated.
func (m *Membership) AddOrUpdate(member *Member) (bool, *MemberState) {
	m.Lock()
	defer m.Unlock()

	oldMember, found := m.members[member.Rank]
	if found {
		os := oldMember.State()
		m.members[member.Rank].SetState(member.State())

		return false, &os
	}

	m.members[member.Rank] = member

	return true, nil
}

// Remove removes member from membership, idempotent.
func (m *Membership) Remove(rank Rank) {
	m.Lock()
	defer m.Unlock()

	delete(m.members, rank)
}

// Get retrieves member reference from membership based on Rank.
func (m *Membership) Get(rank Rank) (*Member, error) {
	m.RLock()
	defer m.RUnlock()

	member, found := m.members[rank]
	if !found {
		return nil, errors.Wrapf(FaultMemberMissing, "rank %d", rank)
	}

	return member, nil
}

// Ranks returns slice of ordered member ranks.
func (m *Membership) Ranks() (ranks []Rank) {
	m.RLock()
	defer m.RUnlock()

	for rank := range m.members {
		ranks = append(ranks, rank)
	}

	sort.Slice(ranks, func(i, j int) bool { return ranks[i] < ranks[j] })

	return
}

func mapMemberStates(states ...MemberState) map[MemberState]struct{} {
	stateMap := make(map[MemberState]struct{})
	for _, s := range states {
		stateMap[s] = struct{}{}
	}

	return stateMap
}

// HostRanks returns mapping of control addresses to ranks managed by harness at
// that address.
//
// Filter to include only host keys with any of the provided ranks, if supplied.
func (m *Membership) HostRanks(rankList ...Rank) map[string][]Rank {
	m.RLock()
	defer m.RUnlock()

	hostRanks := make(map[string][]Rank)
	for _, member := range m.members {
		addr := member.Addr.String()

		if len(rankList) != 0 && !member.Rank.InList(rankList) {
			continue
		}

		if _, exists := hostRanks[addr]; exists {
			hostRanks[addr] = append(hostRanks[addr], member.Rank)
			ranks := hostRanks[addr]
			sort.Slice(ranks, func(i, j int) bool { return ranks[i] < ranks[j] })
			continue
		}
		hostRanks[addr] = []Rank{member.Rank}
	}

	return hostRanks
}

// Members returns slice of references to all system members filtering members
// with excluded states and those not in rank list. Results ordered by member rank.
//
// Empty rank list implies no filtering/include all.
func (m *Membership) Members(rankList []Rank, excludedStates ...MemberState) (ms Members) {
	var ranks []Rank

	m.RLock()
	defer m.RUnlock()

	for rank := range m.members {
		if len(rankList) != 0 && !rank.InList(rankList) {
			continue
		}

		ranks = append(ranks, rank)
	}

	sort.Slice(ranks, func(i, j int) bool { return ranks[i] < ranks[j] })

	es := mapMemberStates(excludedStates...)
	for _, r := range ranks {
		m := m.members[r]
		if _, exclude := es[m.State()]; exclude {
			continue
		}
		ms = append(ms, m)
	}

	return ms
}

// UpdateMemberStates updates member's state according to result state.
//
// TODO: store error message in membership
func (m *Membership) UpdateMemberStates(results MemberResults) error {
	m.Lock()
	defer m.Unlock()

	for _, result := range results {
		if _, found := m.members[result.Rank]; !found {
			return errors.Wrapf(FaultMemberMissing, "rank %d", result.Rank)
		}

		m.members[result.Rank].SetState(result.State)
	}

	return nil
}

// NewMembership returns a reference to a new DAOS system membership.
func NewMembership(log logging.Logger) *Membership {
	return &Membership{members: make(map[Rank]*Member), log: log}
}
