//
// (C) Copyright 2020 Intel Corporation.
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
	"net"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/hashicorp/raft"
	raftboltdb "github.com/hashicorp/raft-boltdb"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

const (
	sysDBFile            = "daos_system.db"
	CurrentSchemaVersion = 0
)

type (
	onDatabaseStartedFn func() error

	dbData struct {
		log logging.Logger

		sync.RWMutex
		NextRank      Rank
		MapVersion    uint32
		Members       *MemberDatabase
		Pools         *PoolDatabase
		SchemaVersion uint
	}

	Database struct {
		log            logging.Logger
		cfg            *DatabaseConfig
		isReplica      bool
		resolveTCPAddr func(string, string) (*net.TCPAddr, error)
		interfaceAddrs func() ([]net.Addr, error)
		raft           *raft.Raft
		onStarted      []onDatabaseStartedFn

		rankLock sync.Mutex
		data     *dbData
	}

	DatabaseConfig struct {
		Replicas []string
		RaftDir  string
	}

	GroupMap struct {
		Version  uint32
		RankURIs map[Rank]string
	}
)

func newGroupMap(version uint32) *GroupMap {
	return &GroupMap{
		Version:  version,
		RankURIs: make(map[Rank]string),
	}
}

func NewDatabase(log logging.Logger, cfg *DatabaseConfig) *Database {
	if cfg == nil {
		cfg = &DatabaseConfig{}
	}

	return &Database{
		log:            log,
		cfg:            cfg,
		resolveTCPAddr: net.ResolveTCPAddr,
		interfaceAddrs: net.InterfaceAddrs,

		data: &dbData{
			log: log,
			Members: &MemberDatabase{
				Ranks: make(ServerRankMap),
				Uuids: make(ServerUuidMap),
				Addrs: make(ServerAddrMap),
			},
			Pools: &PoolDatabase{
				Ranks: make(PoolRankMap),
				Uuids: make(PoolUuidMap),
				Addrs: make(PoolAddrMap),
			},
			SchemaVersion: CurrentSchemaVersion,
		},
	}
}

func (db *Database) resolveReplicas() (reps []*net.TCPAddr, err error) {
	for _, rs := range db.cfg.Replicas {
		rAddr, err := db.resolveTCPAddr("tcp", rs)
		if err != nil {
			return nil, err
		}
		reps = append(reps, rAddr)
	}
	return
}

func (db *Database) checkReplica(ctrlAddr *net.TCPAddr) (repAddr *net.TCPAddr, isBootStrap bool, err error) {
	var repAddrs []*net.TCPAddr
	repAddrs, err = db.resolveReplicas()
	if err != nil {
		return
	}

	var localAddrs []net.IP
	if ctrlAddr.IP.IsUnspecified() {
		var ifaceAddrs []net.Addr
		ifaceAddrs, err = db.interfaceAddrs()
		if err != nil {
			return
		}

		for _, ia := range ifaceAddrs {
			if in, ok := ia.(*net.IPNet); ok {
				localAddrs = append(localAddrs, in.IP)
			}
		}
	} else {
		localAddrs = append(localAddrs, ctrlAddr.IP)
	}

	var idx int
	for idx, repAddr = range repAddrs {
		if repAddr.Port != ctrlAddr.Port {
			continue
		}
		for _, localAddr := range localAddrs {
			if repAddr.IP.Equal(localAddr) {
				if idx == 0 {
					isBootStrap = true
				}
				return
			}
		}
	}

	return
}

func (db *Database) checkLeader() error {
	if !db.isReplica {
		return &ErrNotReplica{db.cfg.Replicas}
	}
	if db.raft.State() != raft.Leader {
		return &ErrNotLeader{
			LeaderHint: string(db.raft.Leader()),
			Replicas:   db.cfg.Replicas,
		}
	}
	return nil
}

// OnStart registers callbacks to be run when the database
// has started.
func (db *Database) OnStart(fn onDatabaseStartedFn) {
	db.onStarted = append(db.onStarted, fn)
}

