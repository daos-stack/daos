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
	"strconv"
	"strings"
	"sync"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
)

// MemberState represents the activity state of DAOS system members.
type MemberState int

const (
	// MemberStateUnknown is the default invalid state.
	MemberStateUnknown MemberState = 0x0000
	// MemberStateAwaitFormat indicates the member is waiting for format.
	MemberStateAwaitFormat MemberState = 0x0001
	// MemberStateStarting indicates the member has started but is not
	// ready.
	MemberStateStarting MemberState = 0x0002
	// MemberStateReady indicates the member has setup successfully.
	MemberStateReady MemberState = 0x0004
	// MemberStateJoined indicates the member has joined the system.
	MemberStateJoined MemberState = 0x0008
	// MemberStateStopping indicates prep-shutdown successfully run.
	MemberStateStopping MemberState = 0x0010
	// MemberStateStopped indicates process has been stopped.
	MemberStateStopped MemberState = 0x0020
	// MemberStateEvicted indicates rank has been evicted from DAOS system.
	MemberStateEvicted MemberState = 0x0040
	// MemberStateErrored indicates the process stopped with errors.
	MemberStateErrored MemberState = 0x0080
	// MemberStateUnresponsive indicates the process is not responding.
	MemberStateUnresponsive MemberState = 0x0100

	// AvailableMemberFilter defines the state(s) to be used when determining
	// whether or not a member is available for the purposes of pool creation, etc.
	AvailableMemberFilter = MemberStateReady | MemberStateJoined
	// AllMemberFilter will match all valid member states.
	AllMemberFilter = MemberState(0xFFFF)
)

func (ms MemberState) String() string {
	switch ms {
	case MemberStateAwaitFormat:
		return "AwaitFormat"
	case MemberStateStarting:
		return "Starting"
	case MemberStateReady:
		return "Ready"
	case MemberStateJoined:
		return "Joined"
	case MemberStateStopping:
		return "Stopping"
	case MemberStateStopped:
		return "Stopped"
	case MemberStateEvicted:
		return "Evicted"
	case MemberStateErrored:
		return "Errored"
	case MemberStateUnresponsive:
		return "Unresponsive"
	default:
		return "Unknown"
	}
}

// isTransitionIllegal indicates if given state transitions is legal.
//
// Map state combinations to true (illegal) or false (legal) and return negated
// value.
func (ms MemberState) isTransitionIllegal(to MemberState) bool {
	if ms == MemberStateUnknown {
		return true // no legal transitions
	}
	if ms == to {
		return true // identical state
	}
	return map[MemberState]map[MemberState]bool{
		MemberStateAwaitFormat: {
			MemberStateEvicted: true,
		},
		MemberStateStarting: {
			MemberStateEvicted: true,
		},
		MemberStateReady: {
			MemberStateEvicted: true,
		},
		MemberStateJoined: {
			MemberStateReady: true,
		},
		MemberStateStopping: {
			MemberStateReady: true,
		},
		MemberStateEvicted: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
		},
		MemberStateErrored: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
		},
		MemberStateUnresponsive: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
		},
	}[ms][to]
}

// Member refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type Member struct {
	Rank           Rank
	UUID           uuid.UUID
	Addr           *net.TCPAddr
	FabricURI      string
	FabricContexts uint32
	state          MemberState
	Info           string
	FaultDomain    *FaultDomain
}

