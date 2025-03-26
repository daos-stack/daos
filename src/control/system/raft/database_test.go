//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"bytes"
	"context"
	"fmt"
	"io"
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
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	. "github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func waitForLeadership(ctx context.Context, t *testing.T, db *Database, gained bool) {
	t.Helper()
	for {
		select {
		case <-ctx.Done():
			t.Fatal(ctx.Err())
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
	defer test.ShowBufferOnFailure(t, buf)

	db := MockDatabase(t, log)
	memberStates := []MemberState{
		MemberStateUnknown, MemberStateAwaitFormat, MemberStateStarting,
		MemberStateReady, MemberStateJoined, MemberStateStopping, MemberStateStopped,
		MemberStateExcluded, MemberStateErrored, MemberStateUnresponsive, MemberStateAdminExcluded,
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
				if matches[0].State != ms {
					t.Fatalf("filtered member doesn't match requested state (%s != %s)", matches[0].State, ms)
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
			sort.Slice(matches, func(i, j int) bool { return matches[i].State < matches[j].State })
			for i, ms := range filter {
				if matches[i].State != ms {
					t.Fatalf("filtered member %d doesn't match requested state (%s != %s)", i, matches[i].State, ms)
				}
			}
		},
		"nonexcluded filter": func(t *testing.T) {
			matches := db.filterMembers(NonExcludedMemberFilter)
			matchLen := len(matches)
			if matchLen != len(memberStates)-4 {
				t.Fatalf("expected %d members to match; got %d", len(memberStates)-4, matchLen)
			}
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			tf(t)
		})
	}
}

func TestSystem_Database_LeadershipCallbacks(t *testing.T) {
	localhost := common.LocalhostCtrlAddr()
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := test.Context(t)
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

	waitForLeadership(ctx, t, db, true)
	dbCancel()
	waitForLeadership(ctx, t, db, false)

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
				val++
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
	return io.NopCloser(tss.contents)
}

