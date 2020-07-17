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
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
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
	MemberDatabase struct {
		Ranks ServerRankMap
		Uuids ServerUuidMap
		Addrs ServerAddrMap
	}

	PoolDatabase struct {
		Ranks PoolRankMap
		Uuids PoolUuidMap
		Addrs PoolAddrMap
	}

	Database struct {
		sync.RWMutex
		rankLock       sync.Mutex
		log            logging.Logger
		cfg            *DatabaseConfig
		isReplica      bool
		resolveTCPAddr func(string, string) (*net.TCPAddr, error)
		interfaceAddrs func() ([]net.Addr, error)
		raft           *raft.Raft

		NextRank      Rank
		MapVersion    uint32
		Members       *MemberDatabase
		Pools         *PoolDatabase
		SchemaVersion uint
	}

	DatabaseConfig struct {
		Replicas []string
		RaftDir  string
	}
)

func (mdb *MemberDatabase) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	type fromJSON MemberDatabase
	from := &struct {
		Ranks map[Rank]uuid.UUID
		Addrs map[string][]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[Rank]uuid.UUID),
		Addrs:    make(map[string][]uuid.UUID),
		fromJSON: (*fromJSON)(mdb),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	for rank, uuid := range from.Ranks {
		member, found := mdb.Uuids[uuid]
		if !found {
			return errors.Errorf("rank %d missing UUID", rank)
		}
		mdb.Ranks[rank] = member
	}

	for addrStr, uuids := range from.Addrs {
		for _, uuid := range uuids {
			member, found := mdb.Uuids[uuid]
			if !found {
				return errors.Errorf("addr %s missing UUID", addrStr)
			}

			addr, err := net.ResolveTCPAddr("tcp", addrStr)
			if err != nil {
				return err
			}
			mdb.Addrs.addMember(addr, member)
		}
	}

	return nil
}

func (mdb *MemberDatabase) addMember(m *Member) {
	mdb.Ranks[m.Rank] = m
	mdb.Uuids[m.UUID] = m
	mdb.Addrs.addMember(m.Addr, m)
}

func (mdb *MemberDatabase) removeMember(m *Member) {
	delete(mdb.Ranks, m.Rank)
	delete(mdb.Uuids, m.UUID)
	delete(mdb.Addrs, m.Addr)
}

func (pdb *PoolDatabase) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	type fromJSON PoolDatabase
	from := &struct {
		Ranks map[Rank][]uuid.UUID
		Addrs map[string][]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[Rank][]uuid.UUID),
		Addrs:    make(map[string][]uuid.UUID),
		fromJSON: (*fromJSON)(pdb),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	for rank, uuids := range from.Ranks {
		for _, uuid := range uuids {
			svc, found := pdb.Uuids[uuid]
			if !found {
				return errors.Errorf("rank %d missing UUID", rank)
			}

			if _, exists := pdb.Ranks[rank]; !exists {
				pdb.Ranks[rank] = []*PoolService{}
			}
			pdb.Ranks[rank] = append(pdb.Ranks[rank], svc)
		}
	}

	for addrStr, uuids := range from.Addrs {
		for _, uuid := range uuids {
			svc, found := pdb.Uuids[uuid]
			if !found {
				return errors.Errorf("addr %s missing UUID", addrStr)
			}

			addr, err := net.ResolveTCPAddr("tcp", addrStr)
			if err != nil {
				return err
			}
			if _, exists := pdb.Addrs[addr]; !exists {
				pdb.Addrs[addr] = []*PoolService{}
			}
			pdb.Addrs[addr] = append(pdb.Addrs[addr], svc)
		}
	}

	return nil
}

func (pdb *PoolDatabase) addService(ps *PoolService) {
	pdb.Uuids[ps.PoolUUID] = ps
	for _, rank := range ps.Replicas {
		pdb.Ranks[rank] = append(pdb.Ranks[rank], ps)
	}
}

func (pdb *PoolDatabase) removeService(ps *PoolService) {
	delete(pdb.Uuids, ps.PoolUUID)
	for _, rank := range ps.Replicas {
		rankServices := pdb.Ranks[rank]
		for idx, rs := range rankServices {
			if rs.PoolUUID == ps.PoolUUID {
				pdb.Ranks[rank] = append(rankServices[:idx], rankServices[:idx]...)
				break
			}
		}
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
	}
}

type ErrNotReplica struct {
	Replicas []string
}

