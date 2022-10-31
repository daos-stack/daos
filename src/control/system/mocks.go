//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"net"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/hashicorp/raft"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func mockControlAddr(t *testing.T, idx uint32) *net.TCPAddr {
	t.Helper()

	addr, err := net.ResolveTCPAddr("tcp",
		fmt.Sprintf("127.0.0.%d:10001", idx))
	if err != nil {
		t.Fatal(err)
	}

	return addr
}

// MockMemberFullSpec returns a reference to a new member struct.
func MockMemberFullSpec(t *testing.T, rank Rank, uuidStr, uri string, addr *net.TCPAddr, state MemberState) *Member {
	t.Helper()

	newUUID, err := uuid.Parse(uuidStr)
	if err != nil {
		t.Fatal(err)
	}

	return &Member{
		Rank:        rank,
		UUID:        newUUID,
		FabricURI:   uri,
		Addr:        addr,
		state:       state,
		FaultDomain: MustCreateFaultDomain(),
		LastUpdate:  time.Now(),
	}
}

// MockMember returns a system member with appropriate values.
func MockMember(t *testing.T, idx uint32, state MemberState, info ...string) *Member {
	t.Helper()

	addr := mockControlAddr(t, idx)
	m := MockMemberFullSpec(t, Rank(idx), test.MockUUID(int32(idx)), addr.String(), addr, state)
	m.FabricContexts = idx
	if len(info) > 0 {
		m.Info = info[0]
	}

	return m
}

// MockMemberResult return a result from an action on a system member.
func MockMemberResult(rank Rank, action string, err error, state MemberState) *MemberResult {
	result := NewMemberResult(rank, err, state)
	result.Action = action

	return result
}

func MockMembership(t *testing.T, log logging.Logger, resolver resolveTCPFn) (*Membership, *Database) {
	t.Helper()

	db := MockDatabase(t, log)
	m := NewMembership(log, db)

	if resolver != nil {
		return m.WithTCPResolver(resolver), db
	}

	return m, db
}

type (
	mockRaftFuture struct {
		err      error
		index    uint64
		response interface{}
	}
	mockRaftServiceConfig struct {
		LeaderCh              <-chan bool
		ServerAddress         raft.ServerAddress
		State                 raft.RaftState
		LeadershipTransferErr error
	}
	mockRaftService struct {
		cfg mockRaftServiceConfig
		fsm raft.FSM
	}
)

// mockRaftFuture implements raft.Future, raft.IndexFuture, and raft.ApplyFuture
func (mrf *mockRaftFuture) Error() error          { return mrf.err }
func (mrf *mockRaftFuture) Index() uint64         { return mrf.index }
func (mrf *mockRaftFuture) Response() interface{} { return mrf.response }

func (mrs *mockRaftService) Apply(cmd []byte, timeout time.Duration) raft.ApplyFuture {
	mrs.fsm.Apply(&raft.Log{Data: cmd})
	return &mockRaftFuture{}
}

func (mr *mockRaftService) AddVoter(_ raft.ServerID, _ raft.ServerAddress, _ uint64, _ time.Duration) raft.IndexFuture {
	return &mockRaftFuture{}
}

func (mr *mockRaftService) RemoveServer(_ raft.ServerID, _ uint64, _ time.Duration) raft.IndexFuture {
	return &mockRaftFuture{}
}

func (mrs *mockRaftService) BootstrapCluster(cfg raft.Configuration) raft.Future {
	return &mockRaftFuture{}
}

func (mrs *mockRaftService) Leader() raft.ServerAddress {
	return mrs.cfg.ServerAddress
}

func (mrs *mockRaftService) LeaderCh() <-chan bool {
	return mrs.cfg.LeaderCh
}

func (mrs *mockRaftService) LeadershipTransfer() raft.Future {
	if mrs.cfg.LeadershipTransferErr == nil {
		mrs.cfg.State = raft.Follower
	}
	return &mockRaftFuture{err: mrs.cfg.LeadershipTransferErr}
}

func (mrs *mockRaftService) Shutdown() raft.Future {
	mrs.cfg.State = raft.Shutdown
	return &mockRaftFuture{}
}

func (mrs *mockRaftService) State() raft.RaftState {
	return mrs.cfg.State
}

func (mrs *mockRaftService) Barrier(time.Duration) raft.Future {
	return &mockRaftFuture{}
}

func newMockRaftService(cfg *mockRaftServiceConfig, fsm raft.FSM) *mockRaftService {
	if cfg == nil {
		cfg = &mockRaftServiceConfig{
			State: raft.Leader,
		}
	}
	if cfg.LeaderCh == nil {
		cfg.LeaderCh = make(<-chan bool)
	}
	return &mockRaftService{
		cfg: *cfg,
		fsm: fsm,
	}
}

// MockDatabaseWithAddr is similar to MockDatabase but allows a custom
// replica address to be supplied.
func MockDatabaseWithAddr(t *testing.T, log logging.Logger, addr *net.TCPAddr) *Database {
	dbCfg := &DatabaseConfig{}
	if addr != nil {
		dbCfg.Replicas = append(dbCfg.Replicas, addr)
	}

	db := MockDatabaseWithCfg(t, log, dbCfg)
	db.replicaAddr.Addr = addr
	return db
}

// MockDatabaseWithCfg is similar to MockDatabase but allows a custom
// DatabaseConfig to be supplied.
func MockDatabaseWithCfg(t *testing.T, log logging.Logger, dbCfg *DatabaseConfig) *Database {
	db, err := NewDatabase(log, dbCfg)
	if err != nil {
		t.Fatal(err)
	}
	db.raft.setSvc(newMockRaftService(&mockRaftServiceConfig{
		State: raft.Leader,
	}, (*fsm)(db)))
	db.initialized.SetTrue()

	return db
}

// MockDatabase returns a lightweight implementation of the system
// database that does not support raft replication and does all
// operations in memory.
func MockDatabase(t *testing.T, log logging.Logger) *Database {
	return MockDatabaseWithAddr(t, log, common.LocalhostCtrlAddr())
}

// TestDatabase returns a database that is backed by temporary storage
// and can be started. Uses an in-memory transport. Much heavier-weight
// implementation and should only be used by tests that require it.
func TestDatabase(t *testing.T, log logging.Logger, replicas ...*net.TCPAddr) (*Database, func()) {
	t.Helper()

	testDir, cleanup := test.CreateTestDir(t)

	if len(replicas) == 0 {
		replicas = append(replicas, common.LocalhostCtrlAddr())
	}

	db, err := NewDatabase(log, &DatabaseConfig{
		Replicas: replicas,
		RaftDir:  testDir + "/raft",
	})
	if err != nil {
		t.Fatal(err)
	}
	_, db.raftTransport = raft.NewInmemTransport(raft.ServerAddress(db.replicaAddr.String()))

	return db, cleanup
}
