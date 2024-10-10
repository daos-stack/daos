//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"sync"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

type (
	// PoolServiceStorage holds information about the pool storage.
	PoolServiceStorage struct {
		sync.Mutex
		CreationRankStr    string            // string rankset set at creation
		creationRanks      *ranklist.RankSet // used to reconstitute the rankset
		CurrentRankStr     string            // string rankset representing current ranks
		currentRanks       *ranklist.RankSet // used to reconstitute the rankset
		PerRankTierStorage []uint64          // storage allocated to each tier on a rank
	}

	// PoolServiceState is a local type alias for daos.PoolServiceState.
	// NB: We use this to insulate the system DB from any incompatible
	// changes made to the daos.PoolServiceState type.
	PoolServiceState daos.PoolServiceState

	// PoolService represents a pool service created to manage metadata
	// for a DAOS Pool.
	PoolService struct {
		PoolUUID   uuid.UUID
		PoolLabel  string
		State      PoolServiceState
		Replicas   []ranklist.Rank
		Storage    *PoolServiceStorage
		LastUpdate time.Time
	}
)

const (
	PoolServiceStateCreating   = PoolServiceState(daos.PoolServiceStateCreating)
	PoolServiceStateReady      = PoolServiceState(daos.PoolServiceStateReady)
	PoolServiceStateDestroying = PoolServiceState(daos.PoolServiceStateDestroying)
)

func (pss PoolServiceState) String() string {
	return daos.PoolServiceState(pss).String()
}

// NewPoolService returns a properly-initialized *PoolService.
func NewPoolService(uuid uuid.UUID, tierStorage []uint64, ranks []ranklist.Rank) *PoolService {
	rs := ranklist.RankSetFromRanks(ranks)
	return &PoolService{
		PoolUUID: uuid,
		State:    PoolServiceStateCreating,
		Storage: &PoolServiceStorage{
			PerRankTierStorage: tierStorage,
			CreationRankStr:    rs.RangedString(),
			CurrentRankStr:     rs.RangedString(),
		},
	}
}

// CreationRanks returns the set of target ranks associated
// with the pool's creation.
func (pss *PoolServiceStorage) CreationRanks() []ranklist.Rank {
	pss.Lock()
	defer pss.Unlock()

	if pss.creationRanks == nil {
		var err error
		pss.creationRanks, err = ranklist.CreateRankSet(pss.CreationRankStr)
		if err != nil {
			return nil
		}
	}
	return pss.creationRanks.Ranks()
}

// CurrentRanks returns the set of target ranks associated
// with the pool's current.
func (pss *PoolServiceStorage) CurrentRanks() []ranklist.Rank {
	pss.Lock()
	defer pss.Unlock()

	if pss.currentRanks == nil {
		var err error
		pss.currentRanks, err = ranklist.CreateRankSet(pss.CurrentRankStr)
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
	if len(pss.PerRankTierStorage) >= 1 {
		return uint64(len(pss.CurrentRanks())) * pss.PerRankTierStorage[0]
	}
	return 0
}

// TotalNVMe returns the total amount of NVMe storage allocated to
// the pool, calculated from the current set of ranks multiplied
// by the per-rank NVMe allocation made at creation time.
func (pss *PoolServiceStorage) TotalNVMe() uint64 {
	if len(pss.PerRankTierStorage) >= 2 {
		sum := uint64(0)
		for _, tierStorage := range pss.PerRankTierStorage[1:] {
			sum += uint64(len(pss.CurrentRanks())) * tierStorage
		}
		return sum
	}
	return 0
}

func (pss *PoolServiceStorage) String() string {
	if pss == nil {
		return "no pool storage info available"
	}
	return fmt.Sprintf("total SCM: %s, total NVMe: %s",
		humanize.Bytes(pss.TotalSCM()),
		humanize.Bytes(pss.TotalNVMe()))
}
