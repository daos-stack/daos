package daos

import "github.com/google/uuid"

type (
	PoolInfo struct {
		UUID            uuid.UUID `json:"uuid"`
		Label           string    `json:"label,omitempty"`
		TotalTargets    uint32    `json:"total_targets"`
		ActiveTargets   uint32    `json:"active_targets"`
		TotalEngines    uint32    `json:"total_engines"`
		DisabledTargets uint32    `json:"disabled_targets"`
		Version         uint32    `json:"version"`
		Leader          uint32    `json:"leader"`
		/*Rebuild          *PoolRebuildStatus   `json:"rebuild"`
		TierStats        []*StorageUsageStats `json:"tier_stats"`
		EnabledRanks     *ranklist.RankSet    `json:"-"`
		DisabledRanks    *ranklist.RankSet    `json:"-"`*/
		PoolLayoutVer    uint32 `json:"pool_layout_ver"`
		UpgradeLayoutVer uint32 `json:"upgrade_layout_ver"`
	}
)
