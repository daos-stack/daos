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
	"io"
	"time"

	"github.com/hashicorp/raft"
	"github.com/pkg/errors"
)

// This file contains the "guts" of the new MS database. The basic theory
// is that our MS database can be modeled as a FSM, with every modification
// captured by a discrete log entry. These logs are distributed to a set of
// replicas, and a given entry is marked committed when a quorum of replicas
// have indicated that it has been persisted.
//
// https://github.com/hashicorp/raft
// https://raft.github.io/
// https://www.hashicorp.com/resources/raft-consul-consensus-protocol-explained

const (
	raftOpAddMember raftOp = iota + 1
	raftOpUpdateMember
	raftOpRemoveMember
	raftOpAddPoolService
	raftOpUpdatePoolService
	raftOpRemovePoolService

	// raftTimeout sets an upper limit for how long an Apply
	// operation may take (TODO: tuning required?).
	raftTimeout = 1 * time.Second
)

type (
	raftOp uint32

	// raftUpdate provides some metadata for an update operation.
	// The data is an opaque blob to raft.
	raftUpdate struct {
		Time time.Time
		Op   raftOp
		Data json.RawMessage
	}

	// memberUpdate provides some metadata for a membership update. In
	// particular, it specifies whether or not the NextRank counter should
	// be incremented in order for the next new member to receive a unique rank.
	memberUpdate struct {
		Member   *Member
		NextRank bool
	}
)

func (ro raftOp) String() string {
	return [...]string{
		"noop",
		"addMember",
		"updateMember",
		"removeMember",
	}[ro]
}

// createRaftUpdate serializes the inner payload and then wraps
// it with a *raftUpdate that is submitted to the raft service.
func createRaftUpdate(op raftOp, inner interface{}) ([]byte, error) {
	data, err := json.Marshal(inner)
	if err != nil {
		return nil, err
	}
	return json.Marshal(&raftUpdate{
		Time: time.Now(),
		Op:   op,
		Data: data,
	})
}

// submitMemberUpdate submits the given member update operation to
// the raft service.
func (db *Database) submitMemberUpdate(op raftOp, m *memberUpdate) error {
	data, err := createRaftUpdate(op, m)
	if err != nil {
		return err
	}
	return db.submitRaftUpdate(data)
}

// submitPoolUpdate submits the given pool service update operation to
// the raft service.
func (db *Database) submitPoolUpdate(op raftOp, ps *PoolService) error {
	data, err := createRaftUpdate(op, ps)
	if err != nil {
		return err
	}
	return db.submitRaftUpdate(data)
}

// submitRaftUpdate submits the serialized operation to the raft service.
func (db *Database) submitRaftUpdate(data []byte) error {
	return db.raft.Apply(data, raftTimeout).Error()
}

// Everything above here happens on the current leader.
//
// Everything below here happens on N replicas.

// NB: This type alias allows us to use a Database object as a raft.FSM.
type fsm Database

// Apply is called after the log entry has been committed. This is the
// only place that direct modification of the data should occur.
//
// NB: Per Hashicorp (https://github.com/hashicorp/raft/issues/307),
// the only reasonable response to an Apply() failure is to panic,
// because the Raft algorithm does not specify a recovery mechanism
// for this scenario.
//
// TODO: This approach feels too heavy-handed, given that the control
// plane is responsible for more than just hosting the raft service.
// It's not clear how we can meaningfully handle errors though, and it
// seems risky to allow a replica to continue in an indeterminite state.
// For the moment, let's just use the nuclear option on the theory that
// these are "can't happen" errors, e.g. ENOMEM or corrupt snapshot.
func (f *fsm) Apply(l *raft.Log) interface{} {
	c := new(raftUpdate)
	if err := json.Unmarshal(l.Data, c); err != nil {
		panic(errors.Wrapf(err, "failed to unmarshal %+v", l.Data))
	}

	switch c.Op {
	case raftOpAddMember, raftOpUpdateMember, raftOpRemoveMember:
		f.data.applyMemberUpdate(c.Op, c.Data)
	case raftOpAddPoolService, raftOpUpdatePoolService, raftOpRemovePoolService:
		f.data.applyPoolUpdate(c.Op, c.Data)
	default:
		panic(errors.Errorf("unhandled Apply operation: %d", c.Op))
	}

	return nil
}

