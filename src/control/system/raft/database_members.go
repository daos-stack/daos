//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"encoding/json"
	"net"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/system"
)

type (
	// MemberRankMap provides a map of Rank->*system.Member.
	MemberRankMap map[system.Rank]*system.Member
	// MemberUuidMap provides a map of UUID->*system.Member.
	MemberUuidMap map[uuid.UUID]*system.Member
	// MemberAddrMap provides a map of string->[]*system.Member.
	MemberAddrMap map[string][]*system.Member

	// MemberDatabase contains a set of maps for looking
	// up members and provides methods for managing the
	// membership.
	MemberDatabase struct {
		Ranks        MemberRankMap
		Uuids        MemberUuidMap
		Addrs        MemberAddrMap
		FaultDomains *system.FaultDomainTree
	}
)

// MarshalJSON creates a serialized representation of the MemberRankMap.
// The member's UUID is used to represent the member in order to
// avoid duplicating member details in the serialized format.
func (mrm MemberRankMap) MarshalJSON() ([]byte, error) {
	jm := make(map[system.Rank]uuid.UUID)
	for rank, member := range mrm {
		jm[rank] = member.UUID
	}
	return json.Marshal(jm)
}

func (mam MemberAddrMap) addMember(addr *net.TCPAddr, m *system.Member) {
	if _, exists := mam[addr.String()]; !exists {
		mam[addr.String()] = []*system.Member{}
	}
	mam[addr.String()] = append(mam[addr.String()], m)
}

func (mam MemberAddrMap) removeMember(m *system.Member) {
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
		Ranks map[system.Rank]uuid.UUID
		Addrs map[string][]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[system.Rank]uuid.UUID),
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
func (mdb *MemberDatabase) addMember(m *system.Member) {
	mdb.Ranks[m.Rank] = m
	mdb.Uuids[m.UUID] = m
	mdb.Addrs.addMember(m.Addr, m)

	mdb.addToFaultDomainTree(m)
}

func (mdb *MemberDatabase) addToFaultDomainTree(m *system.Member) {
	if err := mdb.FaultDomains.AddDomain(system.MemberFaultDomain(m)); err != nil {
		panic(err)
	}
}

func (mdb *MemberDatabase) updateMember(m *system.Member) {
	cur, found := mdb.Uuids[m.UUID]
	if !found {
		panic(errors.Errorf("member update for unknown member %+v", m))
	}
	cur.State = m.State
	cur.Info = m.Info
	cur.LastUpdate = m.LastUpdate
	cur.Incarnation = m.Incarnation

	mdb.removeFromFaultDomainTree(cur)
	cur.FaultDomain = m.FaultDomain
	mdb.addToFaultDomainTree(cur)
}

// removeMember is responsible for removing new Member and updating all
// of the relevant maps.
func (mdb *MemberDatabase) removeMember(m *system.Member) {
	delete(mdb.Ranks, m.Rank)
	delete(mdb.Uuids, m.UUID)
	mdb.Addrs.removeMember(m)
	mdb.removeFromFaultDomainTree(m)
}

func (mdb *MemberDatabase) removeFromFaultDomainTree(m *system.Member) {
	if err := mdb.FaultDomains.RemoveDomain(system.MemberFaultDomain(m)); err != nil {
		panic(err)
	}
}
