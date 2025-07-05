//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"testing"

	. "github.com/daos-stack/daos/src/control/system"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func getMembers(t *testing.T) Members {
	members := make(Members, 4)
	for i := 0; i < len(members); i++ {
		members[i] = MockMember(t, uint32(i), MemberStateJoined)
		// Simulate two ranks per host.
		members[i].Addr = MockControlAddr(t, uint32(i/2))
	}

	return members
}

func getMemberDatabase(t *testing.T, members Members) *MemberDatabase {
	mdb := MemberDatabase{
		Ranks: MemberRankMap{},
		Uuids: MemberUuidMap{},
		Addrs: MemberAddrMap{},
	}
	domains := []*FaultDomain{}
	for _, m := range members {
		mdb.Ranks[m.Rank] = m
		mdb.Uuids[m.UUID] = m
		mdb.Addrs[m.Addr.String()] = append(mdb.Addrs[m.Addr.String()], m)
		domains = append(domains, MemberFaultDomain(m))
	}
	mdb.FaultDomains = NewFaultDomainTree(domains...)

	return &mdb
}

func TestSystem_MemberAddrMap_addMember(t *testing.T) {
	members := getMembers(t)

	for name, tc := range map[string]struct {
		idxsToStart  []int
		idxsToAdd    []int
		idxsExpected []int
		expNrUuids   int
		expNrRanks   int
	}{
		"add success": {
			idxsToStart:  []int{0, 2, 3},
			idxsToAdd:    []int{1},
			idxsExpected: []int{0, 1, 2, 3},
			expNrUuids:   4,
			expNrRanks:   4,
		},
		"add duplicate": {
			idxsToStart:  []int{0, 1, 2, 3},
			idxsToAdd:    []int{1},
			idxsExpected: []int{0, 1, 2, 3},
			expNrUuids:   4,
			expNrRanks:   4,
		},
		"add with other duplicates at same address": {
			idxsToStart:  []int{0, 0, 2, 3},
			idxsToAdd:    []int{1},
			idxsExpected: []int{0, 1, 2, 3},
			expNrUuids:   4,
			expNrRanks:   4,
		},
		"add with other duplicates at different address": {
			idxsToStart: []int{0, 2, 3, 3},
			idxsToAdd:   []int{1},
			// Duplicate persists because added rank at different address.
			idxsExpected: []int{0, 1, 2, 3, 3},
			expNrUuids:   4,
			expNrRanks:   4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			// Retrieve member reference slices from indices specified.
			startMembers := Members{}
			for _, idx := range tc.idxsToStart {
				if len(members) <= idx {
					t.Fatal("bad test params")
				}
				startMembers = append(startMembers, members[idx])
			}
			toAdd := Members{}
			for _, idx := range tc.idxsToAdd {
				if len(members) <= idx {
					t.Fatal("bad test params")
				}
				toAdd = append(toAdd, members[idx])
			}
			expMembers := Members{}
			for _, idx := range tc.idxsExpected {
				if len(members) <= idx {
					t.Fatal("bad test params")
				}
				expMembers = append(expMembers, members[idx])
			}

			mdb := getMemberDatabase(t, startMembers)

			for _, m := range toAdd {
				mdb.addMember(m)
			}

			// Check member DB was updated.
			for _, expMember := range expMembers {
				checkMemberDB(t, mdb, expMember,
					cmpopts.IgnoreFields(Member{}, "LastUpdate"))
			}
			if len(mdb.Uuids) != tc.expNrUuids {
				t.Fatalf("expected %d uuid map entries, got %d", tc.expNrUuids,
					len(mdb.Uuids))
			}
			if len(mdb.Ranks) != tc.expNrRanks {
				t.Fatalf("expected %d rank map entries, got %d", tc.expNrRanks,
					len(mdb.Ranks))
			}

			// Verify address map has expected number of entries.
			count := 0
			for _, uuids := range mdb.Addrs {
				count += len(uuids)
			}
			if count != len(expMembers) {
				t.Fatalf("expected %d address map entries, got %d", len(expMembers),
					count)
			}
		})
	}
}

