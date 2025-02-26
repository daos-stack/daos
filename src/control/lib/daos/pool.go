//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"sort"
	"strconv"
	"strings"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#include <stdint.h>

#include <daos_pool.h>
*/
import "C"

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
		Status       int32            `json:"status"`
		State        PoolRebuildState `json:"state"`
		Objects      uint64           `json:"objects"`
		Records      uint64           `json:"records"`
		TotalObjects uint64           `json:"total_objects"`
	}

	// PoolInfo contains information about the pool.
	PoolInfo struct {
		QueryMask        PoolQueryMask        `json:"query_mask"`
		State            PoolServiceState     `json:"state"`
		UUID             uuid.UUID            `json:"uuid"`
		Label            string               `json:"label,omitempty"`
		TotalTargets     uint32               `json:"total_targets"`
		ActiveTargets    uint32               `json:"active_targets"`
		TotalEngines     uint32               `json:"total_engines"`
		DisabledTargets  uint32               `json:"disabled_targets"`
		Version          uint32               `json:"version"`
		ServiceLeader    uint32               `json:"svc_ldr"`
		ServiceReplicas  []ranklist.Rank      `json:"svc_reps,omitempty"`
		Rebuild          *PoolRebuildStatus   `json:"rebuild"`
		TierStats        []*StorageUsageStats `json:"tier_stats"`
		EnabledRanks     *ranklist.RankSet    `json:"enabled_ranks,omitempty"`
		DisabledRanks    *ranklist.RankSet    `json:"disabled_ranks,omitempty"`
		DeadRanks        *ranklist.RankSet    `json:"dead_ranks,omitempty"`
		PoolLayoutVer    uint32               `json:"pool_layout_ver"`
		UpgradeLayoutVer uint32               `json:"upgrade_layout_ver"`
		MemFileBytes     uint64               `json:"mem_file_bytes"`
		MdOnSsdActive    bool                 `json:"md_on_ssd_active"`
	}

	PoolQueryTargetType  int32
	PoolQueryTargetState int32

	// PoolQueryTargetInfo contains information about a single target
	PoolQueryTargetInfo struct {
		Type          PoolQueryTargetType  `json:"target_type"`
		State         PoolQueryTargetState `json:"target_state"`
		Space         []*StorageUsageStats `json:"space"`
		MemFileBytes  uint64               `json:"mem_file_bytes"`
		MdOnSsdActive bool                 `json:"md_on_ssd_active"`
	}

	// StorageTargetUsage represents DAOS target storage usage
	StorageTargetUsage struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		MediaType StorageMediaType `json:"media_type"`
	}

	// PoolQueryOption is used to supply pool query options.
	PoolQueryOption string

	// PoolQueryMask implements a bitmask for pool query options.
	PoolQueryMask C.uint64_t

	// PoolConnectFlag represents DAOS pool connect options.
	PoolConnectFlag uint
)

const (
	// DefaultPoolQueryMask defines the default pool query mask.
	DefaultPoolQueryMask = PoolQueryMask(^uint64(0) &^ (C.DPI_ENGINES_ENABLED | C.DPI_ENGINES_DEAD))
	// HealthOnlyPoolQueryMask defines the mask for health-only queries.
	HealthOnlyPoolQueryMask = PoolQueryMask(^uint64(0) &^ (C.DPI_ENGINES_ENABLED | C.DPI_SPACE))

	// PoolQueryOptionSpace retrieves storage space usage as part of the pool query.
	PoolQueryOptionSpace PoolQueryOption = "space"
	// PoolQueryOptionRebuild retrieves pool rebuild status as part of the pool query.
	PoolQueryOptionRebuild PoolQueryOption = "rebuild"
	// PoolQueryOptionEnabledEngines retrieves enabled engines as part of the pool query.
	PoolQueryOptionEnabledEngines PoolQueryOption = "enabled_engines"
	// PoolQueryOptionDisabledEngines retrieves disabled engines as part of the pool query.
	PoolQueryOptionDisabledEngines PoolQueryOption = "disabled_engines"
	// PoolQueryOptionDeadEngines retrieves dead engines as part of the pool query.
	PoolQueryOptionDeadEngines PoolQueryOption = "dead_engines"

	// PoolConnectFlagReadOnly indicates that the connection is read-only.
	PoolConnectFlagReadOnly PoolConnectFlag = C.DAOS_PC_RO
	// PoolConnectFlagReadWrite indicates that the connection is read-write.
	PoolConnectFlagReadWrite PoolConnectFlag = C.DAOS_PC_RW
	// PoolConnectFlagExclusive indicates that the connection is exclusive.
	PoolConnectFlagExclusive PoolConnectFlag = C.DAOS_PC_EX
)

