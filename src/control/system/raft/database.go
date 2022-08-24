//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"context"
	"net"
	"os"
	"sort"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/hashicorp/raft"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// CurrentSchemaVersion indicates the current db schema version.
	CurrentSchemaVersion = 0
)

type (
	onLeadershipGainedFn func(context.Context) error
	onLeadershipLostFn   func() error
	onRaftShutdownFn     func() error

	raftService interface {
		Apply([]byte, time.Duration) raft.ApplyFuture
		AddVoter(raft.ServerID, raft.ServerAddress, uint64, time.Duration) raft.IndexFuture
		RemoveServer(raft.ServerID, uint64, time.Duration) raft.IndexFuture
		BootstrapCluster(raft.Configuration) raft.Future
		Leader() raft.ServerAddress
		LeaderCh() <-chan bool
		LeadershipTransfer() raft.Future
		Shutdown() raft.Future
		State() raft.RaftState
	}

	// syncRaft provides a wrapper for synchronized access to the
	// stored raft implementation.
	syncRaft struct {
		sync.RWMutex
		svc raftService
	}

	// dbData is the raft-replicated system database. It
	// should never be updated directly; updates must be
	// applied in order to ensure that they are sent to
	// all participating replicas.
	dbData struct {
		sync.RWMutex
		log logging.Logger

		Version       uint64
		NextRank      system.Rank
		MapVersion    uint32
		Members       *MemberDatabase
		Pools         *PoolDatabase
		System        *SystemDatabase
		SchemaVersion uint
	}

	// syncTCPAddr protects a TCP address with a mutex to allow
	// for atomic reads and writes.
	syncTCPAddr struct {
		sync.RWMutex
		Addr *net.TCPAddr
	}

	// Database provides high-level access methods for the
	// system data as well as structure for managing the raft
	// service that replicates the system data.
	Database struct {
		sync.Mutex
		log                logging.Logger
		cfg                *DatabaseConfig
		initialized        atm.Bool
		replicaAddr        *syncTCPAddr
		raft               syncRaft
		raftTransport      raft.Transport
		raftLeaderNotifyCh chan bool
		onLeadershipGained []onLeadershipGainedFn
		onLeadershipLost   []onLeadershipLostFn
		onRaftShutdown     []onRaftShutdownFn
		shutdownCb         context.CancelFunc
		shutdownErrCh      chan error

		data *dbData
	}

	// DatabaseConfig defines the configuration for the system database.
	DatabaseConfig struct {
		Replicas   []*net.TCPAddr
		RaftDir    string
		SystemName string
	}

	// GroupMap represents a version of the system membership map.
	GroupMap struct {
		Version     uint32
		RankEntries map[system.Rank]RankEntry
		MSRanks     []system.Rank
	}

	// RankEntry comprises the information about a rank in GroupMap.
	RankEntry struct {
		URI         string
		Incarnation uint64
	}
)

// setSvc safely sets the raft service implementation under a lock
func (sr *syncRaft) setSvc(svc raftService) {
	sr.Lock()
	defer sr.Unlock()
	sr.svc = svc
}

// getSvc returns the raft service implementation with a closure
// to unlock it, or an error
func (sr *syncRaft) getSvc() (raftService, func(), error) {
	sr.RLock()

	if sr.svc == nil {
		sr.RUnlock()
		return nil, func() {}, system.ErrRaftUnavail
	}

	return sr.svc, sr.RUnlock, nil
}

// withReadLock executes the supplied closure under a read lock
func (sr *syncRaft) withReadLock(fn func(raftService) error) error {
	svc, unlock, err := sr.getSvc()
	defer unlock()

	if err != nil {
		return err
	}
	return fn(svc)
}

func (cfg *DatabaseConfig) stringReplicas(excludeAddr *net.TCPAddr) (replicas []string) {
	for _, r := range cfg.Replicas {
		if common.CmpTCPAddr(r, excludeAddr) {
			continue
		}
		replicas = append(replicas, r.String())
	}
	return
}