func (db *Database) Start(ctrlAddr *net.TCPAddr) error {
	var repAddr *net.TCPAddr
	var isBootStrap, needsBootStrap bool
	var err error

	repAddr, isBootStrap, err = db.checkReplica(ctrlAddr)
	if err != nil {
		return err
	}
	db.isReplica = repAddr != nil
	db.log.Debugf("system db start: isReplica: %t, isBootStrap: %t", db.isReplica, isBootStrap)

	if !db.isReplica {
		return nil
	}

	if _, err := os.Stat(db.cfg.RaftDir); err != nil {
		if !os.IsNotExist(err) {
			return errors.Wrapf(err, "can't Stat() %s", db.cfg.RaftDir)
		}
		if err := os.Mkdir(db.cfg.RaftDir, 0700); err != nil {
			return errors.Wrapf(err, "failed to Mkdir() %s", db.cfg.RaftDir)
		}
	}

	rc := raft.DefaultConfig()
	rc.Logger = newCompatLogger(db.log)
	rc.SnapshotThreshold = 16 // arbitrarily low to exercise snapshots
	//rc.SnapshotInterval = 5 * time.Second
	rc.HeartbeatTimeout = 250 * time.Millisecond
	rc.ElectionTimeout = 250 * time.Millisecond
	rc.LeaderLeaseTimeout = 125 * time.Millisecond
	rc.LocalID = raft.ServerID(repAddr.String())
	// Just use an in-memory transport for the moment, until
	// we add real replica support over gRPC.
	_, transport := raft.NewInmemTransport(raft.NewInmemAddr())

	snaps, err := raft.NewFileSnapshotStore(db.cfg.RaftDir, 2, os.Stderr)
	if err != nil {
		return err
	}

	sysDBPath := filepath.Join(db.cfg.RaftDir, sysDBFile)
	boltDB, err := raftboltdb.NewBoltStore(sysDBPath)
	if err != nil {
		return err
	}
	ra, err := raft.NewRaft(rc, (*fsm)(db), boltDB, boltDB, snaps, transport)
	if err != nil {
		return err
	}
	db.raft = ra

	needsBootStrap = func() bool {
		_, err := os.Stat(sysDBPath)
		return err == nil
	}()

	if isBootStrap && needsBootStrap {
		db.log.Debugf("bootstrapping MS on %s", rc.LocalID)
		bsc := raft.Configuration{
			Servers: []raft.Server{
				{
					ID:      rc.LocalID,
					Address: transport.LocalAddr(),
				},
			},
		}
		db.raft.BootstrapCluster(bsc)
	}

	for _, fn := range db.onStarted {
		if err := fn(); err != nil {
			return errors.Wrap(err, "failure in onStarted callback")
		}
	}

	return nil
}

func (db *Database) GroupMap() (*GroupMap, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	gm := newGroupMap(db.data.MapVersion)
	for _, srv := range db.data.Members.Ranks {
		gm.RankURIs[srv.Rank] = srv.FabricURI
	}
	return gm, nil
}

func (db *Database) ReplicaRanks() (*GroupMap, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	// TODO: Should this only return one rank per replica, or
	// should we return all ready ranks per replica, for resiliency?
	gm := newGroupMap(db.data.MapVersion)
	for _, srv := range db.data.Members.Ranks {
		repAddr, _, err := db.checkReplica(srv.Addr)
		if err != nil || repAddr == nil || srv.state != MemberStateJoined {
			continue
		}
		gm.RankURIs[srv.Rank] = srv.FabricURI
	}
	return gm, nil
}

func (db *Database) AllMembers() ([]*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	// NB: This is expensive! We make a copy of the
	// membership to ensure that it can't be changed
	// elsewhere.
	dbCopy := make([]*Member, len(db.data.Members.Uuids))
	copyIdx := 0
	for _, dbRec := range db.data.Members.Uuids {
		dbCopy[copyIdx] = new(Member)
		*dbCopy[copyIdx] = *dbRec
		copyIdx++
	}
	return dbCopy, nil
}

func (db *Database) MemberRanks() ([]Rank, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	ranks := make([]Rank, 0, len(db.data.Members.Ranks))
	for rank := range db.data.Members.Ranks {
		ranks = append(ranks, rank)
	}

	sort.Slice(ranks, func(i, j int) bool { return ranks[i] < ranks[j] })

	return ranks, nil
}