func TestSystem_Database_SnapshotRestore(t *testing.T) {
	maxRanks := 2048
	maxPools := 1024
	maxAttrs := 4096
	maxFindings := 512

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := test.Context(t)

	db0, cleanup0 := TestDatabase(t, log)
	defer cleanup0()

	nextAddr := ctrlAddrGen(ctx, net.IPv4(127, 0, 0, 1), 4)
	replicas := replicaGen(ctx, maxRanks, 5)
	for i := 0; i < maxRanks; i++ {
		mu := &memberUpdate{
			Member: &Member{
				Rank:        Rank(i),
				UUID:        uuid.New(),
				Addr:        <-nextAddr,
				State:       MemberStateJoined,
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
			State:     system.PoolServiceStateReady,
			Replicas:  <-replicas,
			Storage: &PoolServiceStorage{
				CreationRankStr:    fmt.Sprintf("[0-%d]", maxRanks),
				PerRankTierStorage: []uint64{1, 2},
			},
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

	for i := 0; i < maxFindings; i++ {
		f := checker.MockFinding(i)
		data, err := createRaftUpdate(raftOpAddCheckerFinding, f)
		if err != nil {
			t.Fatal(err)
		}
		rl := &raft.Log{
			Data: data,
		}
		(*fsm)(db0).Apply(rl)
	}

	attrs := make(map[string]string)
	for i := 0; i < maxAttrs; i++ {
		attrs[fmt.Sprintf("prop%04d", i)] = fmt.Sprintf("value%04d", i)
	}
	data, err := createRaftUpdate(raftOpUpdateSystemAttrs, attrs)
	if err != nil {
		t.Fatal(err)
	}
	rl := &raft.Log{
		Data: data,
	}
	(*fsm)(db0).Apply(rl)

	snap, err := (*fsm)(db0).Snapshot()
	if err != nil {
		t.Fatal(err)
	}
	sink := &testSnapshotSink{}
	if err := snap.Persist(sink); err != nil {
		t.Fatal(err)
	}

	db1, cleanup1 := TestDatabase(t, log)
	defer cleanup1()

	if err := (*fsm)(db1).Restore(sink.Reader()); err != nil {
		t.Fatal(err)
	}

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(dbData{}, Member{}, PoolServiceStorage{}),
		cmpopts.IgnoreFields(dbData{}, "RWMutex"),
		cmpopts.IgnoreFields(PoolServiceStorage{}, "Mutex"),
		protocmp.Transform(),
	}
	if diff := cmp.Diff(db0.data, db1.data, cmpOpts...); diff != "" {
		t.Fatalf("db differs after restore (-want, +got):\n%s\n", diff)
	}
}

func TestSystem_Database_SnapshotRestoreBadVersion(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	db0, cleanup0 := TestDatabase(t, log)
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

	db1, cleanup1 := TestDatabase(t, log)
	defer cleanup1()

	wantErr := errors.Errorf("%d != %d", db0.data.SchemaVersion, CurrentSchemaVersion)
	gotErr := (*fsm)(db1).Restore(sink.Reader())
	test.CmpErr(t, wantErr, gotErr)
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
			defer test.ShowBufferOnFailure(t, buf)

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

// For tests where the ID is unimportant
func ignoreFaultDomainIDOption() cmp.Option {
	return cmp.FilterPath(
		func(p cmp.Path) bool {
			return p.Last().String() == ".ID"
		}, cmp.Ignore())
}

func TestSystem_Database_memberRaftOps(t *testing.T) {
	ctx := test.Context(t)

	testMembers := make([]*Member, 0)
	nextAddr := ctrlAddrGen(ctx, net.IPv4(127, 0, 0, 1), 4)
	for i := 0; i < 3; i++ {
		testMembers = append(testMembers, &Member{
			Rank:        Rank(i),
			UUID:        uuid.New(),
			Addr:        <-nextAddr,
			State:       MemberStateJoined,
			FaultDomain: MustCreateFaultDomainFromString("/rack0"),
		})
	}

	changedFaultDomainMember := &Member{
		Rank:        testMembers[1].Rank,
		UUID:        testMembers[1].UUID,
		Addr:        testMembers[1].Addr,
		State:       testMembers[1].State,
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
			expFDTree: NewFaultDomainTree(MemberFaultDomain(testMembers[0])),
		},
		"update state success": {
			startingMembers: testMembers,
			op:              raftOpUpdateMember,
			updateMember: &Member{
				Rank:        testMembers[1].Rank,
				UUID:        testMembers[1].UUID,
				Addr:        testMembers[1].Addr,
				State:       MemberStateStopped,
				FaultDomain: testMembers[1].FaultDomain,
			},
			expMembers: []*Member{
				testMembers[0],
				{
					Rank:        testMembers[1].Rank,
					UUID:        testMembers[1].UUID,
					Addr:        testMembers[1].Addr,
					State:       MemberStateStopped,
					FaultDomain: testMembers[1].FaultDomain,
				},
				testMembers[2],
			},
			expFDTree: NewFaultDomainTree(
				MemberFaultDomain(testMembers[0]),
				MemberFaultDomain(testMembers[1]),
				MemberFaultDomain(testMembers[2]),
			),
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
				MemberFaultDomain(testMembers[0]),
				MemberFaultDomain(changedFaultDomainMember),
				MemberFaultDomain(testMembers[2]),
			),
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
				MemberFaultDomain(testMembers[0]),
				MemberFaultDomain(testMembers[1]),
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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

			if diff := cmp.Diff(tc.expFDTree, db.data.Members.FaultDomains, ignoreFaultDomainIDOption()); diff != "" {
				t.Fatalf("wrong FaultDomainTree in DB (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Database_memberFaultDomain(t *testing.T) {
	for name, tc := range map[string]struct {
		rank        Rank
		faultDomain *FaultDomain
		expResult   *FaultDomain
	}{
		"nil fault domain": {
			expResult: MustCreateFaultDomain("rank0"),
		},
		"empty fault domain": {
			rank:        Rank(2),
			faultDomain: MustCreateFaultDomain(),
			expResult:   MustCreateFaultDomain("rank2"),
		},
		"existing fault domain": {
			rank:        Rank(1),
			faultDomain: MustCreateFaultDomain("one", "two"),
			expResult:   MustCreateFaultDomain("one", "two", "rank1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			m := MockMemberFullSpec(t, tc.rank, uuid.New().String(), "dontcare", &net.TCPAddr{},
				MemberStateJoined).WithFaultDomain(tc.faultDomain)
			result := MemberFaultDomain(m)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
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
			defer test.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			db.data.Members.FaultDomains = tc.fdTree

			result := db.FaultDomainTree()

			if diff := cmp.Diff(tc.fdTree, result); diff != "" {
				t.Fatalf("(-want, +got):\n%s\n", diff)
			}

			if result != nil && result == db.data.Members.FaultDomains {
				t.Fatal("expected fault domain tree to be a copy")
			}
		})
	}
}

func TestSystem_Database_SystemAttrs(t *testing.T) {
	for name, tc := range map[string]struct {
		startAttrs  map[string]string
		attrsUpdate map[string]string
		searchKeys  []string
		expAttrs    map[string]string
		expErr      error
	}{
		"add success": {
			startAttrs:  map[string]string{},
			attrsUpdate: map[string]string{"foo": "bar"},
			expAttrs:    map[string]string{"foo": "bar"},
		},
		"remove success": {
			startAttrs:  map[string]string{"bye": "gone"},
			attrsUpdate: map[string]string{"bye": ""},
			expAttrs:    map[string]string{},
		},
		"update success": {
			startAttrs:  map[string]string{"foo": "baz"},
			attrsUpdate: map[string]string{"foo": "bar"},
			expAttrs:    map[string]string{"foo": "bar"},
		},
		"get bad key": {
			startAttrs:  map[string]string{},
			attrsUpdate: map[string]string{"foo": "bar"},
			expAttrs:    map[string]string{"foo": "bar"},
			searchKeys:  []string{"whoops"},
			expErr:      ErrSystemAttrNotFound("whoops"),
		},
		"get good key": {
			startAttrs:  map[string]string{"foo": "bar", "baz": "qux"},
			attrsUpdate: map[string]string{"foo": "quux"},
			expAttrs:    map[string]string{"baz": "qux"},
			searchKeys:  []string{"baz"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)

			db.data.System.Attributes = tc.startAttrs
			db.SetSystemAttrs(tc.attrsUpdate)

			gotAttrs, gotErr := db.GetSystemAttrs(tc.searchKeys, nil)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expAttrs, gotAttrs); diff != "" {
				t.Fatalf("unexpected system properties (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Database_OnEvent(t *testing.T) {
	puuid := uuid.New()
	puuidAnother := uuid.New()

	for name, tc := range map[string]struct {
		poolSvcs    []*PoolService
		event       *events.RASEvent
		expPoolSvcs []*PoolService
	}{
		"nil event": {
			event:       nil,
			expPoolSvcs: []*PoolService{},
		},
		"pool svc replicas update miss": {
			poolSvcs: []*PoolService{
				{
					PoolUUID:   puuid,
					PoolLabel:  "pool0001",
					State:      system.PoolServiceStateReady,
					Replicas:   []Rank{1, 2, 3, 4, 5},
					LastUpdate: time.Now(),
				},
			},
			event: events.NewPoolSvcReplicasUpdateEvent(
				"foo", 1, puuidAnother.String(), []uint32{2, 3, 5, 6, 7}, 1),
			expPoolSvcs: []*PoolService{
				{
					PoolUUID:   puuid,
					PoolLabel:  "pool0001",
					State:      system.PoolServiceStateReady,
					Replicas:   []Rank{1, 2, 3, 4, 5},
					LastUpdate: time.Now(),
				},
			},
		},
		"pool svc replicas update hit": {
			poolSvcs: []*PoolService{
				{
					PoolUUID:   puuid,
					PoolLabel:  "pool0001",
					State:      system.PoolServiceStateReady,
					Replicas:   []Rank{1, 2, 3, 4, 5},
					LastUpdate: time.Now(),
				},
			},
			event: events.NewPoolSvcReplicasUpdateEvent(
				"foo", 1, puuid.String(), []uint32{2, 3, 5, 6, 7}, 1),
			expPoolSvcs: []*PoolService{
				{
					PoolUUID:   puuid,
					PoolLabel:  "pool0001",
					State:      system.PoolServiceStateReady,
					Replicas:   []Rank{2, 3, 5, 6, 7},
					LastUpdate: time.Now(),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx, cancel := context.WithTimeout(test.Context(t), 50*time.Millisecond)
			defer cancel()

			db := MockDatabase(t, log)
			for _, ps := range tc.poolSvcs {
				lock, err := db.TakePoolLock(ctx, ps.PoolUUID)
				if err != nil {
					t.Fatal(err)
				}

				if err := db.AddPoolService(lock.InContext(ctx), ps); err != nil {
					t.Fatal(err)
				}
				lock.Release()
			}

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()

			ps.Subscribe(events.RASTypeAny, db)

			ps.Publish(tc.event)

			<-ctx.Done()

			poolSvcs, err := db.PoolServiceList(false)
			if err != nil {
				t.Fatal(err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(PoolService{}),
				cmpopts.EquateApproxTime(time.Second),
			}
			if diff := cmp.Diff(tc.expPoolSvcs, poolSvcs, cmpOpts...); diff != "" {
				t.Errorf("unexpected pool service replicas (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystemDatabase_PoolServiceList(t *testing.T) {
	ready := &PoolService{
		PoolUUID:   uuid.New(),
		PoolLabel:  "pool0001",
		State:      system.PoolServiceStateReady,
		Replicas:   []Rank{1, 2, 3, 4, 5},
		LastUpdate: time.Now(),
	}
	creating := &PoolService{
		PoolUUID:   uuid.New(),
		PoolLabel:  "pool0002",
		State:      system.PoolServiceStateCreating,
		Replicas:   []Rank{1, 2, 3, 4, 5},
		LastUpdate: time.Now(),
	}
	destroying := &PoolService{
		PoolUUID:   uuid.New(),
		PoolLabel:  "pool0003",
		State:      system.PoolServiceStateDestroying,
		Replicas:   []Rank{1, 2, 3, 4, 5},
		LastUpdate: time.Now(),
	}

	for name, tc := range map[string]struct {
		poolSvcs    []*PoolService
		all         bool
		expPoolSvcs []*PoolService
	}{
		"empty": {
			expPoolSvcs: []*PoolService{},
		},
		"all: false": {
			poolSvcs:    []*PoolService{creating, ready, destroying},
			expPoolSvcs: []*PoolService{ready},
		},
		"all: true": {
			poolSvcs:    []*PoolService{creating, ready, destroying},
			all:         true,
			expPoolSvcs: []*PoolService{creating, ready, destroying},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)
			db := MockDatabase(t, log)
			for _, ps := range tc.poolSvcs {
				lock, err := db.TakePoolLock(ctx, ps.PoolUUID)
				if err != nil {
					t.Fatal(err)
				}

				if err := db.AddPoolService(lock.InContext(ctx), ps); err != nil {
					t.Fatal(err)
				}
				lock.Release()
			}

			poolSvcs, err := db.PoolServiceList(tc.all)
			if err != nil {
				t.Fatal(err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.SortSlices(func(x, y *PoolService) bool {
					return x.PoolLabel < y.PoolLabel
				}),
				cmpopts.IgnoreUnexported(PoolService{}),
				cmpopts.EquateApproxTime(time.Second),
			}
			if diff := cmp.Diff(tc.expPoolSvcs, poolSvcs, cmpOpts...); diff != "" {
				t.Errorf("unexpected pool service replicas (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Database_GroupMap(t *testing.T) {
	membersWithStates := func(states ...MemberState) []*Member {
		members := make([]*Member, len(states))

		for i, ms := range states {
			members[i] = MockMember(t, uint32(i), ms)
		}

		return members
	}
	memberWithNoURI := MockMemberFullSpec(t, 2, test.MockUUID(2), "", MockControlAddr(t, 2),
		MemberStateJoined)

	for name, tc := range map[string]struct {
		members     []*Member
		expGroupMap *GroupMap
		expErr      error
	}{
		"empty membership": {
			expErr: ErrEmptyGroupMap,
		},
		"excluded members not included": {
			// This is a bit fragile, but I don't see a better way to maintain
			// this list. We'll just need to keep it updated as the states change.
			members: membersWithStates(
				MemberStateUnknown,       // rank 0
				MemberStateAwaitFormat,   // rank 1, excluded
				MemberStateStarting,      // rank 2
				MemberStateReady,         // rank 3
				MemberStateJoined,        // rank 4
				MemberStateStopping,      // rank 5
				MemberStateStopped,       // rank 6
				MemberStateExcluded,      // rank 7, excluded
				MemberStateAdminExcluded, // rank 8, excluded
				MemberStateErrored,       // rank 9
				MemberStateUnresponsive,  // rank 10
			),
			expGroupMap: &GroupMap{
				Version: 11,
				RankEntries: map[Rank]RankEntry{
					0: {
						PrimaryURI:     MockControlAddr(t, 0).String(),
						NumPrimaryCtxs: 0,
					},
					2: {
						PrimaryURI:     MockControlAddr(t, 2).String(),
						NumPrimaryCtxs: 2,
					},
					3: {
						PrimaryURI:     MockControlAddr(t, 3).String(),
						NumPrimaryCtxs: 3,
					},
					4: {
						PrimaryURI:     MockControlAddr(t, 4).String(),
						NumPrimaryCtxs: 4,
					},
					5: {
						PrimaryURI:     MockControlAddr(t, 5).String(),
						NumPrimaryCtxs: 5,
					},
					6: {
						PrimaryURI:     MockControlAddr(t, 6).String(),
						NumPrimaryCtxs: 6,
					},
					9: {
						PrimaryURI:     MockControlAddr(t, 9).String(),
						NumPrimaryCtxs: 9,
					},
					10: {
						PrimaryURI:     MockControlAddr(t, 10).String(),
						NumPrimaryCtxs: 10,
					},
				},
			},
		},
		"MS ranks included": {
			members: membersWithStates(MemberStateJoined, MemberStateJoined),
			expGroupMap: &GroupMap{
				Version: 2,
				RankEntries: map[Rank]RankEntry{
					0: {
						PrimaryURI:     MockControlAddr(t, 0).String(),
						NumPrimaryCtxs: 0,
					},
					1: {
						PrimaryURI:     MockControlAddr(t, 1).String(),
						NumPrimaryCtxs: 1,
					},
				},
				MSRanks: []Rank{1},
			},
		},
		"unset fabric URI skipped": {
			members: append([]*Member{
				memberWithNoURI,
			}, membersWithStates(MemberStateJoined)...),
			expGroupMap: &GroupMap{
				Version: 2,
				RankEntries: map[Rank]RankEntry{
					0: {
						PrimaryURI:     MockControlAddr(t, 0).String(),
						NumPrimaryCtxs: 0,
					},
				},
			},
		},
		"secondary URIs": {
			members: []*Member{
				{
					Rank:                  2,
					UUID:                  uuid.MustParse(test.MockUUID(2)),
					PrimaryFabricURI:      MockControlAddr(t, 2).String(),
					PrimaryFabricContexts: 8,
					SecondaryFabricURIs: []string{
						MockControlAddr(t, 3).String(),
						MockControlAddr(t, 4).String(),
					},
					SecondaryFabricContexts: []uint32{4, 6},
					Addr:                    MockControlAddr(t, 2),
					State:                   MemberStateJoined,
					FaultDomain:             MustCreateFaultDomain(),
					LastUpdate:              time.Now(),
				},
			},
			expGroupMap: &GroupMap{
				Version: 1,
				RankEntries: map[Rank]RankEntry{
					2: {
						PrimaryURI:     MockControlAddr(t, 2).String(),
						NumPrimaryCtxs: 8,
						SecondaryURIs: []string{
							MockControlAddr(t, 3).String(),
							MockControlAddr(t, 4).String(),
						},
						NumSecondaryCtxs: []uint32{4, 6},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			for _, m := range tc.members {
				if err := db.AddMember(m); err != nil {
					t.Fatal(err)
				}
			}

			gotGroupMap, gotErr := db.GroupMap()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expGroupMap, gotGroupMap); diff != "" {
				t.Fatalf("unexpected GroupMap (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func Test_Database_ResignLeadership(t *testing.T) {
	for name, tc := range map[string]struct {
		cause     error
		expErr    error
		expLeader bool
	}{
		"nil cause": {
			expLeader: false,
		},
		// For these next errors, we just want to verify that the
		// method exits early, preventing the state from being toggled.
		"cause: raft.ErrNotLeader": {
			cause:     raft.ErrNotLeader,
			expLeader: true,
		},
		"cause: raft.ErrLeadershipLost": {
			cause:     raft.ErrLeadershipLost,
			expLeader: true,
		},
		"cause: raft.ErrLeadershipTransferInProgress": {
			cause:     raft.ErrLeadershipTransferInProgress,
			expLeader: true,
		},
		"cause: system.ErrNotLeader": {
			cause:     &system.ErrNotLeader{},
			expLeader: true,
		},
		// Also check to see what happens if we get a raft error during
		// leadership transfer.
		"leadership transfer fails": {
			expErr:    errors.New("leadership transfer failed"),
			expLeader: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			db.raft.setSvc(newMockRaftService(&mockRaftServiceConfig{
				LeadershipTransferErr: tc.expErr,
				State:                 raft.Leader,
			}, (*fsm)(db)))

			resignErr := db.ResignLeadership(tc.cause)
			test.CmpErr(t, tc.expErr, resignErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expLeader, db.IsLeader(), "unexpected leader state")
		})
	}
}

func TestDatabase_TakePoolLock(t *testing.T) {
	mockUUID := uuid.MustParse(test.MockUUID(1))
	parentLock := makeLock(1, 1, 1)
	wrongIdLock := makeLock(1, 2, 1)
	wrongPoolLock := makeLock(1, 1, 2)

	for name, tc := range map[string]struct {
		ctx          context.Context
		poolUUID     uuid.UUID
		existingLock *PoolLock
		expErr       error
		expNewLock   bool
	}{
		"nil context": {
			poolUUID: mockUUID,
			expErr:   errors.New("nil context"),
		},
		"empty pool UUID": {
			ctx:    test.Context(t),
			expErr: errors.New("nil pool UUID"),
		},
		"already-released parent lock": {
			ctx:      parentLock.InContext(test.Context(t)),
			poolUUID: mockUUID,
			expErr:   errors.New("lock not found"),
		},
		"parent lock wrong id": {
			ctx:          parentLock.InContext(test.Context(t)),
			existingLock: wrongIdLock,
			poolUUID:     mockUUID,
			expErr:       errors.New("is locked"),
		},
		"parent lock for wrong pool": {
			ctx:          wrongPoolLock.InContext(test.Context(t)),
			existingLock: wrongPoolLock,
			poolUUID:     mockUUID,
			expErr:       errors.New("different pool"),
		},
		"successful new lock": {
			ctx:        test.Context(t),
			poolUUID:   mockUUID,
			expNewLock: true,
		},
		"successful parent lock": {
			ctx:          parentLock.InContext(test.Context(t)),
			existingLock: parentLock,
			poolUUID:     mockUUID,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			if tc.existingLock != nil {
				db.poolLocks.locks = make(map[uuid.UUID]*PoolLock)
				db.poolLocks.locks[tc.existingLock.poolUUID] = tc.existingLock
			}
			gotLock, gotErr := db.TakePoolLock(tc.ctx, tc.poolUUID)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if tc.expNewLock && (gotLock == nil || gotLock == parentLock) {
				t.Fatal("expected new lock")
			} else if !tc.expNewLock && gotLock != parentLock {
				t.Fatal("expected parent lock")
			}
		})
	}
}
