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

	PoolDatabase struct {
		Ranks PoolRankMap
		Uuids PoolUuidMap
		Addrs PoolAddrMap
	}
)

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

func (pdb *PoolDatabase) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	type fromJSON PoolDatabase
	from := &struct {
		Ranks map[Rank][]uuid.UUID
		Addrs map[string][]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[Rank][]uuid.UUID),
		Addrs:    make(map[string][]uuid.UUID),
		fromJSON: (*fromJSON)(pdb),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	for rank, uuids := range from.Ranks {
		for _, uuid := range uuids {
			svc, found := pdb.Uuids[uuid]
			if !found {
				return errors.Errorf("rank %d missing UUID", rank)
			}

			if _, exists := pdb.Ranks[rank]; !exists {
				pdb.Ranks[rank] = []*PoolService{}
			}
			pdb.Ranks[rank] = append(pdb.Ranks[rank], svc)
		}
	}

	for addrStr, uuids := range from.Addrs {
		for _, uuid := range uuids {
			svc, found := pdb.Uuids[uuid]
			if !found {
				return errors.Errorf("addr %s missing UUID", addrStr)
			}

			addr, err := net.ResolveTCPAddr("tcp", addrStr)
			if err != nil {
				return err
			}
			if _, exists := pdb.Addrs[addr]; !exists {
				pdb.Addrs[addr] = []*PoolService{}
			}
			pdb.Addrs[addr] = append(pdb.Addrs[addr], svc)
		}
	}

	return nil
}

func (pdb *PoolDatabase) addService(ps *PoolService) {
	pdb.Uuids[ps.PoolUUID] = ps
	for _, rank := range ps.Replicas {
		pdb.Ranks[rank] = append(pdb.Ranks[rank], ps)
	}
}

func (pdb *PoolDatabase) removeService(ps *PoolService) {
	delete(pdb.Uuids, ps.PoolUUID)
	for _, rank := range ps.Replicas {
		rankServices := pdb.Ranks[rank]
		for idx, rs := range rankServices {
			if rs.PoolUUID == ps.PoolUUID {
				pdb.Ranks[rank] = append(rankServices[:idx], rankServices[:idx]...)
				break
			}
		}
	}
}