func (enr *ErrNotReplica) Error() string {
	return fmt.Sprintf("not a system db replica (try one of %s)",
		strings.Join(enr.Replicas, ","))
}

type ErrNotLeader struct {
	Replicas []string
}

func (enr *ErrNotLeader) Error() string {
	// TODO: remove self from returned replicas
	return fmt.Sprintf("not the system db leader (try one of %s)",
		strings.Join(enr.Replicas, ","))
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

func (db *Database) checkReplica(ctrlAddr *net.TCPAddr) (isReplica, isBootStrap bool, err error) {
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

	for idx, repAddr := range repAddrs {
		if repAddr.Port != ctrlAddr.Port {
			continue
		}
		for _, localAddr := range localAddrs {
			if repAddr.IP.Equal(localAddr) {
				isReplica = true
				if idx == 0 {
					isBootStrap = true
				}
				break
			}
		}
		if isReplica {
			break
		}
	}

	return
}

func (db *Database) Start(ctrlAddr *net.TCPAddr) error {
	var isBootStrap, needsBootStrap bool
	var err error

	db.isReplica, isBootStrap, err = db.checkReplica(ctrlAddr)
	if err != nil {
		return err
	}
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
	rc.SnapshotThreshold = 16 // arbitrarily low to exercise snapshots
	//rc.SnapshotInterval = 5 * time.Second
	// Just use an in-memory transport for the moment, until
	// we add real replica support over gRPC.
	_, transport := raft.NewInmemTransport(raft.NewInmemAddr())
	rc.LocalID = raft.ServerID("node00")

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

	return nil
}

type GroupMap struct {
	Version  uint32
	RankURIs map[Rank]string
}

func NewGroupMap(version uint32) *GroupMap {
	return &GroupMap{
		Version:  version,
		RankURIs: make(map[Rank]string),
	}
}

func (db *Database) GroupMap() (*GroupMap, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.RLock()
	defer db.RUnlock()

	gm := NewGroupMap(db.MapVersion)
	for _, srv := range db.Members.Ranks {
		gm.RankURIs[srv.Rank] = srv.FabricURI
	}
	return gm, nil
}

func (db *Database) ReplicaRanks() (*GroupMap, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.RLock()
	defer db.RUnlock()

	// TODO: Should this only return one rank per replica, or
	// should we return all ready ranks per replica, for resiliency?
	gm := NewGroupMap(db.MapVersion)
	for _, srv := range db.Members.Ranks {
		isReplica, _, err := db.checkReplica(srv.Addr)
		// FIXME: Joined doesn't seem like the right state here, but
		// I don't see where it ever transitions to Ready...
		if err != nil || !isReplica || srv.state != MemberStateJoined {
			continue
		}
		gm.RankURIs[srv.Rank] = srv.FabricURI
	}
	return gm, nil
}

func (db *Database) AllMembers() []*Member {
	db.RLock()
	defer db.RUnlock()

	// NB: This is expensive! We make a copy of the
	// membership to ensure that it can't be changed
	// elsewhere.
	dbCopy := make([]*Member, len(db.Members.Uuids))
	copyIdx := 0
	for _, dbRec := range db.Members.Uuids {
		dbCopy[copyIdx] = new(Member)
		*dbCopy[copyIdx] = *dbRec
		copyIdx++
	}
	return dbCopy
}

func (db *Database) MemberRanks() []Rank {
	db.RLock()
	defer db.RUnlock()

	ranks := make([]Rank, 0, len(db.Members.Ranks))
	for rank := range db.Members.Ranks {
		ranks = append(ranks, rank)
	}
	return ranks
}

func (db *Database) MemberCount() int {
	db.RLock()
	defer db.RUnlock()

	return len(db.Members.Ranks)
}

type raftOp uint32

func (ro raftOp) String() string {
	return [...]string{
		"noop",
		"addMember",
		"updateMember",
		"removeMember",
	}[ro]
}

const (
	raftOpNoop raftOp = iota
	raftOpAddMember
	raftOpUpdateMember
	raftOpRemoveMember
	raftOpAddPoolService
	raftOpUpdatePoolService
	raftOpRemovePoolService
)

type raftUpdate struct {
	Time time.Time
	Op   raftOp
	Data json.RawMessage
}

var raftTimeout = 1 * time.Second

func (db *Database) applyMembership(op raftOp, m *Member) error {
	data, err := json.Marshal(m)
	if err != nil {
		return err
	}
	return db.applyUpdate(op, data)
}

func (db *Database) applyPoolService(op raftOp, ps *PoolService) error {
	data, err := json.Marshal(ps)
	if err != nil {
		return err
	}
	return db.applyUpdate(op, data)
}

func (db *Database) applyUpdate(op raftOp, data []byte) error {
	data, err := json.Marshal(&raftUpdate{
		Time: time.Now(),
		Op:   op,
		Data: data,
	})
	if err != nil {
		return err
	}

	return db.raft.Apply(data, raftTimeout).Error()
}

func (db *Database) CurMapVersion() uint32 {
	db.RLock()
	defer db.RUnlock()

	return db.MapVersion
}

func (db *Database) RemoveMember(m *Member) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	return db.applyMembership(raftOpRemoveMember, m)
}