func (sta *syncTCPAddr) String() string {
	if sta == nil || sta.Addr == nil {
		return "(nil)"
	}
	return sta.Addr.String()
}

func (sta *syncTCPAddr) set(addr *net.TCPAddr) {
	sta.Lock()
	defer sta.Unlock()
	sta.Addr = addr
}

func (sta *syncTCPAddr) get() *net.TCPAddr {
	sta.RLock()
	defer sta.RUnlock()
	return sta.Addr
}

// NewDatabase returns a configured and initialized Database instance.
func NewDatabase(log logging.Logger, cfg *DatabaseConfig) (*Database, error) {
	if cfg == nil {
		cfg = &DatabaseConfig{}
	}

	if cfg.SystemName == "" {
		cfg.SystemName = build.DefaultSystemName
	}

	db := &Database{
		log:                log,
		cfg:                cfg,
		replicaAddr:        &syncTCPAddr{},
		shutdownErrCh:      make(chan error),
		raftLeaderNotifyCh: make(chan bool),

		data: &dbData{
			log: log,

			Members: &MemberDatabase{
				Ranks:        make(MemberRankMap),
				Uuids:        make(MemberUuidMap),
				Addrs:        make(MemberAddrMap),
				FaultDomains: system.NewFaultDomainTree(),
			},
			Pools: &PoolDatabase{
				Ranks:  make(PoolRankMap),
				Uuids:  make(PoolUuidMap),
				Labels: make(PoolLabelMap),
			},
			System: &SystemDatabase{
				Attributes: make(map[string]string),
			},
			SchemaVersion: CurrentSchemaVersion,
		},
	}

	for _, repAddr := range db.cfg.Replicas {
		if !common.IsLocalAddr(repAddr) {
			continue
		}
		db.setReplica(repAddr)
	}

	return db, nil
}

// isReplica returns true if the supplied address matches
// a known replica address.
func (db *Database) isReplica(ctrlAddr *net.TCPAddr) bool {
	for _, candidate := range db.cfg.Replicas {
		if common.CmpTCPAddr(ctrlAddr, candidate) {
			return true
		}
	}

	return false
}

// SystemName returns the system name set in the configuration.
func (db *Database) SystemName() string {
	return db.cfg.SystemName
}

// LeaderQuery returns the system leader, if known.
func (db *Database) LeaderQuery() (leader string, replicas []string, err error) {
	if !db.IsReplica() {
		return "", nil, &system.ErrNotReplica{db.cfg.stringReplicas(nil)}
	}

	return db.leaderHint(), db.cfg.stringReplicas(nil), nil
}

// ReplicaAddr returns the system's replica address if
// the system is configured as a MS replica.
func (db *Database) ReplicaAddr() (*net.TCPAddr, error) {
	if !db.IsReplica() {
		return nil, &system.ErrNotReplica{db.cfg.stringReplicas(nil)}
	}
	return db.getReplica(), nil
}

// PeerAddrs returns the addresses of this system's replication peers.
func (db *Database) PeerAddrs() ([]*net.TCPAddr, error) {
	myAddr, err := db.ReplicaAddr()
	if err != nil {
		return nil, err
	}

	var peers []*net.TCPAddr
	for _, rep := range db.cfg.Replicas {
		if !common.CmpTCPAddr(myAddr, rep) {
			peers = append(peers, rep)
		}
	}
	return peers, nil
}

// getReplica safely returns the current local replica address.
func (db *Database) getReplica() *net.TCPAddr {
	return db.replicaAddr.get()
}

// setReplica safely sets the current local replica address.
func (db *Database) setReplica(addr *net.TCPAddr) {
	db.replicaAddr.set(addr)
	db.log.Debugf("set db replica addr: %s", addr)
}

// IsReplica returns true if the system is configured as a replica.
func (db *Database) IsReplica() bool {
	return db != nil && db.getReplica() != nil
}

