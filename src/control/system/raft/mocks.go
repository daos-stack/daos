//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"net"
	"testing"
	"time"

	"github.com/hashicorp/raft"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	mockRaftFuture struct {
		err      error
		index    uint64
		response interface{}
	}
	mockRaftServiceConfig struct {
		LeaderCh      <-chan bool
		ServerAddress raft.ServerAddress
		State         raft.RaftState
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
	return &mockRaftFuture{}
}

func (mrs *mockRaftService) Shutdown() raft.Future {
	mrs.cfg.State = raft.Shutdown
	return &mockRaftFuture{}
}

func (mrs *mockRaftService) State() raft.RaftState {
	return mrs.cfg.State
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
	db.replicaAddr = addr
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
	_, db.raftTransport = raft.NewInmemTransport(db.serverAddress())

	return db, cleanup
}