func TestSystem_MemberDatabase_removeMember(t *testing.T) {
	for name, tc := range map[string]struct {
		startingMembers Members
		idxsToRemove    []int
		idxsExpected    []int
		expNrUuids      int
		expNrRanks      int
	}{
		"remove success": {
			startingMembers: getMembers(t),
			idxsToRemove:    []int{1},
			idxsExpected:    []int{0, 2, 3},
			expNrUuids:      3,
			expNrRanks:      3,
		},
		"remove duplicates": {
			startingMembers: func() Members {
				ms := getMembers(t)
				dm := MockMember(t, 1, MemberStateJoined)
				dm.Addr = MockControlAddr(t, 0)
				return append(ms, dm)
			}(),
			idxsToRemove: []int{1},
			idxsExpected: []int{0, 2, 3},
			expNrUuids:   3,
			expNrRanks:   3,
		},
		"remove with other duplicates at same address": {
			startingMembers: func() Members {
				ms := getMembers(t)
				dm := MockMember(t, 0, MemberStateJoined)
				dm.Addr = MockControlAddr(t, 0)
				return append(ms, dm)
			}(),
			idxsToRemove: []int{1},
			idxsExpected: []int{0, 2, 3},
			expNrUuids:   3,
			expNrRanks:   3,
		},
		"remove with other duplicates at different address": {
			startingMembers: func() Members {
				ms := getMembers(t)
				dm := MockMember(t, 3, MemberStateJoined)
				dm.Addr = MockControlAddr(t, 1)
				return append(ms, dm)
			}(),
			idxsToRemove: []int{1},
			// Duplicate persists because removed rank at different address.
			idxsExpected: []int{0, 2, 3, 3},
			expNrUuids:   3,
			expNrRanks:   3,
		},
	} {
		t.Run(name, func(t *testing.T) {
			// Retrieve member reference slices from indices specified.
			toRemove := Members{}
			for _, idx := range tc.idxsToRemove {
				if len(tc.startingMembers) <= idx {
					t.Fatal("bad test params")
				}
				toRemove = append(toRemove, tc.startingMembers[idx])
			}
			expMembers := Members{}
			for _, idx := range tc.idxsExpected {
				if len(tc.startingMembers) <= idx {
					t.Fatal("bad test params")
				}
				expMembers = append(expMembers, tc.startingMembers[idx])
			}

			mdb := getMemberDatabase(t, tc.startingMembers)

			for _, m := range toRemove {
				mdb.removeMember(m)
			}

			// Check member DB was updated.
			for _, expMember := range expMembers {
				checkMemberDB(t, mdb, expMember,
					cmpopts.IgnoreFields(Member{}, "LastUpdate"))
			}
			if len(mdb.Uuids) != tc.expNrUuids {
				t.Fatalf("expected %d uuid map entries, got %d", tc.expNrUuids,
					len(mdb.Uuids))
			}
			if len(mdb.Ranks) != tc.expNrRanks {
				t.Fatalf("expected %d rank map entries, got %d", tc.expNrRanks,
					len(mdb.Ranks))
			}

			// Verify address map has expected number of entries.
			count := 0
			for _, uuids := range mdb.Addrs {
				count += len(uuids)
			}
			if count != len(expMembers) {
				t.Fatalf("expected %d address map entries, got %d", len(expMembers),
					count)
			}
		})
	}
}

func TestSystem_MemberAddrMap_updateMember(t *testing.T) {
	members := getMembers(t)
	updatedMember := *members[2]
	updatedMember.State = MemberStateErrored
	identicalMember := *members[2]

	for name, tc := range map[string]struct {
		memberToUpdate Member
		expMembers     Members
	}{
		"update success": {
			memberToUpdate: updatedMember,
			expMembers: Members{
				members[0], members[1], &updatedMember, members[3],
			},
		},
		"update; identical member": {
			memberToUpdate: identicalMember,
			expMembers:     members,
		},
	} {
		t.Run(name, func(t *testing.T) {
			mdb := getMemberDatabase(t, members)

			mdb.updateMember(&tc.memberToUpdate)

			// Check member DB was updated.
			for _, expMember := range tc.expMembers {
				checkMemberDB(t, mdb, expMember,
					cmpopts.IgnoreFields(Member{}, "LastUpdate"))
			}
			if len(mdb.Uuids) != len(tc.expMembers) {
				t.Fatalf("expected %d uuid map entries, got %d", len(tc.expMembers),
					len(mdb.Uuids))
			}
			if len(mdb.Ranks) != len(tc.expMembers) {
				t.Fatalf("expected %d rank map entries, got %d", len(tc.expMembers),
					len(mdb.Ranks))
			}

			// Verify address map has expected number of entries.
			count := 0
			for _, uuids := range mdb.Addrs {
				count += len(uuids)
			}
			if count != len(tc.expMembers) {
				t.Fatalf("expected %d address map entries, got %d",
					len(tc.expMembers), count)
			}
		})
	}
}