// IsBootstrap returns true if the system is a replica and meets the
// criteria for bootstrapping (starting without configured peers) the
// system database as part of initial wireup.
func (db *Database) IsBootstrap() bool {
	if !db.IsReplica() {
		return false
	}
	// Only the first replica should bootstrap. All the others
	// should be added as voters.
	return common.CmpTCPAddr(db.cfg.Replicas[0], db.getReplica())
}

// CheckReplica returns an error if the node is not configured as a
// replica or the service is not running.
func (db *Database) CheckReplica() error {
	if !db.IsReplica() {
		return &system.ErrNotReplica{db.cfg.stringReplicas(nil)}
	}

	if db.initialized.IsFalse() {
		return system.ErrUninitialized
	}

	return db.raft.withReadLock(func(_ raftService) error { return nil })
}

// CheckLeader returns an error if the node is not a replica
// or is not the current system leader. The error can be inspected
// for hints about where to find the current leader.
func (db *Database) CheckLeader() error {
	if err := db.CheckReplica(); err != nil {
		return err
	}

	return db.raft.withReadLock(func(svc raftService) error {
		if svc.State() != raft.Leader {
			return &system.ErrNotLeader{
				LeaderHint: db.leaderHint(),
				Replicas:   db.cfg.stringReplicas(db.getReplica()),
			}
		}
		return nil
	})
}

// leaderHint returns a string representation of the current raft
// leader address, if known, or an empty string otherwise.
func (db *Database) leaderHint() string {
	var leaderHint raft.ServerAddress
	if err := db.raft.withReadLock(func(svc raftService) error {
		leaderHint = svc.Leader()
		return nil
	}); err != nil {
		return ""
	}
	return string(leaderHint)
}

// IsLeader returns a boolean indicating whether or not this
// system thinks that is a) a replica and b) the current leader.
func (db *Database) IsLeader() bool {
	return db.CheckLeader() == nil
}

// OnLeadershipGained registers callbacks to be run when this instance
// gains the leadership role.
func (db *Database) OnLeadershipGained(fns ...onLeadershipGainedFn) {
	db.onLeadershipGained = append(db.onLeadershipGained, fns...)
}

// OnLeadershipLost registers callbacks to be run when this instance
// loses the leadership role.
func (db *Database) OnLeadershipLost(fns ...onLeadershipLostFn) {
	db.onLeadershipLost = append(db.onLeadershipLost, fns...)
}

// OnRaftShutdown registers callbacks to be run when this instance
// shuts down.
func (db *Database) OnRaftShutdown(fns ...onRaftShutdownFn) {
	db.onRaftShutdown = append(db.onRaftShutdown, fns...)
}

// Start checks to see if the system is configured as a MS replica. If
// not, it returns early without an error. If it is, the persistent storage
// is initialized if necessary, and the replica is started to begin the
// process of choosing a MS leader.
func (db *Database) Start(parent context.Context) error {
	if !db.IsReplica() {
		return nil
	}

	db.log.Debugf("system db start: isReplica: %t, isBootstrap: %t", db.IsReplica(), db.IsBootstrap())

	var newDB bool

	if _, err := os.Stat(db.cfg.RaftDir); err != nil {
		if !os.IsNotExist(err) {
			return errors.Wrapf(err, "can't Stat() %s", db.cfg.RaftDir)
		}
		newDB = true
		if err := os.Mkdir(db.cfg.RaftDir, 0700); err != nil {
			return errors.Wrapf(err, "failed to Mkdir() %s", db.cfg.RaftDir)
		}
	}

	if err := db.configureRaft(); err != nil {
		return errors.Wrap(err, "unable to configure raft service")
	}

	// Set this before starting raft so that we can distinguish between
	// an unformatted system and one where raft isn't started.
	db.initialized.SetTrue()

	if err := db.startRaft(newDB); err != nil {
		return errors.Wrap(err, "unable to start raft service")
	}

	// Create a child context with cancel callback and stash
	// the cancel for use by Stop().
	var ctx context.Context
	ctx, db.shutdownCb = context.WithCancel(parent)

	// Kick off a goroutine to monitor the leadership state channel.
	go db.monitorLeadershipState(ctx)

	return nil
}

