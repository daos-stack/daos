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
	"fmt"
	"io"
	"io/ioutil"
	"math/rand"
	"net"
	"sort"
	"strings"
	"sync/atomic"
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

func waitForLeadership(t *testing.T, ctx context.Context, db *Database, gained bool, timeout time.Duration) {
	t.Helper()
	timer := time.NewTimer(timeout)
	for {
		select {
		case <-ctx.Done():
			t.Fatal(ctx.Err())
			return
		case <-timer.C:
			state := "gained"
			if !gained {
				state = "lost"
			}
			t.Fatalf("leadership was not %s before timeout", state)
			return
		default:
			if db.IsLeader() == gained {
				return
			}
			time.Sleep(1 * time.Second)
		}
	}
}

func TestSystem_Database_filterMembers(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	db := MockDatabase(t, log)
	memberStates := []MemberState{
		MemberStateUnknown, MemberStateAwaitFormat, MemberStateStarting,
		MemberStateReady, MemberStateJoined, MemberStateStopping, MemberStateStopped,
		MemberStateEvicted, MemberStateErrored, MemberStateUnresponsive,
	}

	for i, ms := range memberStates {
		if err := db.AddMember(MockMember(t, uint32(i), ms)); err != nil {
			t.Fatal(err)
		}
	}

	for name, tf := range map[string]func(t *testing.T){
		"individual state filters": func(t *testing.T) {
			for _, ms := range memberStates {
				matches := db.filterMembers(ms)
				matchLen := len(matches)
				if matchLen != 1 {
					t.Fatalf("expected exactly 1 member to match %s (got %d)", ms, matchLen)
				}
				if matches[0].state != ms {
					t.Fatalf("filtered member doesn't match requested state (%s != %s)", matches[0].state, ms)
				}
			}
		},
		"all members filter": func(t *testing.T) {
			matchLen := len(db.filterMembers(AllMemberFilter))
			if matchLen != len(memberStates) {
				t.Fatalf("expected all members to be %d; got %d", len(memberStates), matchLen)
			}
		},
		"subset filter": func(t *testing.T) {
			filter := []MemberState{memberStates[1], memberStates[2]}
			matches := db.filterMembers(filter...)
			matchLen := len(matches)
			if matchLen != 2 {
				t.Fatalf("expected 2 members to match; got %d", matchLen)
			}

			// sort the results for stable comparison
			sort.Slice(matches, func(i, j int) bool { return matches[i].state < matches[j].state })
			for i, ms := range filter {
				if matches[i].state != ms {
					t.Fatalf("filtered member %d doesn't match requested state (%s != %s)", i, matches[i].state, ms)
				}
			}
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			tf(t)
		})
	}
}

func TestSystem_Database_Cancel(t *testing.T) {
	localhost := common.LocalhostCtrlAddr()
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	dbCtx, dbCancel := context.WithCancel(ctx)

	db, cleanup := TestDatabase(t, log, localhost)
	defer cleanup()
	if err := db.Start(dbCtx); err != nil {
		t.Fatal(err)
	}

	var onGainedCalled, onLostCalled uint32
	db.OnLeadershipGained(func(_ context.Context) error {
		atomic.StoreUint32(&onGainedCalled, 1)
		return nil
	})
	db.OnLeadershipLost(func() error {
		atomic.StoreUint32(&onLostCalled, 1)
		return nil
	})

	waitForLeadership(t, ctx, db, true, 10*time.Second)
	dbCancel()
	waitForLeadership(t, ctx, db, false, 10*time.Second)

	if atomic.LoadUint32(&onGainedCalled) != 1 {
		t.Fatal("OnLeadershipGained callbacks didn't execute")
	}
	if atomic.LoadUint32(&onLostCalled) != 1 {
		t.Fatal("OnLeadershipLost callbacks didn't execute")
	}
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

	db0, cleanup0 := TestDatabase(t, log, nil)
	defer cleanup0()

	nextAddr := ctrlAddrGen(ctx, net.IPv4(127, 0, 0, 1), 4)
	replicas := replicaGen(ctx, maxRanks, 5)
	for i := 0; i < maxRanks; i++ {
		mu := &memberUpdate{
			Member: &Member{
				Rank:        Rank(i),
				UUID:        uuid.New(),
				Addr:        <-nextAddr,
				state:       MemberStateJoined,
				FaultDomain: MustCreateFaultDomainFromString("/my/test/domain"),
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
			PoolUUID:  uuid.New(),
			PoolLabel: fmt.Sprintf("pool%04d", i),
			State:     PoolServiceStateReady,
			Replicas:  <-replicas,
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

	db1, cleanup1 := TestDatabase(t, log, nil)
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

	db0, cleanup0 := TestDatabase(t, log, nil)
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

	db1, cleanup1 := TestDatabase(t, log, nil)
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

			db := MockDatabase(t, log)
			rl := &raft.Log{
				Data: tc.payload,
			}
			(*fsm)(db).Apply(rl)

			if !strings.Contains(buf.String(), "SHUTDOWN") {
				t.Fatal("expected an emergency shutdown, but didn't see one")
			}
		})
	}
}

func raftUpdateTestMember(t *testing.T, db *Database, op raftOp, member *Member) {
	t.Helper()

	mu := &memberUpdate{
		Member: member,
	}
	if op == raftOpAddMember {
		mu.NextRank = true
	}
	data, err := createRaftUpdate(op, mu)
	if err != nil {
		t.Fatal(err)
	}
	rl := &raft.Log{
		Data: data,
	}
	(*fsm)(db).Apply(rl)
}

