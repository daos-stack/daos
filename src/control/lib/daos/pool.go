//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

type (
	// PoolTierUsage describes usage of a single pool storage tier in
	// a simpler format.
	PoolTierUsage struct {
		// TierName identifies a pool's storage tier.
		TierName string `json:"tier_name"`
		// Size is the total number of bytes in the pool tier.
		Size uint64 `json:"size"`
		// Free is the number of free bytes in the pool tier.
		Free uint64 `json:"free"`
		// Imbalance is the percentage imbalance of pool tier usage
		// across all the targets.
		Imbalance uint32 `json:"imbalance"`
	}

	// StorageUsageStats represents raw DAOS storage usage statistics.
	StorageUsageStats struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		Min       uint64           `json:"min"`
		Max       uint64           `json:"max"`
		Mean      uint64           `json:"mean"`
		MediaType StorageMediaType `json:"media_type"`
	}

	// PoolRebuildStatus contains detailed information about the pool rebuild process.
	PoolRebuildStatus struct {
		Status  int32            `json:"status"`
		State   PoolRebuildState `json:"state"`
		Objects uint64           `json:"objects"`
		Records uint64           `json:"records"`
	}

	// PoolInfo contains information about the pool.
	PoolInfo struct {
		State            PoolServiceState     `json:"state"`
		UUID             uuid.UUID            `json:"uuid"`
		Label            string               `json:"label,omitempty"`
		TotalTargets     uint32               `json:"total_targets"`
		ActiveTargets    uint32               `json:"active_targets"`
		TotalEngines     uint32               `json:"total_engines"`
		DisabledTargets  uint32               `json:"disabled_targets"`
		Version          uint32               `json:"version"`
		Leader           uint32               `json:"leader"`
		ServiceReplicas  []ranklist.Rank      `json:"svc_reps"`
		Rebuild          *PoolRebuildStatus   `json:"rebuild"`
		TierStats        []*StorageUsageStats `json:"tier_stats"`
		EnabledRanks     *ranklist.RankSet    `json:"-"`
		DisabledRanks    *ranklist.RankSet    `json:"-"`
		PoolLayoutVer    uint32               `json:"pool_layout_ver"`
		UpgradeLayoutVer uint32               `json:"upgrade_layout_ver"`
	}

	// StorageTargetUsage represents DAOS target storage usage
	StorageTargetUsage struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		MediaType StorageMediaType `json:"media_type"`
	}
)

func (srs *StorageUsageStats) calcImbalance(targCount uint32) uint32 {
	spread := srs.Max - srs.Min
	return uint32((float64(spread) / (float64(srs.Total) / float64(targCount))) * 100)
}

func (pi *PoolInfo) Usage() []*PoolTierUsage {
	var tiers []*PoolTierUsage
	for _, tier := range pi.TierStats {
		tiers = append(tiers, &PoolTierUsage{
			TierName:  strings.ToUpper(tier.MediaType.String()),
			Size:      tier.Total,
			Free:      tier.Free,
			Imbalance: tier.calcImbalance(pi.ActiveTargets),
		})
	}
	return tiers
}

// RebuildState returns a string representation of the pool rebuild state.
func (pi *PoolInfo) RebuildState() string {
	if pi.Rebuild == nil {
		return "Unknown"
	}
	return pi.Rebuild.State.String()
}

// Name retrieves effective name for pool from either label or UUID.
func (pi *PoolInfo) Name() string {
	name := pi.Label
	if name == "" {
		// use short version of uuid if no label
		name = strings.Split(pi.UUID.String(), "-")[0]
	}
	return name
}

// PoolServiceState is used to represent the state of the pool service
type PoolServiceState uint

const (
	// PoolServiceStateCreating indicates that the pool service is being created
	PoolServiceStateCreating PoolServiceState = iota
	// PoolServiceStateReady indicates that the pool service is ready to be used
	PoolServiceStateReady
	// PoolServiceStateDestroying indicates that the pool service is being destroyed
	PoolServiceStateDestroying
	// PoolServiceStateDegraded indicates that the pool service is in a degraded state
	PoolServiceStateDegraded
	// PoolServiceStateUnknown indicates that the pool service state is unknown
	PoolServiceStateUnknown
)

func (pss PoolServiceState) String() string {
	if pss < PoolServiceStateCreating || pss > PoolServiceStateUnknown {
		return fmt.Sprintf("Invalid pool service state %d", pss)
	}
	return [...]string{
		mgmtpb.PoolServiceState_Creating.String(),
		mgmtpb.PoolServiceState_Ready.String(),
		mgmtpb.PoolServiceState_Destroying.String(),
		mgmtpb.PoolServiceState_Degraded.String(),
		mgmtpb.PoolServiceState_Unknown.String(),
	}[pss]
}

func (pss PoolServiceState) MarshalJSON() ([]byte, error) {
	return []byte(`"` + pss.String() + `"`), nil
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

// StorageMediaType indicates the type of storage.
type StorageMediaType int32

const (
	// StorageMediaTypeScm indicates that the media is storage class (persistent) memory
	StorageMediaTypeScm StorageMediaType = iota
	// StorageMediaTypeNvme indicates that the media is NVMe SSD
	StorageMediaTypeNvme
)

func (smt StorageMediaType) MarshalJSON() ([]byte, error) {
	typeStr, ok := mgmtpb.StorageMediaType_name[int32(smt)]
	if !ok {
		return nil, errors.Errorf("invalid storage media type %d", smt)
	}
	return []byte(`"` + strings.ToLower(typeStr) + `"`), nil
}

func (smt StorageMediaType) String() string {
	smts, ok := mgmtpb.StorageMediaType_name[int32(smt)]
	if !ok {
		return "unknown"
	}
	return strings.ToLower(smts)
}

// PoolRebuildState indicates the current state of the pool rebuild process.
type PoolRebuildState int32

const (
	// PoolRebuildStateIdle indicates that the rebuild process is idle.
	PoolRebuildStateIdle PoolRebuildState = iota
	// PoolRebuildStateDone indicates that the rebuild process has completed.
	PoolRebuildStateDone
	// PoolRebuildStateBusy indicates that the rebuild process is in progress.
	PoolRebuildStateBusy
)

func (prs PoolRebuildState) String() string {
	prss, ok := mgmtpb.PoolRebuildStatus_State_name[int32(prs)]
	if !ok {
		return "unknown"
	}
	return strings.ToLower(prss)
}

func (prs PoolRebuildState) MarshalJSON() ([]byte, error) {
	stateStr, ok := mgmtpb.PoolRebuildStatus_State_name[int32(prs)]
	if !ok {
		return nil, errors.Errorf("invalid rebuild state %d", prs)
	}
	return []byte(`"` + strings.ToLower(stateStr) + `"`), nil
}

func (prs *PoolRebuildState) UnmarshalJSON(data []byte) error {
	stateStr := strings.ToUpper(string(data))
	state, ok := mgmtpb.PoolRebuildStatus_State_value[stateStr]
	if !ok {
		// Try converting the string to an int32, to handle the
		// conversion from protobuf message using convert.Types().
		si, err := strconv.ParseInt(stateStr, 0, 32)
		if err != nil {
			return errors.Errorf("invalid rebuild state %q", stateStr)
		}

		if _, ok = mgmtpb.PoolRebuildStatus_State_name[int32(si)]; !ok {
			return errors.Errorf("invalid rebuild state %q", stateStr)
		}
		state = int32(si)
	}
	*prs = PoolRebuildState(state)

	return nil
}
