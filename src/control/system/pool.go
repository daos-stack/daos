//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
)

const (
	// PoolServiceStateCreating indicates that the pool service is being created
	PoolServiceStateCreating PoolServiceState = iota
	// PoolServiceStateReady indicates that the pool service is ready to be used
	PoolServiceStateReady
	// PoolServiceStateDestroying indicates that the pool service is being destroyed
	PoolServiceStateDestroying
	// PoolServiceStateDegraded indicates that the pool service is being Degraded
	PoolServiceStateDegraded
	// PoolServiceStateUnknown indicates that the pool service is Unknown state
	PoolServiceStateUnknown
)

type (
	// PoolServiceState is used to represent the state of the pool service
	PoolServiceState uint

	// PoolServiceStorage holds information about the pool storage.
	PoolServiceStorage struct {
		sync.Mutex
		CreationRankStr    string   // string rankset set at creation
		creationRanks      *RankSet // used to reconstitute the rankset
		CurrentRankStr     string   // string rankset representing current ranks
		currentRanks       *RankSet // used to reconstitute the rankset
		PerRankTierStorage []uint64 // storage allocated to each tier on a rank
	}

	// PoolService represents a pool service created to manage metadata
	// for a DAOS Pool.
	PoolService struct {
		PoolUUID   uuid.UUID
		PoolLabel  string
		State      PoolServiceState
		Replicas   []Rank
		Storage    *PoolServiceStorage
		LastUpdate time.Time
	}
)

// NewPoolService returns a properly-initialized *PoolService.
func NewPoolService(uuid uuid.UUID, tierStorage []uint64, ranks []Rank) *PoolService {
	rs := RankSetFromRanks(ranks)
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

func (pss PoolServiceState) String() string {
	return [...]string{
		"Creating",
		"Ready",
		"Destroying",
		"Degraded",
		"Unknown",
	}[pss]
}

func (pss PoolServiceState) MarshalJSON() ([]byte, error) {
	stateStr, ok := mgmtpb.PoolServiceState_name[int32(pss)]
	if !ok {
		return nil, errors.Errorf("invalid Pool Service state %d", pss)
	}
	return []byte(`"` + stateStr + `"`), nil
}

func (pss *PoolServiceState) UnmarshalJSON(data []byte) error {
	stateStr := strings.Trim(string(data), "\"")

	state, ok := mgmtpb.PoolServiceState_value[stateStr]
	if !ok {
		// Try converting the string to an int32, to handle the
		// conversion from protobuf message using convert.Types().
		si, err := strconv.ParseInt(stateStr, 0, 32)
		if err != nil {
			return errors.Errorf("invalid Pool Service state number parse %q", stateStr)
		}

		if _, ok = mgmtpb.PoolServiceState_name[int32(si)]; !ok {
			return errors.Errorf("invalid Pool Service state name lookup %q", stateStr)
		}
		state = int32(si)
	}
	*pss = PoolServiceState(state)

	return nil
}
