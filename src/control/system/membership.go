//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"context"
	"net"
	"sort"
	"strconv"
	"strings"
	"sync"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
)

// resolveTCPFn is a type alias for the net.ResolveTCPAddr function signature.
type resolveTCPFn func(string, string) (*net.TCPAddr, error)

// Membership tracks details of system members.
type Membership struct {
	sync.RWMutex
	log        logging.Logger
	db         *Database
	resolveTCP resolveTCPFn
}

// NewMembership returns a reference to a new DAOS system membership.
func NewMembership(log logging.Logger, db *Database) *Membership {
	return &Membership{
		db:         db,
		log:        log,
		resolveTCP: net.ResolveTCPAddr,
	}
}

// WithTCPResolver adds a resolveTCPFn to the membership structure.
func (m *Membership) WithTCPResolver(resolver resolveTCPFn) *Membership {
	m.resolveTCP = resolver

	return m
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

// JoinRequest contains information needed for join membership update.
type JoinRequest struct {
	Rank           Rank
	UUID           uuid.UUID
	ControlAddr    *net.TCPAddr
	FabricURI      string
	FabricContexts uint32
	FaultDomain    *FaultDomain
}

// JoinResponse contains information returned from join membership update.
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
		curMember.Info = ""
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

// CheckHosts returns set of all ranks on any of the hosts in provided host set
// string and another slice of all hosts from input hostset string that are
// missing from the membership. Host addresses are resolved before looking up
// resident ranks to verify destination server is still available.
func (m *Membership) CheckHosts(hosts string, ctlPort int) (*RankSet, *hostlist.HostSet, error) {
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

		tcpAddr, resolveErr := m.resolveTCP("tcp", host)
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

func (m *Membership) handleRankDown(evt *events.RASEvent) {
	ei := evt.GetRankStateInfo()
	if ei == nil {
		m.log.Error("no extended info in RankDown event received")
		return
	}
	m.log.Debugf("processing RAS event %q from rank %d on host %q",
		evt.Msg, evt.Rank, evt.Hostname)

	// TODO: sanity check that the correct member is being updated by
	// performing lookup on provided hostname and matching returned
	// addresses with the member address with matching rank.

	mr := NewMemberResult(Rank(evt.Rank), errors.Wrap(ei.ExitErr, evt.Msg), MemberStateErrored)

	if err := m.UpdateMemberStates(MemberResults{mr}, true); err != nil {
		m.log.Errorf("updating member states: %s", err)
		return
	}

	member, err := m.Get(Rank(evt.Rank))
	if err != nil {
		m.log.Errorf("member with rank %d not found", evt.Rank)
		return
	}
	m.log.Debugf("update rank %d to %+v (%s)", evt.Rank, member, member.Info)
}

// OnEvent handles events on channel and updates member states accordingly.
func (m *Membership) OnEvent(_ context.Context, evt *events.RASEvent) {
	switch evt.ID {
	case events.RASRankDown:
		m.handleRankDown(evt)
	}
}