// RemoveFiles destructively removes files associated with the system
// database.
func (db *Database) RemoveFiles() error {
	return os.RemoveAll(db.cfg.RaftDir)
}

// Stop signals to the database that it should shutdown all background
// tasks and release any resources.
func (db *Database) Stop() error {
	if db.shutdownCb == nil {
		return errors.New("no shutdown callback set")
	}

	db.shutdownCb()
	return <-db.shutdownErrCh
}

// monitorLeadershipState runs a loop to monitor for leadership state
// change events. On receipt of a state change, executes callbacks
// set with OnLeadershipGained() or OnLeadershipLost(), as appropriate.
func (db *Database) monitorLeadershipState(parent context.Context) {
	var cancelGainedCtx context.CancelFunc

	runOnLeadershipLost := func() {
		for _, fn := range db.onLeadershipLost {
			if err := fn(); err != nil {
				db.log.Errorf("failure in onLeadershipLost callback: %s", err)
			}
		}
	}

	for {
		select {
		case <-parent.Done():
			if cancelGainedCtx != nil {
				cancelGainedCtx()
			}
			runOnLeadershipLost()

			db.shutdownErrCh <- db.ShutdownRaft()
			close(db.shutdownErrCh)
			return
		case isLeader := <-db.raftLeaderNotifyCh:
			if !isLeader {
				db.log.Debugf("node %s lost MS leader state", db.replicaAddr)
				if cancelGainedCtx != nil {
					cancelGainedCtx()
				}
				runOnLeadershipLost()
				break
			}

			db.log.Debugf("node %s gained MS leader state", db.replicaAddr)
			var gainedCtx context.Context
			gainedCtx, cancelGainedCtx = context.WithCancel(parent)
			for _, fn := range db.onLeadershipGained {
				if err := fn(gainedCtx); err != nil {
					db.log.Errorf("failure in onLeadershipGained callback: %s", err)
					cancelGainedCtx()
					_ = db.ResignLeadership(err)
					break
				}
			}

		}
	}
}

// IncMapVer forces the system database to increment the map version.
func (db *Database) IncMapVer() error {
	if err := db.CheckLeader(); err != nil {
		return err
	}

	return db.submitIncMapVer()
}

func newGroupMap(version uint32) *GroupMap {
	return &GroupMap{
		Version:     version,
		RankEntries: make(map[system.Rank]RankEntry),
	}
}

// GroupMap returns the latest system group map.
func (db *Database) GroupMap() (*GroupMap, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	gm := newGroupMap(db.data.MapVersion)
	for _, srv := range db.data.Members.Ranks {
		// Only members that have been auto-excluded or administratively
		// excluded should be omitted from the group map. If a member
		// is actually down, it will be marked dead by swim and moved
		// into the excluded state eventually.
		if srv.State&system.ExcludedMemberFilter != 0 {
			continue
		}
		// Quick sanity-check: Don't include members that somehow have
		// a nil rank or fabric URI, either.
		if srv.Rank.Equals(system.NilRank) || srv.FabricURI == "" {
			db.log.Errorf("member has invalid rank (%d) or URI (%s)", srv.Rank, srv.FabricURI)
			continue
		}
		gm.RankEntries[srv.Rank] = RankEntry{URI: srv.FabricURI, Incarnation: srv.Incarnation}
		if db.isReplica(srv.Addr) {
			gm.MSRanks = append(gm.MSRanks, srv.Rank)
		}
	}

	if len(gm.RankEntries) == 0 {
		return nil, system.ErrEmptyGroupMap
	}

	return gm, nil
}

// copyMember makes a copy of the supplied Member pointer
// for safe use outside of the database.
func copyMember(in *system.Member) *system.Member {
	out := new(system.Member)
	*out = *in
	return out
}