func (pcf PoolConnectFlag) String() string {
	flagStrs := []string{}
	if pcf&PoolConnectFlagReadOnly != 0 {
		flagStrs = append(flagStrs, "read-only")
	}
	if pcf&PoolConnectFlagReadWrite != 0 {
		flagStrs = append(flagStrs, "read-write")
	}
	if pcf&PoolConnectFlagExclusive != 0 {
		flagStrs = append(flagStrs, "exclusive")
	}
	sort.Strings(flagStrs)
	return strings.Join(flagStrs, ",")
}

func (pqo PoolQueryOption) String() string {
	return string(pqo)
}

var poolQueryOptMap = map[C.int]PoolQueryOption{
	C.DPI_SPACE:            PoolQueryOptionSpace,
	C.DPI_REBUILD_STATUS:   PoolQueryOptionRebuild,
	C.DPI_ENGINES_ENABLED:  PoolQueryOptionEnabledEngines,
	C.DPI_ENGINES_DISABLED: PoolQueryOptionDisabledEngines,
	C.DPI_ENGINES_DEAD:     PoolQueryOptionDeadEngines,
}

func resolvePoolQueryOpt(name PoolQueryOption) (C.int, error) {
	for opt, optName := range poolQueryOptMap {
		if name == optName {
			return opt, nil
		}
	}
	return 0, errors.Errorf("invalid pool query option: %q", name)
}

// MustNewPoolQueryMask returns a PoolQueryMask initialized with the specified options.
// NB: If an error occurs due to an invalid option, it panics.
func MustNewPoolQueryMask(options ...PoolQueryOption) (mask PoolQueryMask) {
	if err := mask.SetOptions(options...); err != nil {
		panic(err)
	}
	return
}

// SetOptions sets the pool query mask to include the specified options.
func (pqm *PoolQueryMask) SetOptions(options ...PoolQueryOption) error {
	for _, optName := range options {
		if opt, err := resolvePoolQueryOpt(optName); err != nil {
			return err
		} else {
			*pqm |= PoolQueryMask(opt)
		}
	}
	return nil
}

// ClearOptions clears the pool query mask of the specified options.
func (pqm *PoolQueryMask) ClearOptions(options ...PoolQueryOption) error {
	for _, optName := range options {
		if opt, err := resolvePoolQueryOpt(optName); err != nil {
			return err
		} else {
			*pqm &^= PoolQueryMask(opt)
		}
	}
	return nil
}

// SetAll sets the pool query mask to include all pool query options.
func (pqm *PoolQueryMask) SetAll() {
	*pqm = PoolQueryMask(^uint64(0)) // DPI_ALL is -1
}

// ClearAll clears the pool query mask of all pool query options.
func (pqm *PoolQueryMask) ClearAll() {
	*pqm = 0
}

// HasOption returns true if the pool query mask includes the specified option.
func (pqm PoolQueryMask) HasOption(option PoolQueryOption) bool {
	return strings.Contains(pqm.String(), option.String())
}

func (pqm PoolQueryMask) String() string {
	var flags []string
	for i := 0; i < (C.sizeof_int*8)-1; i++ {
		opt := C.int(1 << i)
		if flag, ok := poolQueryOptMap[opt]; ok {
			if pqm&PoolQueryMask(opt) != 0 {
				flags = append(flags, flag.String())
			}
		}
	}
	sort.Strings(flags)
	return strings.Join(flags, ",")
}

func (pqm PoolQueryMask) MarshalJSON() ([]byte, error) {
	return json.Marshal(pqm.String())
}

