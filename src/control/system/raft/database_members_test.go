//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"testing"

	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/system"
)

func TestRaft_MemberAddrMap_removeMember(t *testing.T) {
	makeMap := func(t *testing.T, members ...*system.Member) MemberAddrMap {
		mam := make(MemberAddrMap)
		for _, m := range members {
			mam[m.Addr.String()] = append(mam[m.Addr.String()], m)
		}
		return mam
	}

	testMember := func(t *testing.T, idx int) *system.Member {
		return system.MockMember(t, uint32(idx), system.MemberStateJoined)
	}

	members := func(t *testing.T, n int) system.Members {
		members := system.Members{}
		for i := 0; i < n; i++ {
			members = append(members, testMember(t, i))
		}
		return members
	}

	for name, tc := range map[string]struct {
		addrMap  MemberAddrMap
		toRemove *system.Member
		expMap   MemberAddrMap
	}{
		"empty": {
			addrMap: make(MemberAddrMap),
			toRemove: &system.Member{
				Addr: system.MockControlAddr(t, 1),
				UUID: uuid.New(),
			},
			expMap: make(MemberAddrMap),
		},
		"only one member": {
			addrMap:  makeMap(t, members(t, 1)...),
			toRemove: testMember(t, 0),
			expMap:   make(MemberAddrMap),
		},
		"first member": {
			addrMap:  makeMap(t, members(t, 3)...),
			toRemove: testMember(t, 0),
			expMap:   makeMap(t, testMember(t, 1), testMember(t, 2)),
		},
		"last member": {
			addrMap:  makeMap(t, members(t, 3)...),
			toRemove: testMember(t, 2),
			expMap:   makeMap(t, testMember(t, 0), testMember(t, 1)),
		},
		"middle member": {
			addrMap:  makeMap(t, members(t, 3)...),
			toRemove: testMember(t, 1),
			expMap:   makeMap(t, testMember(t, 0), testMember(t, 2)),
		},
		"not found": {
			addrMap:  makeMap(t, members(t, 3)...),
			toRemove: testMember(t, 6),
			expMap:   makeMap(t, members(t, 3)...),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.addrMap.removeMember(tc.toRemove)

			test.CmpAny(t, "MemberAddrMap", tc.expMap, tc.addrMap, cmpopts.IgnoreFields(system.Member{}, "LastUpdate"))
		})
	}
}
