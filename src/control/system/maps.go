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
)

const (
	PoolServiceStateCreating PoolServiceState = iota
	PoolServiceStateReady
	PoolServiceStateDestroying
)

type (
	PoolServiceState uint

	PoolService struct {
		PoolUUID uuid.UUID
		State    PoolServiceState
		Replicas []Rank
	}

	PoolRankMap map[Rank][]*PoolService
	PoolUuidMap map[uuid.UUID]*PoolService
	PoolAddrMap map[net.Addr][]*PoolService
)

type (
	ServerRankMap map[Rank]*Member
	ServerUuidMap map[uuid.UUID]*Member
	ServerAddrMap map[net.Addr][]*Member
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

func (prm PoolRankMap) MarshalJSON() ([]byte, error) {
	jm := make(map[Rank][]uuid.UUID)
	for rank, svcList := range prm {
		if _, exists := jm[rank]; !exists {
			jm[rank] = []uuid.UUID{}
		}
		for _, svc := range svcList {
			jm[rank] = append(jm[rank], svc.PoolUUID)
		}
	}
	return json.Marshal(jm)
}

func (pam PoolAddrMap) MarshalJSON() ([]byte, error) {
	jm := make(map[string][]uuid.UUID)
	for addr, svcList := range pam {
		addrStr := addr.String()
		if _, exists := jm[addrStr]; !exists {
			jm[addrStr] = []uuid.UUID{}
		}
		for _, svc := range svcList {
			jm[addrStr] = append(jm[addrStr], svc.PoolUUID)
		}
	}
	return json.Marshal(jm)
}