// applyMemberUpdate is responsible for applying the membership update
// operation to the database.
func (d *dbData) applyMemberUpdate(op raftOp, data []byte) {
	m := new(memberUpdate)
	if err := json.Unmarshal(data, m); err != nil {
		panic(errors.Wrap(err, "failed to decode member update"))
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpAddMember:
		d.Members.addMember(m.Member)
	case raftOpUpdateMember:
		cur, found := d.Members.Uuids[m.Member.UUID]
		if !found {
			panic(errors.Errorf("member update for unknown member %+v", m))
		}
		cur.state = m.Member.state
		cur.Info = m.Member.Info
	case raftOpRemoveMember:
		d.Members.removeMember(m.Member)
	default:
		panic(errors.Errorf("unhandled Member Apply operation: %d", op))
	}

	if m.NextRank {
		d.NextRank++
	}
	d.MapVersion++
}

// applyPoolUpdate is responsible for applying the pool service update
// operation to the database.
func (d *dbData) applyPoolUpdate(op raftOp, data []byte) {
	ps := new(PoolService)
	if err := json.Unmarshal(data, ps); err != nil {
		panic(errors.Wrap(err, "failed to decode pool service update"))
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpAddPoolService:
		d.Pools.addService(ps)
	case raftOpUpdatePoolService:
		cur, found := d.Pools.Uuids[ps.PoolUUID]
		if !found {
			panic(errors.Errorf("pool service update for unknown pool %+v", ps))
		}
		cur.State = ps.State
		cur.Replicas = ps.Replicas
	case raftOpRemovePoolService:
		d.Pools.removeService(ps)
	default:
		panic(errors.Errorf("unhandled Pool Service Apply operation: %d", op))
	}

	d.MapVersion++
}

// Snapshot is called to support log compaction, so that we don't have to keep
// every log entry from the start of the system. Instead, the raft service periodically
// creates a point-in-time snapshot which can be used to restore the current state, or
// to efficiently catch up a peer.
func (f *fsm) Snapshot() (raft.FSMSnapshot, error) {
	f.data.Lock()
	defer f.data.Unlock()

	data, err := json.Marshal(f.data)
	if err != nil {
		return nil, err
	}

	f.log.Debugf("created raft db snapshot (map version %d)", f.data.MapVersion)
	return &fsmSnapshot{data}, nil
}

// Restore is called to force the FSM to read in a snapshot, discarding any previous state.
func (f *fsm) Restore(rc io.ReadCloser) error {
	data := NewDatabase(nil, nil).data
	if err := json.NewDecoder(rc).Decode(data); err != nil {
		return err
	}

	if data.SchemaVersion != CurrentSchemaVersion {
		return errors.Errorf("restored schema version %d != %d",
			data.SchemaVersion, CurrentSchemaVersion)
	}

	f.data.Members = data.Members
	f.data.Pools = data.Pools
	f.data.NextRank = data.NextRank
	f.data.MapVersion = data.MapVersion
	f.log.Debugf("db snapshot loaded (map version %d)", data.MapVersion)
	return nil
}

// fsmSnapshot implements the raft.FSMSnapshot interface, and is used
// to persist the snapshot to an io.WriteCloser.
type fsmSnapshot struct {
	data []byte
}

// Persist writes the snapshot to the supplied raft.SnapshotSink.
func (f *fsmSnapshot) Persist(sink raft.SnapshotSink) error {
	err := func() error {
		if _, err := sink.Write(f.data); err != nil {
			return err
		}

		return sink.Close()
	}()

	if err != nil {
		_ = sink.Cancel()
	}

	return err
}

// Release is a no-op for this implementation.
func (f *fsmSnapshot) Release() {}