func (pqm *PoolQueryMask) UnmarshalJSON(data []byte) error {
	if len(data) == 0 {
		*pqm = 0
		return nil
	}

	val, err := strconv.ParseUint(string(data), 10, 64)
	if err == nil {
		*pqm = PoolQueryMask(val)
		return nil
	}

	var newVal PoolQueryMask
	for _, opt := range strings.Split(strings.Trim(string(data), "\""), ",") {
		for k, v := range poolQueryOptMap {
			if v.String() == opt {
				newVal |= PoolQueryMask(k)
				goto next
			}
		}
		return errors.Errorf("invalid pool query option: %s", opt)
	next:
	}
	*pqm = newVal

	return nil
}

func (srs *StorageUsageStats) calcImbalance(targCount uint32) uint32 {
	spread := srs.Max - srs.Min
	return uint32((float64(spread) / (float64(srs.Total) / float64(targCount))) * 100)
}

func (pi *PoolInfo) MarshalJSON() ([]byte, error) {
	type Alias PoolInfo

	return json.Marshal(&struct {
		*Alias
		Usage []*PoolTierUsage `json:"usage,omitempty"`
	}{
		Alias: (*Alias)(pi),
		Usage: pi.Usage(),
	})
}

// Usage returns a slice of PoolTierUsage objects describing the pool's storage
// usage in a simpler format.
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
	PoolServiceStateCreating = PoolServiceState(mgmtpb.PoolServiceState_Creating)
	// PoolServiceStateReady indicates that the pool service is ready to be used
	PoolServiceStateReady = PoolServiceState(mgmtpb.PoolServiceState_Ready)
	// PoolServiceStateDestroying indicates that the pool service is being destroyed
	PoolServiceStateDestroying = PoolServiceState(mgmtpb.PoolServiceState_Destroying)
	// PoolServiceStateDegraded indicates that the pool service is in a degraded state
	PoolServiceStateDegraded = PoolServiceState(mgmtpb.PoolServiceState_Degraded)
	// PoolServiceStateUnknown indicates that the pool service state is unknown
	PoolServiceStateUnknown = PoolServiceState(mgmtpb.PoolServiceState_Unknown)
)

func (pss PoolServiceState) String() string {
	psss, ok := mgmtpb.PoolServiceState_name[int32(pss)]
	if !ok {
		return "invalid"
	}
	return psss
}

func (pss PoolServiceState) MarshalJSON() ([]byte, error) {
	return []byte(`"` + pss.String() + `"`), nil
}

func unmarshalStrVal(inStr string, name2Vals map[string]int32, val2Names map[int32]string) (int32, error) {
	intVal, found := name2Vals[inStr]
	if found {
		return intVal, nil
	}
	// Try converting the string to an int32, to handle the
	// conversion from protobuf message using convert.Types().
	val, err := strconv.ParseInt(inStr, 0, 32)
	if err != nil {
		return 0, errors.Errorf("non-numeric string value %q", inStr)
	}

	if _, ok := val2Names[int32(val)]; !ok {
		return 0, errors.Errorf("unable to resolve string to value %q", inStr)
	}
	return int32(val), nil
}

func (pss *PoolServiceState) UnmarshalJSON(data []byte) error {
	stateStr := strings.Trim(string(data), "\"")

	state, err := unmarshalStrVal(stateStr, mgmtpb.PoolServiceState_value, mgmtpb.PoolServiceState_name)
	if err != nil {
		return errors.Wrap(err, "failed to unmarshal PoolServiceState")
	}
	*pss = PoolServiceState(state)

	return nil
}

// StorageMediaType indicates the type of storage.
type StorageMediaType int32

const (
	// StorageMediaTypeScm indicates that the media is storage class (persistent) memory
	StorageMediaTypeScm = StorageMediaType(mgmtpb.StorageMediaType_SCM)
	// StorageMediaTypeNvme indicates that the media is NVMe SSD
	StorageMediaTypeNvme = StorageMediaType(mgmtpb.StorageMediaType_NVME)
	// StorageMediaTypeMax indicates the end of the StorageMediaType array
	StorageMediaTypeMax = StorageMediaType(StorageMediaTypeNvme + 1)
)

