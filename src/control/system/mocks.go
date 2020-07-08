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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/hashicorp/raft"
)

// MockMember returns a system member with appropriate values.
func MockMember(t *testing.T, idx uint32, state MemberState, info ...string) *Member {
	addr, err := net.ResolveTCPAddr("tcp",
		fmt.Sprintf("127.0.0.%d:10001", idx))
	if err != nil {
		t.Fatal(err)
	}

	m := NewMember(Rank(idx), common.MockUUID(int32(idx)),
		addr.String(), addr, state)
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

func MockDatabase(t *testing.T, log logging.Logger) *Database {
	db := NewDatabase(log, nil)
	db.isReplica = true
	addr, it := raft.NewInmemTransport(raft.NewInmemAddr())
	rCfg := raft.DefaultConfig()
	rCfg.LocalID = raft.ServerID(addr)
	rCfg.HeartbeatTimeout = 10 * time.Millisecond
	rCfg.ElectionTimeout = 10 * time.Millisecond
	rCfg.LeaderLeaseTimeout = 5 * time.Millisecond
	rCfg.LogLevel = "ERROR"
	dss := raft.NewDiscardSnapshotStore()
	ra, err := raft.NewRaft(rCfg, (*fsm)(db),
		raft.NewInmemStore(), raft.NewInmemStore(), dss, it,
	)
	if err != nil {
		t.Fatal(err)
	}
	db.raft = ra
	if f := db.raft.BootstrapCluster(raft.Configuration{
		Servers: []raft.Server{
			{
				ID:      rCfg.LocalID,
				Address: it.LocalAddr(),
			},
		},
	}); f.Error() != nil {
		t.Fatal(f.Error())
	}
	time.Sleep(250 * time.Millisecond)

	return db
}