// AllMembers returns a copy of the system membership.
func (db *Database) AllMembers() ([]*system.Member, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	// NB: This is expensive! We make a copy of the
	// membership to ensure that it can't be changed
	// elsewhere.
	dbCopy := make([]*system.Member, len(db.data.Members.Uuids))
	copyIdx := 0
	for _, dbRec := range db.data.Members.Uuids {
		dbCopy[copyIdx] = copyMember(dbRec)
		copyIdx++
	}
	return dbCopy, nil
}

// filterMembers returns the set of members with states matching the
// supplied list of MemberStates. Note that the returned list is
// non-deterministic, so callers should sort the results if that is
// important.
//
// NB: If the returned members will be used outside of the database,
// they should be copied using the copyMember() helper in order to
// allow them to be safely modified.
func (db *Database) filterMembers(desiredStates ...system.MemberState) (result []*system.Member) {
	// NB: Must be done under a lock!

	var includeUnknown bool
	stateMask := system.AllMemberFilter
	if len(desiredStates) > 0 {
		stateMask = 0
		for _, s := range desiredStates {
			if s == system.MemberStateUnknown {
				includeUnknown = true
			}
			stateMask |= s
		}
	}
	if stateMask == system.AllMemberFilter {
		includeUnknown = true
	}

	for _, m := range db.data.Members.Ranks {
		if m.State == system.MemberStateUnknown && includeUnknown || m.State&stateMask > 0 {
			result = append(result, m)
		}
	}

	return
}

// MemberRanks returns a slice of all the ranks in the membership.
func (db *Database) MemberRanks(desiredStates ...system.MemberState) ([]system.Rank, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	ranks := make([]system.Rank, 0, len(db.data.Members.Ranks))
	for _, m := range db.filterMembers(desiredStates...) {
		ranks = append(ranks, m.Rank)
	}

	sort.Slice(ranks, func(i, j int) bool { return ranks[i] < ranks[j] })

	return ranks, nil
}

// MemberCount returns the number of members in the system.
func (db *Database) MemberCount(desiredStates ...system.MemberState) (int, error) {
	if err := db.CheckReplica(); err != nil {
		return -1, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	return len(db.filterMembers(desiredStates...)), nil
}

// CurMapVersion returns the current system map version.
func (db *Database) CurMapVersion() (uint32, error) {
	if err := db.CheckReplica(); err != nil {
		return 0, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	return db.data.MapVersion, nil
}

// RemoveMember removes a member from the system.
func (db *Database) RemoveMember(m *system.Member) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	_, err := db.FindMemberByUUID(m.UUID)
	if err != nil {
		return err
	}

	return db.submitMemberUpdate(raftOpRemoveMember, &memberUpdate{Member: m})
}

func (db *Database) manageVoter(vc *system.Member, op raftOp) error {
	// Ignore self as a voter candidate.
	if common.CmpTCPAddr(db.getReplica(), vc.Addr) {
		return nil
	}

	// Ignore non-replica candidates.
	if !db.isReplica(vc.Addr) {
		return nil
	}

	rsi := raft.ServerID(vc.Addr.String())
	rsa := raft.ServerAddress(vc.Addr.String())

	switch op {
	case raftOpAddMember:
	case raftOpUpdateMember, raftOpRemoveMember:
		// If we're updating an existing member, we need to kick it out of the
		// raft cluster and then re-add it so that it doesn't hijack the campaign.
		db.log.Debugf("removing %s as a current raft voter", vc)
		if err := db.raft.withReadLock(func(svc raftService) error {
			return svc.RemoveServer(rsi, 0, 0).Error()
		}); err != nil {
			return errors.Wrapf(err, "failed to remove %q as a raft replica", vc.Addr)
		}
	default:
		return errors.Errorf("unhandled manageVoter op: %s", op)
	}

	db.log.Debugf("adding %s as a new raft voter", vc)
	if err := db.raft.withReadLock(func(svc raftService) error {
		return svc.AddVoter(rsi, rsa, 0, 0).Error()
	}); err != nil {
		return errors.Wrapf(err, "failed to add %q as raft replica", vc.Addr)
	}

	return nil
}

// AddMember adds a member to the system.
func (db *Database) AddMember(newMember *system.Member) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	if _, err := db.FindMemberByUUID(newMember.UUID); err == nil {
		return system.ErrUuidExists(newMember.UUID)
	}
	if _, err := db.FindMemberByRank(newMember.Rank); err == nil {
		return system.ErrRankExists(newMember.Rank)
	}

	if err := db.manageVoter(newMember, raftOpAddMember); err != nil {
		return err
	}

	mu := &memberUpdate{Member: newMember}
	if newMember.Rank.Equals(system.NilRank) {
		newMember.Rank = db.data.NextRank
		mu.NextRank = true
	}

	if err := db.submitMemberUpdate(raftOpAddMember, mu); err != nil {
		return err
	}

	return nil
}