// MarshalJSON marshals system.Member to JSON.
func (sm *Member) MarshalJSON() ([]byte, error) {
	if sm == nil {
		return nil, errors.New("tried to marshal nil Member")
	}

	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON Member
	return json.Marshal(&struct {
		Addr        string
		State       int
		FaultDomain string
		*toJSON
	}{
		Addr:        sm.Addr.String(),
		State:       int(sm.state),
		FaultDomain: sm.FaultDomain.String(),
		toJSON:      (*toJSON)(sm),
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
		Addr        string
		State       int
		FaultDomain string
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

	fd, err := NewFaultDomainFromString(from.FaultDomain)
	if err != nil {
		return err
	}
	sm.FaultDomain = fd

	return nil
}

func (sm *Member) String() string {
	return fmt.Sprintf("%s/%d/%s", sm.Addr, sm.Rank, sm.State())
}

// State retrieves member state.
func (sm *Member) State() MemberState {
	return sm.state
}

// WithInfo adds info field and returns updated member.
func (sm *Member) WithInfo(msg string) *Member {
	sm.Info = msg
	return sm
}

// WithFaultDomain adds the fault domain field and returns the updated member.
func (sm *Member) WithFaultDomain(fd *FaultDomain) *Member {
	sm.FaultDomain = fd
	return sm
}

// RankFaultDomain generates the fault domain representing this Member rank.
func (sm *Member) RankFaultDomain() *FaultDomain {
	rankDomain := fmt.Sprintf("rank%d", uint32(sm.Rank))
	return sm.FaultDomain.MustCreateChild(rankDomain) // "rankX" string can't fail
}

// NewMember returns a reference to a new member struct.
func NewMember(rank Rank, uuidStr, uri string, addr *net.TCPAddr, state MemberState) *Member {
	// FIXME: Either require a valid uuid.UUID to be supplied
	// or else change the return signature to include an error
	newUUID := uuid.MustParse(uuidStr)
	return &Member{Rank: rank, UUID: newUUID, FabricURI: uri, Addr: addr,
		state: state, FaultDomain: MustCreateFaultDomain()}
}

// Members is a type alias for a slice of member references
type Members []*Member

// MemberResult refers to the result of an action on a Member.
type MemberResult struct {
	Addr    string
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
//
// Host address and action fields are not always used so not populated here.
func NewMemberResult(rank Rank, err error, state MemberState) *MemberResult {
	result := MemberResult{Rank: rank, State: state}
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
	log logging.Logger
	db  *Database
}

func (m *Membership) addMember(member *Member) error {
	m.log.Debugf("adding system member: %s", member)

	return m.db.AddMember(member)
}

func (m *Membership) updateMember(member *Member) error {
	m.log.Debugf("updating system member: %s", member)

	return m.db.UpdateMember(member)
}

// Add adds member to membership, returns member count.
func (m *Membership) Add(member *Member) (int, error) {
	m.Lock()
	defer m.Unlock()

	if err := m.addMember(member); err != nil {
		return -1, err
	}

	count, err := m.db.MemberCount()
	if err != nil {
		return -1, err
	}

	return count, nil
}

// Count returns the number of members.
func (m *Membership) Count() (int, error) {
	return m.db.MemberCount()
}

type JoinRequest struct {
	Rank           Rank
	UUID           uuid.UUID
	ControlAddr    *net.TCPAddr
	FabricURI      string
	FabricContexts uint32
	FaultDomain    *FaultDomain
}

type JoinResponse struct {
	Member     *Member
	Created    bool
	PrevState  MemberState
	MapVersion uint32
}

// Join creates or updates an entry in the membership for the given
// JoinRequest.
func (m *Membership) Join(req *JoinRequest) (resp *JoinResponse, err error) {
	m.Lock()
	defer m.Unlock()

	resp = new(JoinResponse)
	curMember, err := m.db.FindMemberByUUID(req.UUID)
	if err == nil {
		if !curMember.Rank.Equals(req.Rank) {
			return nil, errors.Errorf("re-joining server %s has different rank (%d != %d)",
				req.UUID, req.Rank, curMember.Rank)
		}

		if !curMember.FaultDomain.Equals(req.FaultDomain) {
			m.log.Infof("fault domain for rank %d changed from %q to %q",
				curMember.Rank,
				curMember.FaultDomain.String(),
				req.FaultDomain.String())
		}

		resp.PrevState = curMember.state
		curMember.state = MemberStateJoined
		curMember.Addr = req.ControlAddr
		curMember.FabricURI = req.FabricURI
		curMember.FabricContexts = req.FabricContexts
		curMember.FaultDomain = req.FaultDomain
		if err := m.db.UpdateMember(curMember); err != nil {
			return nil, err
		}
		resp.Member = curMember

		resp.MapVersion, err = m.db.CurMapVersion()
		if err != nil {
			return nil, err
		}

		return resp, err
	}

	if !IsMemberNotFound(err) {
		return nil, err
	}

	newMember := &Member{
		Rank:           req.Rank,
		UUID:           req.UUID,
		Addr:           req.ControlAddr,
		FabricURI:      req.FabricURI,
		FabricContexts: req.FabricContexts,
		FaultDomain:    req.FaultDomain,
		state:          MemberStateJoined,
	}
	if err := m.db.AddMember(newMember); err != nil {
		return nil, err
	}
	resp.Created = true
	resp.Member = newMember
	resp.MapVersion, err = m.db.CurMapVersion()
	if err != nil {
		return nil, err
	}

	return resp, nil
}

// AddOrReplace adds member to membership or replaces member if it exists.
//
// Note: this method updates state without checking if state transition is
//       legal so use with caution.
func (m *Membership) AddOrReplace(newMember *Member) error {
	m.Lock()
	defer m.Unlock()

	if err := m.addMember(newMember); err == nil {
		return nil
	}

	return m.updateMember(newMember)
}

// Remove removes member from membership, idempotent.
func (m *Membership) Remove(rank Rank) {
	m.Lock()
	defer m.Unlock()

	member, err := m.db.FindMemberByRank(rank)
	if err != nil {
		m.log.Debugf("remove %d failed: %s", rank, err)
		return
	}
	if err := m.db.RemoveMember(member); err != nil {
		m.log.Debugf("remove %d failed: %s", rank, err)
	}
}

// Get retrieves member reference from membership based on Rank.
func (m *Membership) Get(rank Rank) (*Member, error) {
	m.RLock()
	defer m.RUnlock()

	return m.db.FindMemberByRank(rank)
}

// RankList returns slice of all ordered member ranks.
func (m *Membership) RankList() ([]Rank, error) {
	return m.db.MemberRanks()
}

func (m *Membership) getHostRanks(rankSet *RankSet) map[string][]Rank {
	var rankList []Rank
	hostRanks := make(map[string][]Rank)

	if rankSet != nil {
		rankList = rankSet.Ranks()
	}

	members, err := m.db.AllMembers()
	if err != nil {
		m.log.Errorf("failed to get all members: %s", err)
		return nil
	}

	for _, member := range members {
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

// HostRanks returns mapping of control addresses to ranks managed by harness at
// that address.
//
// Filter to include only host keys with any of the provided ranks, if supplied.
func (m *Membership) HostRanks(rankSet *RankSet) map[string][]Rank {
	m.RLock()
	defer m.RUnlock()

	return m.getHostRanks(rankSet)
}

// HostList returns slice of control addresses that contain any of the ranks
// in the input rank list.
//
// If input rank list is empty, return all hosts in membership and ignore ranks
// that are not in the membership.
func (m *Membership) HostList(rankSet *RankSet) []string {
	m.RLock()
	defer m.RUnlock()

	hostRanks := m.getHostRanks(rankSet)
	hosts := make([]string, 0, len(hostRanks))

	for host := range hostRanks {
		hosts = append(hosts, host)
	}
	sort.Strings(hosts)

	return hosts
}

// Members returns slice of references to all system members ordered by rank.
//
// Empty rank list implies no filtering/include all and ignore ranks that are
// not in the membership.
func (m *Membership) Members(rankSet *RankSet) (members Members) {
	m.RLock()
	defer m.RUnlock()

	if rankSet == nil || rankSet.Count() == 0 {
		var err error
		members, err = m.db.AllMembers()
		if err != nil {
			m.log.Errorf("failed to get all members: %s", err)
			return nil
		}
	} else {
		for _, rank := range rankSet.Ranks() {
			if member, err := m.db.FindMemberByRank(rank); err == nil {
				members = append(members, member)
			}
		}
	}
	sort.Slice(members, func(i, j int) bool { return members[i].Rank < members[j].Rank })

	return
}

// UpdateMemberStates updates member's state according to result state.
//
// If updateOnFail is false, only update member state and info if result is a
// success, if true then update state even if result is errored.
func (m *Membership) UpdateMemberStates(results MemberResults, updateOnFail bool) error {
	m.Lock()
	defer m.Unlock()

	for _, result := range results {
		member, err := m.db.FindMemberByRank(result.Rank)
		if err != nil {
			return err
		}

		// use opportunity to update host address in result
		if result.Addr == "" {
			result.Addr = member.Addr.String()
		}

		// don't update members if:
		// - result reports an error and updateOnFail is false or
		// - if transition from current to result state is illegal
		if result.Errored {
			if !updateOnFail {
				continue
			}
			if result.State != MemberStateErrored {
				return errors.Errorf(
					"errored result for rank %d has conflicting state '%s'",
					result.Rank, result.State)
			}
		}
		if member.State().isTransitionIllegal(result.State) {
			continue
		}
		member.state = result.State
		member.Info = result.Msg

		if err := m.db.UpdateMember(member); err != nil {
			return err
		}
	}

	return nil
}

// CheckRanks returns rank sets of existing and missing membership ranks from
// provided rank set string, if empty string is given then return hit rank set
// containing all ranks in the membership.
func (m *Membership) CheckRanks(ranks string) (hit, miss *RankSet, err error) {
	m.RLock()
	defer m.RUnlock()

	var allRanks, toTest []Rank
	allRanks, err = m.db.MemberRanks()
	if err != nil {
		return
	}
	toTest, err = ParseRanks(ranks)
	if err != nil {
		return
	}

	if ranks == "" {
		return RankSetFromRanks(allRanks), RankSetFromRanks(nil), nil
	}

	missing := CheckRankMembership(allRanks, toTest)
	miss = RankSetFromRanks(missing)
	hit = RankSetFromRanks(CheckRankMembership(missing, toTest))

	return
}

type resolveFnSig func(string, string) (*net.TCPAddr, error)

// CheckHosts returns set of all ranks on any of the hosts in provided host set
// string and another slice of all hosts from input hostset string that are
// missing from the membership.
func (m *Membership) CheckHosts(hosts string, ctlPort int, resolveFn resolveFnSig) (*RankSet, *hostlist.HostSet, error) {
	m.RLock()
	defer m.RUnlock()

	hostRanks := m.getHostRanks(nil)
	rs, err := CreateRankSet("")
	if err != nil {
		return nil, nil, err
	}

	hs, err := hostlist.CreateSet(hosts)
	if err != nil {
		return nil, nil, err
	}
	missHS, err := hostlist.CreateSet("")
	if err != nil {
		return nil, nil, err
	}
	for _, host := range strings.Split(hs.DerangedString(), ",") {
		origHostString := host
		if !common.HasPort(host) {
			host = net.JoinHostPort(host, strconv.Itoa(ctlPort))
		}

		tcpAddr, resolveErr := resolveFn("tcp", host)
		if resolveErr != nil {
			m.log.Debugf("host addr %q didn't resolve: %s", host, resolveErr)
			if _, err := missHS.Insert(origHostString); err != nil {
				return nil, nil, err
			}
			continue
		}

		if rankList, exists := hostRanks[tcpAddr.String()]; exists {
			m.log.Debugf("CheckHosts(): %v ranks found at %s", rankList, origHostString)
			for _, rank := range rankList {
				rs.Add(rank)
			}
			continue
		}

		if _, err := missHS.Insert(origHostString); err != nil {
			return nil, nil, err
		}
	}

	return rs, missHS, nil
}

// NewMembership returns a reference to a new DAOS system membership.
func NewMembership(log logging.Logger, db *Database) *Membership {
	return &Membership{db: db, log: log}
}
