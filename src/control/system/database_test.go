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
	"bytes"
	"context"
	"io"
	"io/ioutil"
	"math/rand"
	"net"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/hashicorp/raft"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func setupTestDatabase(t *testing.T, log logging.Logger, replicas []string) (*Database, func()) {
	testDir, cleanup := common.CreateTestDir(t)

	db := NewDatabase(log, &DatabaseConfig{
		RaftDir:  testDir + "/raft",
		Replicas: replicas,
	})
	return db, cleanup
}

func waitForLeadership(t *testing.T, ctx context.Context, db *Database, gained bool, timeout time.Duration) {
	t.Helper()
	timer := time.NewTimer(timeout)
	for {
		select {
		case <-ctx.Done():
			t.Fatal(ctx.Err())
			return
		case <-timer.C:
			t.Fatal("leadership state did not change before timeout")
			return
		default:
			if db.IsLeader() == gained {
				return
			}
			time.Sleep(1 * time.Second)
		}
	}
}

func TestSystem_Database_checkReplica(t *testing.T) {
	for name, tc := range map[string]struct {
		replicas     []string
		controlAddr  *net.TCPAddr
		expRepAddr   *net.TCPAddr
		expBootstrap bool
		expErr       error
	}{
		"not replica": {
			replicas:    []string{mockControlAddr(t, 2).String()},
			controlAddr: mockControlAddr(t, 1),
		},
		"replica, no bootstrap": {
			replicas: []string{
				mockControlAddr(t, 2).String(),
				mockControlAddr(t, 1).String(),
			},
			controlAddr: mockControlAddr(t, 1),
			expRepAddr:  mockControlAddr(t, 1),
		},
		"replica, bootstrap": {
			replicas: []string{
				mockControlAddr(t, 1).String(),
			},
			controlAddr:  mockControlAddr(t, 1),
			expRepAddr:   mockControlAddr(t, 1),
			expBootstrap: true,
		},
		"replica, unspecified control address": {
			replicas: []string{
				mockControlAddr(t, 1).String(),
			},
			controlAddr: &net.TCPAddr{
				IP:   net.IPv4zero,
				Port: build.DefaultControlPort,
			},
			expRepAddr:   mockControlAddr(t, 1),
			expBootstrap: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			db, cleanup := setupTestDatabase(t, log, tc.replicas)
			defer cleanup()

			repAddr, isBootstrap, gotErr := db.checkReplica(tc.controlAddr)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// Just to get a bit of extra coverage
			if repAddr != nil {
				db.replicaAddr = repAddr
				var err error
				repAddr, err = db.ReplicaAddr()
				if err != nil {
					t.Fatal(err)
				}
			} else {
				_, err := db.ReplicaAddr()
				common.CmpErr(t, &ErrNotReplica{tc.replicas}, err)
			}

			if diff := cmp.Diff(tc.expRepAddr, repAddr); diff != "" {
				t.Fatalf("unexpected repAddr (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expBootstrap, isBootstrap); diff != "" {
				t.Fatalf("unexpected isBootstrap (-want, +got)\n:%s\n", diff)
			}
		})
	}
}

