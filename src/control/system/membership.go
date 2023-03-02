//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"context"
	"fmt"
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
	Incarnation    uint64
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
	var curMember *Member
	if !req.Rank.Equals(NilRank) {
		curMember, err = m.db.FindMemberByRank(req.Rank)
	} else {
		curMember, err = m.db.FindMemberByUUID(req.UUID)
	}
	if err == nil {
		// Fault domain check only matters if there are other members
		// besides the one being updated.
		if count, err := m.db.MemberCount(); err != nil {
			return nil, err
		} else if count != 1 {
			if err := m.checkReqFaultDomain(req); err != nil {
				return nil, err
			}
		}

		if curMember.state == MemberStateAdminExcluded {
			return nil, errAdminExcluded(curMember.UUID, curMember.Rank)
		}
		// If the member is already in the membership, don't allow rejoining
		// with a different rank, as this may indicate something strange
		// has happened on the node. The only exception is if the rejoin
		// request has a rank of NilRank, which would indicate that
		// the original join response was never received.
		if !curMember.Rank.Equals(req.Rank) && !req.Rank.Equals(NilRank) {
			return nil, errRankChanged(req.Rank, curMember.Rank, curMember.UUID)
		}
		if curMember.UUID != req.UUID {
			return nil, errUuidChanged(req.UUID, curMember.UUID, curMember.Rank)
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
		curMember.Incarnation = req.Incarnation
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

	if err := m.checkReqFaultDomain(req); err != nil {
		return nil, err
	}

	newMember := &Member{
		Rank:           req.Rank,
		Incarnation:    req.Incarnation,
		UUID:           req.UUID,
		Addr:           req.ControlAddr,
		FabricURI:      req.FabricURI,
		FabricContexts: req.FabricContexts,
		FaultDomain:    req.FaultDomain,
		state:          MemberStateJoined,
	}
	if err := m.db.AddMember(newMember); err != nil {
		return nil, errors.Wrap(err, "failed to add new member")
	}
	resp.Created = true
	resp.Member = newMember
	resp.MapVersion, err = m.db.CurMapVersion()
	if err != nil {
		return nil, err
	}

	return resp, nil
}

func (m *Membership) checkReqFaultDomain(req *JoinRequest) error {
	currentDepth := m.db.FaultDomainTree().Depth()
	newDepth := req.FaultDomain.NumLevels()
	// currentDepth includes the rank layer, which is not included in the req
	if currentDepth > 0 && newDepth != currentDepth-1 {
		return FaultBadFaultDomainDepth(req.FaultDomain, currentDepth-1)
	}
	return nil
}

// AddOrReplace adds member to membership or replaces member if it exists.
//
// Note: this method updates state without checking if state transition is
// legal so use with caution.
func (m *Membership) AddOrReplace(newMember *Member) error {
	m.Lock()
	defer m.Unlock()

	if err := m.addMember(newMember); err == nil {
		return nil
	}

	return m.db.UpdateMember(newMember)
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

func msgBadStateTransition(m *Member, ts MemberState) string {
	return fmt.Sprintf("illegal member state update for rank %d: %s->%s", m.Rank, m.state, ts)
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

		// don't update members if any of the following is true:
		// - result reports an error and state != errored
		// - result reports an error and updateOnFail is false
		// - if transition from current to result state is illegal

		if result.Errored {
			// Check state matches errored flag.
			if result.State != MemberStateErrored && result.State != MemberStateUnresponsive {
				// result content mismatch (programming error)
				return errors.Errorf(
					"errored result for rank %d has conflicting state '%s'",
					result.Rank, result.State)
			}
			if !updateOnFail {
				continue
			}
		}

		if member.State().isTransitionIllegal(result.State) {
			m.log.Debugf("skipping %s", msgBadStateTransition(member, result.State))
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

// MarkRankDead is a helper method to mark a rank as dead in response to a
// swim_rank_dead event.
func (m *Membership) MarkRankDead(rank Rank, incarnation uint64) error {
	m.Lock()
	defer m.Unlock()

	member, err := m.db.FindMemberByRank(rank)
	if err != nil {
		return err
	}

	ns := MemberStateExcluded
	if member.State().isTransitionIllegal(ns) {
		msg := msgBadStateTransition(member, ns)
		// excluded->excluded transitions expected for multiple swim
		// notifications, if so return error to skip group update
		if member.State() != ns {
			m.log.Error(msg)
		}

		return errors.New(msg)
	}

	if member.State() == MemberStateJoined && member.Incarnation > incarnation {
		m.log.Debugf("ignoring rank dead event for previous incarnation of %d (%x < %x)", rank, incarnation, member.Incarnation)
		return errors.Errorf("event is for previous incarnation of %d", rank)
	}

	m.log.Infof("marking rank %d as %s in response to rank dead event", rank, ns)
	member.state = ns
	return m.db.UpdateMember(member)
}

func (m *Membership) handleEngineFailure(evt *events.RASEvent) {
	m.Lock()
	defer m.Unlock()

	ei := evt.GetEngineStateInfo()
	if ei == nil {
		m.log.Error("no extended info in EngineDied event received")
		return
	}

	member, err := m.db.FindMemberByRank(Rank(evt.Rank))
	if err != nil {
		m.log.Errorf("member with rank %d not found", evt.Rank)
		return
	}

	// TODO DAOS-7261: sanity check that the correct member is being
	//                 updated by performing lookup on provided hostname
	//                 and matching returned addresses with the address
	//                 of the member with the matching rank.
	//
	// e.g. if member.Addr.IP.Equal(net.ResolveIPAddr(evt.Hostname))

	ns := MemberStateErrored
	if member.State().isTransitionIllegal(ns) {
		m.log.Debugf("skipping %s", msgBadStateTransition(member, ns))
		return
	}

	member.state = ns
	member.Info = evt.Msg

	if err := m.db.UpdateMember(member); err != nil {
		m.log.Errorf("updating member with rank %d: %s", member.Rank, err)
	}
}

// OnEvent handles events on channel and updates member states accordingly.
func (m *Membership) OnEvent(_ context.Context, evt *events.RASEvent) {
	switch evt.ID {
	case events.RASEngineDied:
		m.handleEngineFailure(evt)
	}
}

// CompressedFaultDomainTree returns the tree of fault domains of joined
// members in a compressed format.
// Each domain is represented as a tuple: (level, ID, number of children)
// Except for the rank, which is represented as: (rank)
// The order of items is a breadth-first traversal of the tree.
func (m *Membership) CompressedFaultDomainTree(ranks ...uint32) ([]uint32, error) {
	tree := m.db.FaultDomainTree()
	if tree == nil {
		return nil, errors.New("uninitialized fault domain tree")
	}

	subtree, err := getFaultDomainSubtree(tree, ranks...)
	if err != nil {
		return nil, err
	}

	return compressTree(subtree), nil
}

func getFaultDomainSubtree(tree *FaultDomainTree, ranks ...uint32) (*FaultDomainTree, error) {
	if len(ranks) == 0 {
		return tree, nil
	}

	domains := tree.Domains()

	// Traverse the list of domains only once
	treeDomains := make(map[uint32]*FaultDomain, len(ranks))
	for _, d := range domains {
		if r, isRank := getFaultDomainRank(d); isRank {
			treeDomains[r] = d
		}
	}

	rankDomains := make([]*FaultDomain, 0)
	for _, r := range ranks {
		d, ok := treeDomains[r]
		if !ok {
			return nil, fmt.Errorf("rank %d not found in fault domain tree", r)
		}
		rankDomains = append(rankDomains, d)
	}

	return tree.Subtree(rankDomains...)
}

func getFaultDomainRank(fd *FaultDomain) (uint32, bool) {
	fmtStr := rankFaultDomainPrefix + "%d"
	var rank uint32
	n, err := fmt.Sscanf(fd.BottomLevel(), fmtStr, &rank)
	if err != nil || n != 1 {
		return 0, false
	}
	return rank, true
}

func compressTree(tree *FaultDomainTree) []uint32 {
	result := []uint32{}
	queue := make([]*FaultDomainTree, 0)
	queue = append(queue, tree)

	numLevel := 1
	numNextLevel := 0
	seenThisLevel := 0
	level := tree.Depth()

	for len(queue) > 0 {
		if seenThisLevel == numLevel {
			numLevel = numNextLevel
			seenThisLevel = 0
			numNextLevel = 0
			level--
			if level < 0 {
				panic("dev error: decremented levels below 0")
			}
		}
		cur := queue[0]
		queue = queue[1:]
		seenThisLevel++

		if rank, ok := getFaultDomainRank(cur.Domain); ok && cur.IsLeaf() {
			result = append(result, rank)
			continue
		}

		result = append(result,
			uint32(level),
			cur.ID,
			uint32(len(cur.Children)))
		for _, child := range cur.Children {
			queue = append(queue, child)
			numNextLevel++
		}
	}
	return result
}