func (smt StorageMediaType) String() string {
	smts, ok := mgmtpb.StorageMediaType_name[int32(smt)]
	if !ok {
		return "unknown"
	}
	return strings.ToLower(smts)
}

func (smt StorageMediaType) MarshalJSON() ([]byte, error) {
	return []byte(`"` + smt.String() + `"`), nil
}

func (smt *StorageMediaType) UnmarshalJSON(data []byte) error {
	mediaTypeStr := strings.ToUpper(strings.Trim(string(data), "\""))

	sm, err := unmarshalStrVal(mediaTypeStr, mgmtpb.StorageMediaType_value, mgmtpb.StorageMediaType_name)
	if err != nil {
		return errors.Wrap(err, "failed to unmarshal StorageMediaType")
	}
	*smt = StorageMediaType(sm)

	return nil
}

// PoolRebuildState indicates the current state of the pool rebuild process.
type PoolRebuildState int32

const (
	// PoolRebuildStateIdle indicates that the rebuild process is idle.
	PoolRebuildStateIdle = PoolRebuildState(mgmtpb.PoolRebuildStatus_IDLE)
	// PoolRebuildStateDone indicates that the rebuild process has completed.
	PoolRebuildStateDone = PoolRebuildState(mgmtpb.PoolRebuildStatus_DONE)
	// PoolRebuildStateBusy indicates that the rebuild process is in progress.
	PoolRebuildStateBusy = PoolRebuildState(mgmtpb.PoolRebuildStatus_BUSY)
)

func (prs PoolRebuildState) String() string {
	prss, ok := mgmtpb.PoolRebuildStatus_State_name[int32(prs)]
	if !ok {
		return "unknown"
	}
	return strings.ToLower(prss)
}

func (prs PoolRebuildState) MarshalJSON() ([]byte, error) {
	return []byte(`"` + prs.String() + `"`), nil
}

func (prs *PoolRebuildState) UnmarshalJSON(data []byte) error {
	stateStr := strings.ToUpper(strings.Trim(string(data), "\""))

	state, err := unmarshalStrVal(stateStr, mgmtpb.PoolRebuildStatus_State_value, mgmtpb.PoolRebuildStatus_State_name)
	if err != nil {
		return errors.Wrap(err, "failed to unmarshal PoolRebuildState")
	}
	*prs = PoolRebuildState(state)

	return nil
}

func (ptt PoolQueryTargetType) String() string {
	ptts, ok := mgmtpb.PoolQueryTargetInfo_TargetType_name[int32(ptt)]
	if !ok {
		return "invalid"
	}
	return strings.ToLower(ptts)
}

func (pqtt PoolQueryTargetType) MarshalJSON() ([]byte, error) {
	return []byte(`"` + pqtt.String() + `"`), nil
}

const (
	PoolTargetStateUnknown = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_STATE_UNKNOWN)
	// PoolTargetStateDownOut indicates the target is not available
	PoolTargetStateDownOut = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_DOWN_OUT)
	// PoolTargetStateDown indicates the target is not available, may need rebuild
	PoolTargetStateDown = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_DOWN)
	// PoolTargetStateUp indicates the target is up
	PoolTargetStateUp = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_UP)
	// PoolTargetStateUpIn indicates the target is up and running
	PoolTargetStateUpIn = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_UP_IN)
	// PoolTargetStateNew indicates the target is in an intermediate state (pool map change)
	PoolTargetStateNew = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_NEW)
	// PoolTargetStateDrain indicates the target is being drained
	PoolTargetStateDrain = PoolQueryTargetState(mgmtpb.PoolQueryTargetInfo_DRAIN)
)

func (pqts PoolQueryTargetState) String() string {
	pqtss, ok := mgmtpb.PoolQueryTargetInfo_TargetState_name[int32(pqts)]
	if !ok {
		return "invalid"
	}
	return strings.ToLower(pqtss)
}

func (pqts PoolQueryTargetState) MarshalJSON() ([]byte, error) {
	return []byte(`"` + pqts.String() + `"`), nil
}