// UpdateMember updates an existing member.
func (db *Database) UpdateMember(m *system.Member) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	_, err := db.FindMemberByUUID(m.UUID)
	if err != nil {
		return err
	}

	return db.submitMemberUpdate(raftOpUpdateMember, &memberUpdate{Member: m})
}

// FindMemberByRank searches the member database by rank. If no
// member is found, an error is returned.
func (db *Database) FindMemberByRank(rank system.Rank) (*system.Member, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if m, found := db.data.Members.Ranks[rank]; found {
		return copyMember(m), nil
	}

	return nil, system.ErrMemberRankNotFound(rank)
}

// FindMemberByUUID searches the member database by UUID. If no
// member is found, an error is returned.
func (db *Database) FindMemberByUUID(uuid uuid.UUID) (*system.Member, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if m, found := db.data.Members.Uuids[uuid]; found {
		return copyMember(m), nil
	}

	return nil, system.ErrMemberUUIDNotFound(uuid)
}

// FindMembersByAddr searches the member database by control address. If no
// members are found, an error is returned. This search may return multiple
// members, as a given address may be associated with more than one rank.
func (db *Database) FindMembersByAddr(addr *net.TCPAddr) ([]*system.Member, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	var copies []*system.Member
	if members, found := db.data.Members.Addrs[addr.String()]; found {
		for _, m := range members {
			copies = append(copies, copyMember(m))
		}
		return copies, nil
	}

	return nil, system.ErrMemberAddrNotFound(addr)
}

// FaultDomainTree returns the tree of fault domains of joined members.
func (db *Database) FaultDomainTree() *system.FaultDomainTree {
	db.data.RLock()
	defer db.data.RUnlock()

	return db.data.Members.FaultDomains.Copy()
}

// copyPoolService makes a copy of the supplied PoolService pointer
// for safe use outside of the database.
func copyPoolService(in *system.PoolService) *system.PoolService {
	out := new(system.PoolService)
	*out = *in
	return out
}

// PoolServiceList returns a list of pool services registered
// with the system. If the all parameter is not true, only
// pool services in the "Ready" state are returned.
func (db *Database) PoolServiceList(all bool) ([]*system.PoolService, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	// NB: This is expensive! We make a copy of the
	// pool services to ensure that they can't be changed
	// elsewhere.
	dbCopy := make([]*system.PoolService, 0, len(db.data.Pools.Uuids))
	for _, ps := range db.data.Pools.Uuids {
		if ps.State != system.PoolServiceStateReady && !all {
			continue
		}
		dbCopy = append(dbCopy, copyPoolService(ps))
	}
	return dbCopy, nil
}

// FindPoolServiceByUUID searches the pool database by UUID. If no
// pool service is found, an error is returned.
func (db *Database) FindPoolServiceByUUID(uuid uuid.UUID) (*system.PoolService, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if p, found := db.data.Pools.Uuids[uuid]; found {
		return copyPoolService(p), nil
	}

	return nil, system.ErrPoolUUIDNotFound(uuid)
}

// FindPoolServiceByLabel searches the pool database by Label. If no
// pool service is found, an error is returned.
func (db *Database) FindPoolServiceByLabel(label string) (*system.PoolService, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if p, found := db.data.Pools.Labels[label]; found {
		return copyPoolService(p), nil
	}

	return nil, system.ErrPoolLabelNotFound(label)
}

