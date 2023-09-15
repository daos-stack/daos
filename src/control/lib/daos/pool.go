package daos

import (
	"encoding/json"
	"strconv"
	"strings"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#include <daos.h>
#include <daos_pool.h>
*/
import "C"

type (
	StorageMediaType     int32
	PoolQueryTargetType  int32
	PoolQueryTargetState int32

	// PoolQueryTargetInfo contains information about a single target
	PoolQueryTargetInfo struct {
		Type  PoolQueryTargetType  `json:"target_type"`
		State PoolQueryTargetState `json:"target_state"`
		Space []*StorageTargetUsage
	}

	// StorageTargetUsage represents DAOS target storage usage
	StorageTargetUsage struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		MediaType StorageMediaType `json:"media_type"`
	}

	// StorageUsageStats represents DAOS storage usage statistics.
	StorageUsageStats struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		Min       uint64           `json:"min"`
		Max       uint64           `json:"max"`
		Mean      uint64           `json:"mean"`
		MediaType StorageMediaType `json:"media_type"`
	}

	// PoolRebuildState indicates the current state of the pool rebuild process.
	PoolRebuildState int32

	// PoolRebuildStatus contains detailed information about the pool rebuild process.
	PoolRebuildStatus struct {
		Status  int32            `json:"status"`
		State   PoolRebuildState `json:"state"`
		Objects uint64           `json:"objects"`
		Records uint64           `json:"records"`
	}

	PoolInfo struct {
		UUID             uuid.UUID            `json:"uuid"`
		Label            string               `json:"label,omitempty"`
		TotalTargets     uint32               `json:"total_targets"`
		ActiveTargets    uint32               `json:"active_targets"`
		TotalEngines     uint32               `json:"total_engines"`
		DisabledTargets  uint32               `json:"disabled_targets"`
		Version          uint32               `json:"version"`
		Leader           uint32               `json:"leader"`
		Rebuild          *PoolRebuildStatus   `json:"rebuild"`
		TierStats        []*StorageUsageStats `json:"tier_stats"`
		EnabledRanks     *ranklist.RankSet    `json:"-"`
		DisabledRanks    *ranklist.RankSet    `json:"-"`
		PoolLayoutVer    uint32               `json:"pool_layout_ver"`
		UpgradeLayoutVer uint32               `json:"upgrade_layout_ver"`
	}
)

const (
	// StorageMediaTypeScm indicates that the media is storage class (persistent) memory
	StorageMediaTypeScm StorageMediaType = iota
	// StorageMediaTypeNvme indicates that the media is NVMe SSD
	StorageMediaTypeNvme
)

const (
	PoolRebuildStateIdle = 0
	PoolRebuildStateBusy = 1
	PoolRebuildStateDone = 2
	// PoolRebuildStateInProgress indicates that the rebuild process is in progress.
	PoolRebuildStateInProgress = C.DRS_IN_PROGRESS + 10
	// PoolRebuildStateNotStarted indicates that the rebuild process has not started.
	PoolRebuildStateNotStarted = C.DRS_NOT_STARTED + 10
	// PoolRebuildStateCompleted indicates that the rebuild process has completed.
	PoolRebuildStateCompleted = C.DRS_COMPLETED + 10
)

func (pi *PoolInfo) MarshalJSON() ([]byte, error) {
	if pi == nil {
		return []byte("null"), nil
	}

	type Alias PoolInfo
	aux := &struct {
		EnabledRanks  *[]ranklist.Rank `json:"enabled_ranks,omitempty"`
		DisabledRanks *[]ranklist.Rank `json:"disabled_ranks,omitempty"`
		*Alias
	}{
		Alias: (*Alias)(pi),
	}

	if pi.EnabledRanks != nil {
		ranks := pi.EnabledRanks.Ranks()
		aux.EnabledRanks = &ranks
	}

	if pi.DisabledRanks != nil {
		ranks := pi.DisabledRanks.Ranks()
		aux.DisabledRanks = &ranks
	}

	return json.Marshal(&aux)
}

func unmarshalRankSet(ranks string) (*ranklist.RankSet, error) {
	switch ranks {
	case "":
		return nil, nil
	case "[]":
		return &ranklist.RankSet{}, nil
	default:
		return ranklist.CreateRankSet(ranks)
	}
}

func (pi *PoolInfo) UnmarshalJSON(data []byte) error {
	type Alias PoolInfo
	aux := &struct {
		EnabledRanks  string `json:"enabled_ranks"`
		DisabledRanks string `json:"disabled_ranks"`
		*Alias
	}{
		Alias: (*Alias)(pi),
	}

	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	if rankSet, err := unmarshalRankSet(aux.EnabledRanks); err != nil {
		return err
	} else {
		pi.EnabledRanks = rankSet
	}

	if rankSet, err := unmarshalRankSet(aux.DisabledRanks); err != nil {
		return err
	} else {
		pi.DisabledRanks = rankSet
	}

	return nil
}

func (prs PoolRebuildState) String() string {
	switch prs {
	case PoolRebuildStateInProgress:
		return "in progress"
	case PoolRebuildStateNotStarted:
		return "not started"
	case PoolRebuildStateCompleted:
		return "completed"
	case PoolRebuildStateBusy:
		return "busy"
	case PoolRebuildStateIdle:
		return "idle"
	case PoolRebuildStateDone:
		return "done"
	default:
		return "unknown"
	}
}

func (prs PoolRebuildState) MarshalJSON() ([]byte, error) {
	return []byte(`"` + prs.String() + `"`), nil
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

func (pqtt PoolQueryTargetType) MarshalJSON() ([]byte, error) {
	typeStr, ok := mgmtpb.PoolQueryTargetInfo_TargetType_name[int32(pqtt)]
	if !ok {
		return nil, errors.Errorf("invalid target type %d", pqtt)
	}
	return []byte(`"` + strings.ToLower(typeStr) + `"`), nil
}

func (ptt PoolQueryTargetType) String() string {
	ptts, ok := mgmtpb.PoolQueryTargetInfo_TargetType_name[int32(ptt)]
	if !ok {
		return "invalid"
	}
	return strings.ToLower(ptts)
}

const (
	PoolTargetStateUnknown PoolQueryTargetState = iota
	// PoolTargetStateDownOut indicates the target is not available
	PoolTargetStateDownOut
	// PoolTargetStateDown indicates the target is not available, may need rebuild
	PoolTargetStateDown
	// PoolTargetStateUp indicates the target is up
	PoolTargetStateUp
	// PoolTargetStateUpIn indicates the target is up and running
	PoolTargetStateUpIn
	// PoolTargetStateNew indicates the target is in an intermediate state (pool map change)
	PoolTargetStateNew
	// PoolTargetStateDrain indicates the target is being drained
	PoolTargetStateDrain
)

func (pqts PoolQueryTargetState) MarshalJSON() ([]byte, error) {
	stateStr, ok := mgmtpb.PoolQueryTargetInfo_TargetState_name[int32(pqts)]
	if !ok {
		return nil, errors.Errorf("invalid target state %d", pqts)
	}
	return []byte(`"` + strings.ToLower(stateStr) + `"`), nil
}

func (pts PoolQueryTargetState) String() string {
	ptss, ok := mgmtpb.PoolQueryTargetInfo_TargetState_name[int32(pts)]
	if !ok {
		return "invalid"
	}
	return strings.ToLower(ptss)
}
