//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
)

func TestDaos_PoolInfo_Usage(t *testing.T) {
	for name, tc := range map[string]struct {
		status        int32
		scmStats      *StorageUsageStats
		nvmeStats     *StorageUsageStats
		totalTargets  uint32
		activeTargets uint32
		expUsage      []*PoolTierUsage
		expErr        error
	}{
		"successful query": {
			scmStats: &StorageUsageStats{
				MediaType: MediaTypeScm,
				Total:     humanize.GByte * 30,
				Free:      humanize.GByte * 15,
				Min:       humanize.GByte * 1.6,
				Max:       humanize.GByte * 2,
			},
			nvmeStats: &StorageUsageStats{
				MediaType: StorageMediaTypeNvme,
				Total:     humanize.GByte * 500,
				Free:      humanize.GByte * 250,
				Min:       humanize.GByte * 29.5,
				Max:       humanize.GByte * 36,
			},
			totalTargets:  8,
			activeTargets: 8,
			expUsage: []*PoolTierUsage{
				{
					TierName:  "SCM",
					Size:      humanize.GByte * 30,
					Free:      humanize.GByte * 15,
					Imbalance: 10,
				},
				{
					TierName:  "NVME",
					Size:      humanize.GByte * 500,
					Free:      humanize.GByte * 250,
					Imbalance: 10,
				},
			},
		},
		"disabled targets": {
			scmStats: &StorageUsageStats{
				MediaType: MediaTypeScm,
				Total:     humanize.GByte * 30,
				Free:      humanize.GByte * 15,
				Min:       humanize.GByte * 1.6,
				Max:       humanize.GByte * 2,
			},
			nvmeStats: &StorageUsageStats{
				MediaType: MediaTypeNvme,
				Total:     humanize.GByte * 500,
				Free:      humanize.GByte * 250,
				Min:       humanize.GByte * 29.5,
				Max:       humanize.GByte * 36,
			},
			totalTargets:  8,
			activeTargets: 4,
			expUsage: []*PoolTierUsage{
				{
					TierName:  "SCM",
					Size:      humanize.GByte * 30,
					Free:      humanize.GByte * 15,
					Imbalance: 5,
				},
				{
					TierName:  "NVME",
					Size:      humanize.GByte * 500,
					Free:      humanize.GByte * 250,
					Imbalance: 5,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			pool := &PoolInfo{
				TotalTargets:    tc.totalTargets,
				ActiveTargets:   tc.activeTargets,
				DisabledTargets: tc.activeTargets,
				TierStats:       append([]*StorageUsageStats{}, tc.scmStats, tc.nvmeStats),
			}

			if diff := cmp.Diff(tc.expUsage, pool.Usage()); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
