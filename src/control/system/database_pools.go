//
// (C) Copyright 2020-2021 Intel Corporation.
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
	"fmt"
	"sync"

	"github.com/dustin/go-humanize"
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

	// PoolServiceStorage holds information about the pool storage.
	PoolServiceStorage struct {
		sync.Mutex
		CreationRankStr string   // string rankset set at creation
		creationRanks   *RankSet // used to reconstitute the rankset
		CurrentRankStr  string   // string rankset representing current ranks
		currentRanks    *RankSet // used to reconstitute the rankset
		ScmPerRank      uint64   // scm per rank allocated during creation
		NVMePerRank     uint64   // nvme per rank allocated during creation
	}

	// PoolService represents a pool service created to manage metadata
	// for a DAOS Pool.
	PoolService struct {
		PoolUUID  uuid.UUID
		PoolLabel string
		State     PoolServiceState
		Replicas  []Rank
		Storage   *PoolServiceStorage
	}

	// PoolRankMap provides a map of Rank->[]*PoolService.
	PoolRankMap map[Rank][]*PoolService
	// PoolUuidMap provides a map of UUID->*PoolService.
	PoolUuidMap map[uuid.UUID]*PoolService
	// PoolLabelMap provides a map of Label->*PoolService.
	PoolLabelMap map[string]*PoolService

	// PoolDatabase contains a set of maps for looking up DAOS Pool
	// Service instances and methods for managing the pool membership.
	PoolDatabase struct {
		Ranks  PoolRankMap
		Uuids  PoolUuidMap
		Labels PoolLabelMap
	}
)

// NewPoolService returns a properly-initialized *PoolService.
func NewPoolService(uuid uuid.UUID, rankScm, rankNvme uint64, ranks []Rank) *PoolService {
	rs := RankSetFromRanks(ranks)
	return &PoolService{
		PoolUUID: uuid,
		State:    PoolServiceStateCreating,
		Storage: &PoolServiceStorage{
			ScmPerRank:      rankScm,
			NVMePerRank:     rankNvme,
			CreationRankStr: rs.RangedString(),
			CurrentRankStr:  rs.RangedString(),
		},
	}
}

// CreationRanks returns the set of target ranks associated
// with the pool's creation.
func (pss *PoolServiceStorage) CreationRanks() []Rank {
	pss.Lock()
	defer pss.Unlock()

	if pss.creationRanks == nil {
		var err error
		pss.creationRanks, err = CreateRankSet(pss.CreationRankStr)
		if err != nil {
			return nil
		}
	}
	return pss.creationRanks.Ranks()
}

// CurrentRanks returns the set of target ranks associated
// with the pool's current.
func (pss *PoolServiceStorage) CurrentRanks() []Rank {
	pss.Lock()
	defer pss.Unlock()

	if pss.currentRanks == nil {
		var err error
		pss.currentRanks, err = CreateRankSet(pss.CurrentRankStr)
		if err != nil {
			return nil
		}
	}
	return pss.currentRanks.Ranks()
}

// TotalSCM returns the total amount of SCM storage allocated to
// the pool, calculated from the current set of ranks multiplied
// by the per-rank SCM allocation made at creation time.
func (pss *PoolServiceStorage) TotalSCM() uint64 {
	return uint64(len(pss.CurrentRanks())) * pss.ScmPerRank
}

// TotalNVMe returns the total amount of NVMe storage allocated to
// the pool, calculated from the current set of ranks multiplied
// by the per-rank NVMe allocation made at creation time.
func (pss *PoolServiceStorage) TotalNVMe() uint64 {
	return uint64(len(pss.CurrentRanks())) * pss.NVMePerRank
}

func (pss *PoolServiceStorage) String() string {
	if pss == nil {
		return "no pool storage info available"
	}
	return fmt.Sprintf("total SCM: %s, total NVMe: %s",
		humanize.Bytes(pss.TotalSCM()),
		humanize.Bytes(pss.TotalNVMe()))
}

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

// MarshalJSON creates a serialized representation of the PoolLabelMap.
// The pool's UUID is used to represent the pool service in order to
// avoid duplicating pool service details in the serialized format.
func (plm PoolLabelMap) MarshalJSON() ([]byte, error) {
	jm := make(map[string]uuid.UUID)
	for label, svc := range plm {
		jm[label] = svc.PoolUUID
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
		Ranks  map[Rank][]uuid.UUID
		Labels map[string]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[Rank][]uuid.UUID),
		Labels:   make(map[string]uuid.UUID),
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

	for label, uuid := range from.Labels {
		svc, found := pdb.Uuids[uuid]
		if !found {
			return errors.Errorf("label %q missing UUID", label)
		}

		if _, exists := pdb.Labels[label]; exists {
			return errors.Errorf("pool label %q was already restored", label)
		}
		pdb.Labels[label] = svc
	}

	return nil
}

// addService is responsible for adding a new PoolService entry and
// updating all of the relevant maps.
func (pdb *PoolDatabase) addService(ps *PoolService) {
	pdb.Uuids[ps.PoolUUID] = ps
	if ps.PoolLabel != "" {
		pdb.Labels[ps.PoolLabel] = ps
	}
	for _, rank := range ps.Replicas {
		pdb.Ranks[rank] = append(pdb.Ranks[rank], ps)
	}
}

// updateService is responsible for updating relevant maps
// after a pool service update. The cur PoolService
// pointer must exist in the database.
func (pdb *PoolDatabase) updateService(cur, new *PoolService) {
	if cur != pdb.Uuids[cur.PoolUUID] {
		panic("PoolDatabase.updateService() called with non-member pointer")
	}
	cur.State = new.State

	// TODO: Update svc rank map
	cur.Replicas = new.Replicas

	if cur.PoolLabel != "" {
		delete(pdb.Labels, cur.PoolLabel)
	}
	cur.PoolLabel = new.PoolLabel
	if cur.PoolLabel != "" {
		pdb.Labels[cur.PoolLabel] = cur
	}
}

// removeService is responsible for removing a PoolService entry and
// updating all of the relevant maps.
func (pdb *PoolDatabase) removeService(ps *PoolService) {
	delete(pdb.Uuids, ps.PoolUUID)
	if ps.PoolLabel != "" {
		delete(pdb.Labels, ps.PoolLabel)
	}
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