func (db *Database) MemberCount() (int, error) {
	if err := db.checkLeader(); err != nil {
		return -1, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	return len(db.data.Members.Ranks), nil
}

func (db *Database) CurMapVersion() (uint32, error) {
	if err := db.checkLeader(); err != nil {
		return 0, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	return db.data.MapVersion, nil
}

func (db *Database) RemoveMember(m *Member) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	return db.submitMemberUpdate(raftOpRemoveMember, m)
}

func (db *Database) AddMember(newMember *Member) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	db.log.Debugf("add/rejoin: %+v", newMember)

	if cur, err := db.FindMemberByUUID(newMember.UUID); err == nil {
		if !cur.Rank.Equals(newMember.Rank) {
			return errors.Errorf("re-joining server %s has different rank (%d != %d)",
				newMember.UUID, newMember.Rank, cur.Rank)
		}
		db.log.Debugf("rank %d rejoined", cur.Rank)
		return db.UpdateMember(newMember)
	}

	// TODO: Should we allow the rank to be supplied for a new member?
	// Removing this breaks the tests, but maybe the tests shouldn't be
	// specifying rank for new members?
	if newMember.Rank.Equals(NilRank) {
		// Take a lock on the rank here, so that we can apply the new member
		// with the correct rank, but increment NextRank as part of the Apply.
		db.rankLock.Lock()
		defer db.rankLock.Unlock()
		newMember.Rank = db.data.NextRank
	}

	if err := db.submitMemberUpdate(raftOpAddMember, newMember); err != nil {
		return err
	}

	db.log.Debugf("added rank %d", newMember.Rank)

	return nil
}

func (db *Database) UpdateMember(m *Member) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	return db.submitMemberUpdate(raftOpUpdateMember, m)
}

func (db *Database) FindMemberByRank(rank Rank) (*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if m, found := db.data.Members.Ranks[rank]; found {
		return m, nil
	}

	return nil, &ErrMemberNotFound{byRank: &rank}
}

func (db *Database) FindMemberByUUID(uuid uuid.UUID) (*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if m, found := db.data.Members.Uuids[uuid]; found {
		return m, nil
	}

	return nil, &ErrMemberNotFound{byUUID: &uuid}
}

func (db *Database) FindMembersByAddr(addr net.Addr) ([]*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if m, found := db.data.Members.Addrs[addr]; found {
		return m, nil
	}

	return nil, &ErrMemberNotFound{byAddr: addr}
}

func (db *Database) PoolServiceList() ([]*PoolService, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	// NB: This is expensive! We make a copy of the
	// pool services to ensure that they can't be changed
	// elsewhere.
	dbCopy := make([]*PoolService, len(db.data.Pools.Uuids))
	copyIdx := 0
	for _, dbRec := range db.data.Pools.Uuids {
		dbCopy[copyIdx] = new(PoolService)
		*dbCopy[copyIdx] = *dbRec
		copyIdx++
	}
	return dbCopy, nil
}

func (db *Database) FindPoolServiceByUUID(uuid uuid.UUID) (*PoolService, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.data.RLock()
	defer db.data.RUnlock()

	if p, found := db.data.Pools.Uuids[uuid]; found {
		return p, nil
	}

	return nil, &ErrPoolNotFound{byUUID: &uuid}
}

func (db *Database) AddPoolService(ps *PoolService) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	db.log.Debugf("add pool service: %+v", ps)

	if p, err := db.FindPoolServiceByUUID(ps.PoolUUID); err == nil {
		return errors.Errorf("pool %s already exists", p.PoolUUID)
	}

	if err := db.submitPoolUpdate(raftOpAddPoolService, ps); err != nil {
		return err
	}

	db.log.Debugf("added pool service %s", ps.PoolUUID)

	return nil
}

func (db *Database) RemovePoolService(uuid uuid.UUID) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	db.log.Debugf("remove pool service: %s", uuid)

	ps, err := db.FindPoolServiceByUUID(uuid)
	if err != nil {
		return errors.Wrapf(err, "failed to retrieve pool %s", uuid)
	}

	if err := db.submitPoolUpdate(raftOpRemovePoolService, ps); err != nil {
		return err
	}

	db.log.Debugf("removed pool service %s", ps.PoolUUID)

	return nil
}

func (db *Database) UpdatePoolService(ps *PoolService) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	db.log.Debugf("update pool service: %+v", ps)

	_, err := db.FindPoolServiceByUUID(ps.PoolUUID)
	if err != nil {
		return errors.Wrapf(err, "failed to retrieve pool %s", ps.PoolUUID)
	}

	if err := db.submitPoolUpdate(raftOpUpdatePoolService, ps); err != nil {
		return err
	}

	db.log.Debugf("updated pool service %s", ps.PoolUUID)

	return nil
}
