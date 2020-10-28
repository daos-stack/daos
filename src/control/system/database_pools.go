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

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

const (
	// PoolServiceStateCreating indicates that the pool service is being created
	PoolServiceStateCreating PoolServiceState = iota
	// PoolServiceStateReady indicates that the pool service is ready to be used
	PoolServiceStateReady
	// PoolServiceStateDestroying indicates that the pool service is being destroyed
	PoolServiceStateDestroying
)

type (
	// PoolServiceState is used to represent the state of the pool service
	PoolServiceState uint

	// PoolService represents a pool service created to manage metadata
	// for a DAOS Pool.
	PoolService struct {
		PoolUUID uuid.UUID
		State    PoolServiceState
		Replicas []Rank
	}

	// PoolRankMap provides a map of Rank->[]*PoolService.
	PoolRankMap map[Rank][]*PoolService
	// PoolUuidMap provides a map of UUID->*PoolService.
	PoolUuidMap map[uuid.UUID]*PoolService

	// PoolDatabase contains a set of maps for looking up DAOS Pool
	// Service instances and methods for managing the pool membership.
	PoolDatabase struct {
		Ranks PoolRankMap
		Uuids PoolUuidMap
	}
)

func (pss PoolServiceState) String() string {
	return [...]string{
		"Creating",
		"Ready",
		"Destroying",
	}[pss]
}

// MarshalJSON creates a serialized representation of the PoolRankMap.
// The pool's UUID is used to represent the pool service in order to
// avoid duplicating pool service details in the serialized format.
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

// UnmarshalJSON "inflates" the PoolDatabase from a compressed and
// serialized representation. The PoolUuidMap contains a full representation
// of each PoolService, whereas the other serialized maps simply use the
// Pool UUID as a placeholder for the mapping.
func (pdb *PoolDatabase) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	type fromJSON PoolDatabase
	from := &struct {
		Ranks map[Rank][]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[Rank][]uuid.UUID),
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

	return nil
}

// addService is responsible for adding a new PoolService entry and
// updating all of the relevant maps.
func (pdb *PoolDatabase) addService(ps *PoolService) {
	pdb.Uuids[ps.PoolUUID] = ps
	for _, rank := range ps.Replicas {
		pdb.Ranks[rank] = append(pdb.Ranks[rank], ps)
	}
}

// removeService is responsible for removing a PoolService entry and
// updating all of the relevant maps.
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
