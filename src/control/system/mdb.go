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
	ServerRankMap map[Rank]*Member
	ServerUuidMap map[uuid.UUID]*Member
	ServerAddrMap map[net.Addr][]*Member

	MemberDatabase struct {
		Ranks ServerRankMap
		Uuids ServerUuidMap
		Addrs ServerAddrMap
	}
)

func (srm ServerRankMap) MarshalJSON() ([]byte, error) {
	jm := make(map[Rank]uuid.UUID)
	for rank, member := range srm {
		jm[rank] = member.UUID
	}
	return json.Marshal(jm)
}

func (sam ServerAddrMap) addMember(addr net.Addr, m *Member) {
	if _, exists := sam[addr]; !exists {
		sam[addr] = []*Member{}
	}
	sam[addr] = append(sam[addr], m)
}

func (sam ServerAddrMap) MarshalJSON() ([]byte, error) {
	jm := make(map[string][]uuid.UUID)
	for addr, members := range sam {
		addrStr := addr.String()
		if _, exists := jm[addrStr]; !exists {
			jm[addrStr] = []uuid.UUID{}
		}
		for _, member := range members {
			jm[addrStr] = append(jm[addrStr], member.UUID)
		}
	}
	return json.Marshal(jm)
}

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

func (mdb *MemberDatabase) addMember(m *Member) {
	mdb.Ranks[m.Rank] = m
	mdb.Uuids[m.UUID] = m
	mdb.Addrs.addMember(m.Addr, m)
}

func (mdb *MemberDatabase) removeMember(m *Member) {
	delete(mdb.Ranks, m.Rank)
	delete(mdb.Uuids, m.UUID)
	delete(mdb.Addrs, m.Addr)
}
