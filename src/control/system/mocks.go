//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// c6xuplcrnless required by applicable law or agreed to in writing, software
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
	"testing"
	"time"

	"github.com/hashicorp/raft"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func mockControlAddr(t *testing.T, idx uint32) *net.TCPAddr {
	addr, err := net.ResolveTCPAddr("tcp",
		fmt.Sprintf("127.0.0.%d:10001", idx))
	if err != nil {
		t.Fatal(err)
	}
	return addr
}

// MockMember returns a system member with appropriate values.
func MockMember(t *testing.T, idx uint32, state MemberState, info ...string) *Member {
	addr := mockControlAddr(t, idx)
	m := NewMember(Rank(idx), common.MockUUID(int32(idx)),
		addr.String(), addr, state)
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

func MockMembership(t *testing.T, log logging.Logger) *Membership {
	return NewMembership(log, MockDatabase(t, log))
}

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

// MockDatabase returns a lightweight implementation of the system
// database that does not support raft replication and does all
// operations in memory.
func MockDatabase(t *testing.T, log logging.Logger) *Database {
	db := NewDatabase(log, nil)
	db.replicaAddr.Addr = &net.TCPAddr{}
	db.raft = newMockRaftService(&mockRaftServiceConfig{
		State: raft.Leader,
	}, (*fsm)(db))

	return db
}
