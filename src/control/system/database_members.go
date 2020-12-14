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
	"net"

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

type (
	// MemberRankMap provides a map of Rank->*Member.
	MemberRankMap map[Rank]*Member
	// MemberUuidMap provides a map of UUID->*Member.
	MemberUuidMap map[uuid.UUID]*Member
	// MemberAddrMap provides a map of string->[]*Member.
	MemberAddrMap map[string][]*Member

	// MemberDatabase contains a set of maps for looking
	// up members and provides methods for managing the
	// membership.
	MemberDatabase struct {
		Ranks        MemberRankMap
		Uuids        MemberUuidMap
		Addrs        MemberAddrMap
		FaultDomains *FaultDomainTree
	}
)

// MarshalJSON creates a serialized representation of the MemberRankMap.
// The member's UUID is used to represent the member in order to
// avoid duplicating member details in the serialized format.
func (mrm MemberRankMap) MarshalJSON() ([]byte, error) {
	jm := make(map[Rank]uuid.UUID)
	for rank, member := range mrm {
		jm[rank] = member.UUID
	}
	return json.Marshal(jm)
}

func (mam MemberAddrMap) addMember(addr *net.TCPAddr, m *Member) {
	if _, exists := mam[addr.String()]; !exists {
		mam[addr.String()] = []*Member{}
	}
	mam[addr.String()] = append(mam[addr.String()], m)
}

func (mam MemberAddrMap) removeMember(m *Member) {
	members, exists := mam[m.Addr.String()]
	if !exists {
		return
	}
	for i, cur := range members {
		if m.UUID == cur.UUID {
			// remove from slice
			members = append(members[:i], members[i+1:]...)
			break
		}
	}
	if len(members) == 0 {
		delete(mam, m.Addr.String())
	}
}

// MarshalJSON creates a serialized representation of the MemberAddrMap.
// The member's UUID is used to represent the member in order to
// avoid duplicating member details in the serialized format.
func (mam MemberAddrMap) MarshalJSON() ([]byte, error) {
	jm := make(map[string][]uuid.UUID)
	for addr, members := range mam {
		if _, exists := jm[addr]; !exists {
			jm[addr] = []uuid.UUID{}
		}
		for _, member := range members {
			jm[addr] = append(jm[addr], member.UUID)
		}
	}
	return json.Marshal(jm)
}

// UnmarshalJSON "inflates" the MemberDatabase from a compressed and
// serialized representation. The MemberUuidMap contains a full representation
// of each Member, whereas the other serialized maps simply use the
// Member UUID as a placeholder for the mapping.
func (mdb *MemberDatabase) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	type fromJSON MemberDatabase
	from := &struct {
		Ranks map[Rank]uuid.UUID
		Addrs map[string][]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[Rank]uuid.UUID),
		Addrs:    make(map[string][]uuid.UUID),
		fromJSON: (*fromJSON)(mdb),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	for rank, uuid := range from.Ranks {
		member, found := mdb.Uuids[uuid]
		if !found {
			return errors.Errorf("rank %d missing UUID", rank)
		}
		mdb.Ranks[rank] = member
	}

	for addrStr, uuids := range from.Addrs {
		for _, uuid := range uuids {
			member, found := mdb.Uuids[uuid]
			if !found {
				return errors.Errorf("addr %s missing UUID", addrStr)
			}

			addr, err := net.ResolveTCPAddr("tcp", addrStr)
			if err != nil {
				return err
			}
			mdb.Addrs.addMember(addr, member)
		}
	}

	return nil
}

// addMember is responsible for adding a new Member and updating all
// of the relevant maps.
func (mdb *MemberDatabase) addMember(m *Member) {
	mdb.Ranks[m.Rank] = m
	mdb.Uuids[m.UUID] = m
	mdb.Addrs.addMember(m.Addr, m)

	if err := mdb.FaultDomains.AddDomain(m.RankFaultDomain()); err != nil {
		panic(err)
	}
}

func (mdb *MemberDatabase) updateMember(m *Member) {
	cur, found := mdb.Uuids[m.UUID]
	if !found {
		panic(errors.Errorf("member update for unknown member %+v", m))
	}
	cur.state = m.state
	cur.Info = m.Info

	if err := mdb.FaultDomains.RemoveDomain(cur.RankFaultDomain()); err != nil {
		panic(err)
	}
	cur.FaultDomain = m.FaultDomain
	if err := mdb.FaultDomains.AddDomain(cur.RankFaultDomain()); err != nil {
		panic(err)
	}
}

// removeMember is responsible for removing new Member and updating all
// of the relevant maps.
func (mdb *MemberDatabase) removeMember(m *Member) {
	delete(mdb.Ranks, m.Rank)
	delete(mdb.Uuids, m.UUID)
	mdb.Addrs.removeMember(m)
	if err := mdb.FaultDomains.RemoveDomain(m.RankFaultDomain()); err != nil {
		panic(err)
	}
}