// AddPoolService creates an entry for a new pool service in the pool database.
func (db *Database) AddPoolService(ps *system.PoolService) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	if p, err := db.FindPoolServiceByUUID(ps.PoolUUID); err == nil {
		return errors.Errorf("pool %s already exists", p.PoolUUID)
	}

	if err := db.submitPoolUpdate(raftOpAddPoolService, ps); err != nil {
		return err
	}

	return nil
}

// RemovePoolService removes a pool database entry.
func (db *Database) RemovePoolService(uuid uuid.UUID) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	ps, err := db.FindPoolServiceByUUID(uuid)
	if err != nil {
		return errors.Wrapf(err, "failed to retrieve pool %s", uuid)
	}

	if err := db.submitPoolUpdate(raftOpRemovePoolService, ps); err != nil {
		return err
	}

	return nil
}

// UpdatePoolService updates an existing pool database entry.
func (db *Database) UpdatePoolService(ps *system.PoolService) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	_, err := db.FindPoolServiceByUUID(ps.PoolUUID)
	if err != nil {
		return errors.Wrapf(err, "failed to retrieve pool %s", ps.PoolUUID)
	}

	if err := db.submitPoolUpdate(raftOpUpdatePoolService, ps); err != nil {
		return err
	}

	return nil
}

func (db *Database) handlePoolRepsUpdate(evt *events.RASEvent) {
	ei := evt.GetPoolSvcInfo()
	if ei == nil {
		db.log.Error("no extended info in PoolSvcReplicasUpdate event received")
		return
	}
	db.log.Debugf("processing RAS event %q for pool %s with info %+v on host %q",
		evt.Msg, evt.PoolUUID, ei, evt.Hostname)

	uuid, err := uuid.Parse(evt.PoolUUID)
	if err != nil {
		db.log.Errorf("failed to parse pool UUID %q: %s", evt.PoolUUID, err)
		return
	}

	ps, err := db.FindPoolServiceByUUID(uuid)
	if err != nil {
		db.log.Errorf("failed to find pool with UUID %q: %s", evt.PoolUUID, err)
		return
	}

	db.log.Debugf("update pool %s (state=%s) svc ranks %v->%v",
		ps.PoolUUID, ps.State, ps.Replicas, ei.SvcReplicas)

	ps.Replicas = system.RanksFromUint32(ei.SvcReplicas)

	if err := db.UpdatePoolService(ps); err != nil {
		db.log.Errorf("failed to apply pool service update: %s", err)
	}
}

// OnEvent handles events and updates system database accordingly.
func (db *Database) OnEvent(_ context.Context, evt *events.RASEvent) {
	switch evt.ID {
	case events.RASPoolRepsUpdate:
		db.handlePoolRepsUpdate(evt)
	}
}

// SetSystemAttrs submits an update to the system properties map.
func (db *Database) SetSystemAttrs(props map[string]string) error {
	if err := db.CheckLeader(); err != nil {
		return err
	}
	db.Lock()
	defer db.Unlock()

	if err := db.submitSystemAttrsUpdate(props); err != nil {
		return err
	}

	return nil
}

// GetSystemAttrs returns a copy of the system properties map.
func (db *Database) GetSystemAttrs(keys []string, filterFn func(string) bool) (map[string]string, error) {
	if err := db.CheckReplica(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	// Always return a copy of the map to avoid safety issues.
	out := make(map[string]string)
	if len(keys) == 0 {
		for k, v := range db.data.System.Attributes {
			if filterFn != nil && filterFn(k) {
				continue
			}
			out[k] = v
		}
		return out, nil
	}

	for _, k := range keys {
		if v, found := db.data.System.Attributes[k]; found {
			if filterFn != nil && filterFn(k) {
				continue
			}
			out[k] = v
			continue
		}
		return nil, system.ErrSystemAttrNotFound(k)
	}
	return out, nil
}