func TestSystem_Database_memberRaftOps(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	testMembers := make([]*Member, 0)
	nextAddr := ctrlAddrGen(ctx, net.IPv4(127, 0, 0, 1), 4)
	for i := 0; i < 3; i++ {
		testMembers = append(testMembers, &Member{
			Rank:        Rank(i),
			UUID:        uuid.New(),
			Addr:        <-nextAddr,
			state:       MemberStateJoined,
			FaultDomain: MustCreateFaultDomainFromString("/rack0"),
		})
	}

	changedFaultDomainMember := &Member{
		Rank:        testMembers[1].Rank,
		UUID:        testMembers[1].UUID,
		Addr:        testMembers[1].Addr,
		state:       testMembers[1].state,
		FaultDomain: MustCreateFaultDomainFromString("/rack1"),
	}

	cmpOpts := []cmp.Option{
		cmp.AllowUnexported(Member{}),
	}

	for name, tc := range map[string]struct {
		startingMembers []*Member
		op              raftOp
		updateMember    *Member
		expMembers      []*Member
		expFDTree       *FaultDomainTree
	}{
		"add success": {
			op:           raftOpAddMember,
			updateMember: testMembers[0],
			expMembers: []*Member{
				testMembers[0],
			},
			expFDTree: NewFaultDomainTree(testMembers[0].RankFaultDomain()),
		},
		"update state success": {
			startingMembers: testMembers,
			op:              raftOpUpdateMember,
			updateMember: &Member{
				Rank:        testMembers[1].Rank,
				UUID:        testMembers[1].UUID,
				Addr:        testMembers[1].Addr,
				state:       MemberStateStopped,
				FaultDomain: testMembers[1].FaultDomain,
			},
			expMembers: []*Member{
				testMembers[0],
				{
					Rank:        testMembers[1].Rank,
					UUID:        testMembers[1].UUID,
					Addr:        testMembers[1].Addr,
					state:       MemberStateStopped,
					FaultDomain: testMembers[1].FaultDomain,
				},
				testMembers[2],
			},

			expFDTree: NewFaultDomainTree(
				testMembers[0].RankFaultDomain(),
				testMembers[1].RankFaultDomain(),
				testMembers[2].RankFaultDomain()),
		},
		"update fault domain success": {
			startingMembers: testMembers,
			op:              raftOpUpdateMember,
			updateMember:    changedFaultDomainMember,
			expMembers: []*Member{
				testMembers[0],
				changedFaultDomainMember,
				testMembers[2],
			},
			expFDTree: NewFaultDomainTree(
				testMembers[0].RankFaultDomain(),
				changedFaultDomainMember.RankFaultDomain(),
				testMembers[2].RankFaultDomain()),
		},
		"remove success": {
			startingMembers: testMembers,
			op:              raftOpRemoveMember,
			updateMember:    testMembers[2],
			expMembers: []*Member{
				testMembers[0],
				testMembers[1],
			},
			expFDTree: NewFaultDomainTree(
				testMembers[0].RankFaultDomain(),
				testMembers[1].RankFaultDomain()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)

			// setup initial member DB
			for _, initMember := range tc.startingMembers {
				raftUpdateTestMember(t, db, raftOpAddMember, initMember)
			}

			// Update the member
			raftUpdateTestMember(t, db, tc.op, tc.updateMember)

			// Check member DB was updated
			for _, expMember := range tc.expMembers {
				uuidM, ok := db.data.Members.Uuids[expMember.UUID]
				if !ok {
					t.Errorf("member not found for UUID %s", expMember.UUID)
				}
				if diff := cmp.Diff(expMember, uuidM, cmpOpts...); diff != "" {
					t.Fatalf("member wrong in UUID DB (-want, +got):\n%s\n", diff)
				}

				rankM, ok := db.data.Members.Ranks[expMember.Rank]
				if !ok {
					t.Errorf("member not found for rank %d", expMember.Rank)
				}
				if diff := cmp.Diff(expMember, rankM, cmpOpts...); diff != "" {
					t.Fatalf("member wrong in rank DB (-want, +got):\n%s\n", diff)
				}

				addrMs, ok := db.data.Members.Addrs[expMember.Addr.String()]
				if !ok {
					t.Errorf("slice not found for addr %s", expMember.Addr.String())
				}

				found := false
				for _, am := range addrMs {
					if am.Rank == expMember.Rank {
						found = true
						if diff := cmp.Diff(expMember, am, cmpOpts...); diff != "" {
							t.Fatalf("member wrong in addr DB (-want, +got):\n%s\n", diff)
						}
					}
				}
				if !found {
					t.Fatalf("expected member %+v not found for addr %s", expMember, expMember.Addr.String())
				}

			}
			if len(db.data.Members.Uuids) != len(tc.expMembers) {
				t.Fatalf("expected %d members, got %d", len(tc.expMembers), len(db.data.Members.Uuids))
			}

			if diff := cmp.Diff(tc.expFDTree, db.data.Members.FaultDomains); diff != "" {
				t.Fatalf("wrong FaultDomainTree in DB (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Database_FaultDomainTree(t *testing.T) {
	for name, tc := range map[string]struct {
		fdTree *FaultDomainTree
	}{
		"nil": {},
		"actual tree": {
			fdTree: NewFaultDomainTree(
				MustCreateFaultDomain("one", "two", "three"),
				MustCreateFaultDomain("one", "two", "four"),
				MustCreateFaultDomain("five", "six", "seven"),
				MustCreateFaultDomain("five", "eight", "nine"),
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			db.data.Members.FaultDomains = tc.fdTree

			result := db.FaultDomainTree()

			if diff := cmp.Diff(tc.fdTree, result); diff != "" {
				t.Fatalf("(-want, +got):\n%s\n", diff)
			}
		})
	}
}
