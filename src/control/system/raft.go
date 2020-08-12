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
	raftOpAddMember raftOp = iota + 1
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

func (db *Database) submitMemberUpdate(op raftOp, m *Member) error {
	data, err := json.Marshal(m)
	if err != nil {
		return err
	}
	return db.submitRaftUpdate(op, data)
}

func (db *Database) submitPoolUpdate(op raftOp, ps *PoolService) error {
	data, err := json.Marshal(ps)
	if err != nil {
		return err
	}
	return db.submitRaftUpdate(op, data)
}

func (db *Database) submitRaftUpdate(op raftOp, data []byte) error {
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

func (d *dbData) applyMemberUpdate(op raftOp, data []byte) {
	m := new(Member)
	if err := json.Unmarshal(data, m); err != nil {
		d.log.Errorf("failed to decode member update: %s", err)
		return
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpAddMember:
		if !m.Rank.Equals(d.NextRank) {
			d.log.Errorf("unexpected new member rank (%d != %d)", m.Rank, d.NextRank)
		}
		d.NextRank++
		d.Members.addMember(m)
		d.log.Debugf("added member: %+v", m)
	case raftOpUpdateMember:
		cur, found := d.Members.Uuids[m.UUID]
		if !found {
			d.log.Errorf("member update for unknown member %+v", m)
		}
		cur.state = m.state
		cur.Info = m.Info
		d.log.Debugf("updated member: %+v", m)
	case raftOpRemoveMember:
		d.Members.removeMember(m)
		d.log.Debugf("removed %+v", m)
	default:
		d.log.Errorf("unhandled Member Apply operation: %d", op)
		return
	}

	d.MapVersion++
}

func (d *dbData) applyPoolUpdate(op raftOp, data []byte) {
	ps := new(PoolService)
	if err := json.Unmarshal(data, ps); err != nil {
		d.log.Errorf("failed to decode pool service update: %s", err)
		return
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpAddPoolService:
		d.Pools.addService(ps)
		d.log.Debugf("added pool service: %+v", ps)
	case raftOpUpdatePoolService:
		cur, found := d.Pools.Uuids[ps.PoolUUID]
		if !found {
			d.log.Errorf("pool service update for unknown pool %+v", ps)
		}
		cur.State = ps.State
		cur.Replicas = ps.Replicas
		d.log.Debugf("updated pool service: %+v", ps)
	case raftOpRemovePoolService:
		d.Pools.removeService(ps)
		d.log.Debugf("removed %+v", ps)
	default:
		d.log.Errorf("unhandled Pool Service Apply operation: %d", op)
		return
	}

	d.MapVersion++
}

type fsm Database

func (f *fsm) Apply(l *raft.Log) interface{} {
	c := new(raftUpdate)
	if err := json.Unmarshal(l.Data, c); err != nil {
		f.log.Errorf("failed to unmarshal %+v: %s", l.Data, err)
		return nil
	}

	f.log.Debugf("applying log: %+v", string(l.Data))

	switch c.Op {
	case raftOpAddMember, raftOpUpdateMember, raftOpRemoveMember:
		f.data.applyMemberUpdate(c.Op, c.Data)
	case raftOpAddPoolService, raftOpUpdatePoolService, raftOpRemovePoolService:
		f.data.applyPoolUpdate(c.Op, c.Data)
	default:
		f.log.Errorf("unhandled Apply operation: %d", c.Op)
	}

	return nil
}

func (f *fsm) Snapshot() (raft.FSMSnapshot, error) {
	f.data.Lock()
	defer f.data.Unlock()

	data, err := json.Marshal(f.data)
	if err != nil {
		return nil, err
	}

	f.log.Debugf("db snapshot: %+v", string(data))
	f.log.Infof("created raft db snapshot (map version %d)", f.data.MapVersion)
	return &fsmSnapshot{data}, nil
}

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
		_ = sink.Cancel()
	}

	return err
}

func (f *fsmSnapshot) Release() {}