func TestSystem_Database_Cancel(t *testing.T) {
	localhost := &net.TCPAddr{
		IP: net.IPv4(127, 0, 0, 1),
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	dbCtx, dbCancel := context.WithCancel(ctx)

	db, cleanup := setupTestDatabase(t, log, []string{localhost.String()})
	defer cleanup()
	if err := db.Start(dbCtx, localhost); err != nil {
		t.Fatal(err)
	}

	waitForLeadership(t, ctx, db, true, 5*time.Second)
	dbCancel()
	waitForLeadership(t, ctx, db, false, 5*time.Second)
}

func replicaGen(ctx context.Context, maxRanks, maxReplicas int) chan []Rank {
	makeReplicas := func() (replicas []Rank) {
		for i := 0; i < rand.Intn(maxReplicas); i++ {
			replicas = append(replicas, Rank(rand.Intn(maxRanks)))
		}
		return
	}

	ch := make(chan []Rank)
	go func() {
		replicas := makeReplicas()
		for {
			select {
			case <-ctx.Done():
				close(ch)
				return
			case ch <- replicas:
				replicas = makeReplicas()
			}
		}
	}()

	return ch
}

func ctrlAddrGen(ctx context.Context, start net.IP, reqsPerAddr int) chan *net.TCPAddr {
	ch := make(chan *net.TCPAddr)
	go func() {
		sent := 0
		cur := start
		for {
			select {
			case <-ctx.Done():
				close(ch)
				return
			case ch <- &net.TCPAddr{IP: cur, Port: build.DefaultControlPort}:
				sent++
				if sent < reqsPerAddr {
					continue
				}
				tmp := cur.To4()
				val := uint(tmp[0])<<24 + uint(tmp[1])<<16 + uint(tmp[2])<<8 + uint(tmp[3])
				val += 1
				d := byte(val & 0xFF)
				c := byte((val >> 8) & 0xFF)
				b := byte((val >> 16) & 0xFF)
				a := byte((val >> 24) & 0xFF)
				cur = net.IPv4(a, b, c, d)
				sent = 0
			}
		}
	}()

	return ch
}

type testSnapshotSink struct {
	contents *bytes.Buffer
}

func (tss *testSnapshotSink) Cancel() error { return nil }
func (tss *testSnapshotSink) Close() error  { return nil }
func (tss *testSnapshotSink) ID() string    { return "test" }
func (tss *testSnapshotSink) Write(data []byte) (int, error) {
	if tss.contents == nil {
		tss.contents = &bytes.Buffer{}
	}
	w, err := io.Copy(tss.contents, bytes.NewReader(data))
	return int(w), err
}
func (tss *testSnapshotSink) Reader() io.ReadCloser {
	return ioutil.NopCloser(tss.contents)
}

func TestSystem_Database_SnapshotRestore(t *testing.T) {
	maxRanks := 2048
	maxPools := 1024

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	db0, cleanup0 := setupTestDatabase(t, log, nil)
	defer cleanup0()

	nextAddr := ctrlAddrGen(ctx, net.IPv4(127, 0, 0, 1), 4)
	replicas := replicaGen(ctx, maxRanks, 5)
	for i := 0; i < maxRanks; i++ {
		mu := &memberUpdate{
			Member: &Member{
				Rank:  Rank(i),
				UUID:  uuid.New(),
				Addr:  <-nextAddr,
				state: MemberStateJoined,
			},
			NextRank: true,
		}
		data, err := createRaftUpdate(raftOpAddMember, mu)
		if err != nil {
			t.Fatal(err)
		}
		rl := &raft.Log{
			Data: data,
		}
		(*fsm)(db0).Apply(rl)
	}

	for i := 0; i < maxPools; i++ {
		ps := &PoolService{
			PoolUUID: uuid.New(),
			State:    PoolServiceStateReady,
			Replicas: <-replicas,
		}
		data, err := createRaftUpdate(raftOpAddPoolService, ps)
		if err != nil {
			t.Fatal(err)
		}
		rl := &raft.Log{
			Data: data,
		}
		(*fsm)(db0).Apply(rl)
	}

	snap, err := (*fsm)(db0).Snapshot()
	if err != nil {
		t.Fatal(err)
	}
	sink := &testSnapshotSink{}
	if err := snap.Persist(sink); err != nil {
		t.Fatal(err)
	}

	db1, cleanup1 := setupTestDatabase(t, log, nil)
	defer cleanup1()

	if err := (*fsm)(db1).Restore(sink.Reader()); err != nil {
		t.Fatal(err)
	}

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(dbData{}, Member{}),
		cmpopts.IgnoreFields(dbData{}, "RWMutex"),
	}
	if diff := cmp.Diff(db0.data, db1.data, cmpOpts...); diff != "" {
		t.Fatalf("db differs after restore (-want, +got):\n%s\n", diff)
	}
}

func TestSystem_Database_SnapshotRestoreBadVersion(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	db0, cleanup0 := setupTestDatabase(t, log, nil)
	defer cleanup0()
	db0.data.SchemaVersion = 1024 // arbitrarily large, should never get here

	snap, err := (*fsm)(db0).Snapshot()
	if err != nil {
		t.Fatal(err)
	}
	sink := &testSnapshotSink{}
	if err := snap.Persist(sink); err != nil {
		t.Fatal(err)
	}

	db1, cleanup1 := setupTestDatabase(t, log, nil)
	defer cleanup1()

	wantErr := errors.Errorf("%d != %d", db0.data.SchemaVersion, CurrentSchemaVersion)
	gotErr := (*fsm)(db1).Restore(sink.Reader())
	common.CmpErr(t, wantErr, gotErr)
}

func TestSystem_Database_BadApply(t *testing.T) {
	makePayload := func(t *testing.T, op raftOp, inner interface{}) []byte {
		t.Helper()
		data, err := createRaftUpdate(op, inner)
		if err != nil {
			t.Fatal(err)
		}
		return data
	}

	for name, tc := range map[string]struct {
		payload []byte
	}{
		"nil payload": {},
		"garbage payload": {
			payload: []byte{0, 1, 2, 3, 4},
		},
		"unknown op": {
			payload: makePayload(t, 42, &PoolService{}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			db0, cleanup0 := setupTestDatabase(t, log, nil)
			defer cleanup0()

			defer func() {
				if r := recover(); r == nil {
					t.Fatal("expected panic in Apply()")
				}
			}()

			rl := &raft.Log{
				Data: tc.payload,
			}
			(*fsm)(db0).Apply(rl)
		})
	}
}
