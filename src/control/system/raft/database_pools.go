//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"encoding/json"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/system"
)

type (
	// PoolRankMap provides a map of Rank->[]*PoolService.
	PoolRankMap map[system.Rank][]*system.PoolService
	// PoolUuidMap provides a map of UUID->*PoolService.
	PoolUuidMap map[uuid.UUID]*system.PoolService
	// PoolLabelMap provides a map of Label->*PoolService.
	PoolLabelMap map[string]*system.PoolService

	// PoolDatabase contains a set of maps for looking up DAOS Pool
	// Service instances and methods for managing the pool membership.
	PoolDatabase struct {
		Ranks  PoolRankMap
		Uuids  PoolUuidMap
		Labels PoolLabelMap
	}
)

// MarshalJSON creates a serialized representation of the PoolRankMap.
// The pool's UUID is used to represent the pool service in order to
// avoid duplicating pool service details in the serialized format.
func (prm PoolRankMap) MarshalJSON() ([]byte, error) {
	jm := make(map[system.Rank][]uuid.UUID)
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
		Ranks  map[system.Rank][]uuid.UUID
		Labels map[string]uuid.UUID
		*fromJSON
	}{
		Ranks:    make(map[system.Rank][]uuid.UUID),
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
				pdb.Ranks[rank] = []*system.PoolService{}
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
func (pdb *PoolDatabase) addService(ps *system.PoolService) {
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
func (pdb *PoolDatabase) updateService(cur, new *system.PoolService) {
	if cur != pdb.Uuids[cur.PoolUUID] {
		panic("PoolDatabase.updateService() called with non-member pointer")
	}
	cur.State = new.State
	cur.LastUpdate = new.LastUpdate

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
func (pdb *PoolDatabase) removeService(ps *system.PoolService) {
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