func (db *Database) checkLeader() error {
	if !db.isReplica {
		return &ErrNotReplica{db.cfg.Replicas}
	}
	if db.raft.State() != raft.Leader {
		return &ErrNotLeader{db.cfg.Replicas}
	}
	return nil
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
		newMember.Rank = db.NextRank
	}

	if err := db.applyMembership(raftOpAddMember, newMember); err != nil {
		return err
	}

	db.log.Debugf("added rank %d", newMember.Rank)

	return nil
}

func (db *Database) UpdateMember(m *Member) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	return db.applyMembership(raftOpUpdateMember, m)
}

type FindMemberError struct {
	byRank *Rank
	byUUID *uuid.UUID
	byAddr net.Addr
}

func (fme *FindMemberError) Error() string {
	switch {
	case fme.byRank != nil:
		return fmt.Sprintf("unable to find member with rank %d", *fme.byRank)
	case fme.byUUID != nil:
		return fmt.Sprintf("unable to find member with uuid %s", *fme.byUUID)
	case fme.byAddr != nil:
		return fmt.Sprintf("unable to find member with addr %s", fme.byAddr)
	default:
		return "unable to find member"
	}
}

func (db *Database) FindMemberByRank(rank Rank) (*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.RLock()
	defer db.RUnlock()

	if m, found := db.Members.Ranks[rank]; found {
		return m, nil
	}

	return nil, &FindMemberError{byRank: &rank}
}

func (db *Database) FindMemberByUUID(uuid uuid.UUID) (*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.RLock()
	defer db.RUnlock()

	if m, found := db.Members.Uuids[uuid]; found {
		return m, nil
	}

	return nil, &FindMemberError{byUUID: &uuid}
}

func (db *Database) FindMembersByAddr(addr net.Addr) ([]*Member, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.RLock()
	defer db.RUnlock()

	if m, found := db.Members.Addrs[addr]; found {
		return m, nil
	}

	return nil, &FindMemberError{byAddr: addr}
}

type FindPoolError struct {
	byRank *Rank
	byUUID *uuid.UUID
	byAddr net.Addr
}

func (fpe *FindPoolError) Error() string {
	switch {
	case fpe.byRank != nil:
		return fmt.Sprintf("unable to find pool service with rank %d", *fpe.byRank)
	case fpe.byUUID != nil:
		return fmt.Sprintf("unable to find pool service with uuid %s", *fpe.byUUID)
	default:
		return "unable to find pool service"
	}
}

func (db *Database) PoolServiceList() []*PoolService {
	db.RLock()
	defer db.RUnlock()

	// NB: This is expensive! We make a copy of the
	// pool services to ensure that they can't be changed
	// elsewhere.
	dbCopy := make([]*PoolService, len(db.Pools.Uuids))
	copyIdx := 0
	for _, dbRec := range db.Pools.Uuids {
		dbCopy[copyIdx] = new(PoolService)
		*dbCopy[copyIdx] = *dbRec
		copyIdx++
	}
	return dbCopy
}

func (db *Database) FindPoolServiceByUUID(uuid uuid.UUID) (*PoolService, error) {
	if err := db.checkLeader(); err != nil {
		return nil, err
	}
	db.RLock()
	defer db.RUnlock()

	if p, found := db.Pools.Uuids[uuid]; found {
		return p, nil
	}

	return nil, &FindPoolError{byUUID: &uuid}
}

func (db *Database) AddPoolService(ps *PoolService) error {
	if err := db.checkLeader(); err != nil {
		return err
	}
	db.log.Debugf("add pool service: %+v", ps)

	if p, err := db.FindPoolServiceByUUID(ps.PoolUUID); err == nil {
		return errors.Errorf("pool %s already exists", p.PoolUUID)
	}

	if err := db.applyPoolService(raftOpAddPoolService, ps); err != nil {
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

	if err := db.applyPoolService(raftOpRemovePoolService, ps); err != nil {
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

	if err := db.applyPoolService(raftOpUpdatePoolService, ps); err != nil {
		return err
	}

	db.log.Debugf("updated pool service %s", ps.PoolUUID)

	return nil
}

type fsm Database

func (f *fsm) applyMemberUpdate(op raftOp, data []byte) {
	m := new(Member)
	if err := json.Unmarshal(data, m); err != nil {
		f.log.Errorf("failed to decode member update: %s", err)
		return
	}

	switch op {
	case raftOpAddMember:
		if !m.Rank.Equals(f.NextRank) {
			f.log.Errorf("unexpected new member rank (%d != %d)", m.Rank, f.NextRank)
		}
		f.NextRank++
		f.Members.addMember(m)
		f.log.Debugf("added member: %+v", m)
	case raftOpUpdateMember:
		cur, found := f.Members.Uuids[m.UUID]
		if !found {
			f.log.Errorf("member update for unknown member %+v", m)
		}
		cur.state = m.state
		cur.Info = m.Info
		f.log.Debugf("updated member: %+v", m)
	case raftOpRemoveMember:
		f.Members.removeMember(m)
		f.log.Debugf("removed %+v", m)
	default:
		f.log.Errorf("unhandled Member Apply operation: %d", op)
		return
	}

	f.MapVersion++
}

func (f *fsm) applyPoolServiceUpdate(op raftOp, data []byte) {
	ps := new(PoolService)
	if err := json.Unmarshal(data, ps); err != nil {
		f.log.Errorf("failed to decode pool service update: %s", err)
		return
	}

	switch op {
	case raftOpAddPoolService:
		f.Pools.addService(ps)
		f.log.Debugf("added pool service: %+v", ps)
	case raftOpUpdatePoolService:
		cur, found := f.Pools.Uuids[ps.PoolUUID]
		if !found {
			f.log.Errorf("pool service update for unknown pool %+v", ps)
		}
		cur.State = ps.State
		cur.Replicas = ps.Replicas
		f.log.Debugf("updated pool service: %+v", ps)
	case raftOpRemovePoolService:
		f.Pools.removeService(ps)
		f.log.Debugf("removed %+v", ps)
	default:
		f.log.Errorf("unhandled Pool Service Apply operation: %d", op)
		return
	}
}

func (f *fsm) Apply(l *raft.Log) interface{} {
	c := new(raftUpdate)
	if err := json.Unmarshal(l.Data, c); err != nil {
		f.log.Errorf("failed to unmarshal %+v: %s", l.Data, err)
		return nil
	}

	f.log.Debugf("applying log: %+v", string(l.Data))

	f.Lock()
	defer f.Unlock()
	switch c.Op {
	case raftOpAddMember, raftOpUpdateMember, raftOpRemoveMember:
		f.applyMemberUpdate(c.Op, c.Data)
	case raftOpAddPoolService, raftOpUpdatePoolService, raftOpRemovePoolService:
		f.applyPoolServiceUpdate(c.Op, c.Data)
	default:
		f.log.Errorf("unhandled Apply operation: %d", c.Op)
	}

	return nil
}

func (f *fsm) Snapshot() (raft.FSMSnapshot, error) {
	f.Lock()
	defer f.Unlock()

	data, err := json.Marshal(f)
	if err != nil {
		return nil, err
	}

	f.log.Debugf("db snapshot saved: %+v", string(data))
	return &fsmSnapshot{data}, nil
}

func (f *fsm) Restore(rc io.ReadCloser) error {
	db := NewDatabase(nil, nil)
	if err := json.NewDecoder(rc).Decode(db); err != nil {
		return err
	}

	if db.SchemaVersion != CurrentSchemaVersion {
		return errors.Errorf("restored schema version %d != %d",
			db.SchemaVersion, CurrentSchemaVersion)
	}

	f.Members = db.Members
	f.Pools = db.Pools
	f.NextRank = db.NextRank
	f.MapVersion = db.MapVersion
	f.log.Debugf("db snapshot loaded: %+v", db)
	return nil
}

type fsmSnapshot struct {
	data []byte
}

func (f *fsmSnapshot) Persist(sink raft.SnapshotSink) error {
	err := func() error {
		if _, err := sink.Write(f.data); err != nil {
			return err
		}

		return sink.Close()
	}()

	if err != nil {
		sink.Cancel()
	}

	return err
}

func (f *fsmSnapshot) Release() {}
