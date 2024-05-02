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
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
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

func genTestMask(xfrm func(pqm *PoolQueryMask)) PoolQueryMask {
	testMask := PoolQueryMask(0)
	xfrm(&testMask)
	return testMask
}

func TestDaos_PoolQueryMask(t *testing.T) {
	for name, tc := range map[string]struct {
		testMask  PoolQueryMask
		expString string
	}{
		"no mask": {
			expString: "",
		},
		"set query all=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryAll(true)
			}),
			expString: "disabled_engines,enabled_engines,rebuild,space",
		},
		"set query all=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				*pqm = PoolQueryMask(^uint64(0))
				pqm.SetQueryAll(false)
			}),
			expString: "",
		},
		"set query space=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQuerySpace(true)
			}),
			expString: "space",
		},
		"set query space=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryAll(true)
				pqm.SetQuerySpace(false)
			}),
			expString: "disabled_engines,enabled_engines,rebuild",
		},
		"set query space=false (already false)": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQuerySpace(false)
			}),
			expString: "",
		},
		"set query rebuild=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryRebuild(true)
			}),
			expString: "rebuild",
		},
		"set query rebuild=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryAll(true)
				pqm.SetQueryRebuild(false)
			}),
			expString: "disabled_engines,enabled_engines,space",
		},
		"set query engines_enabled=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryEnabledEngines(true)
			}),
			expString: "enabled_engines",
		},
		"set query engines_enabled=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryAll(true)
				pqm.SetQueryEnabledEngines(false)
			}),
			expString: "disabled_engines,rebuild,space",
		},
		"set query engines_disabled=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryDisabledEngines(true)
			}),
			expString: "disabled_engines",
		},
		"set query engines_disabled=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryAll(true)
				pqm.SetQueryDisabledEngines(false)
			}),
			expString: "enabled_engines,rebuild,space",
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expString, tc.testMask.String()); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDaos_PoolQueryMaskMarshalJSON(t *testing.T) {
	// NB: The MarshalJSON implementation uses the stringer, so
	// there's no point in testing all of the options here.
	for name, tc := range map[string]struct {
		testMask PoolQueryMask
		expJSON  []byte
	}{
		"no mask": {
			expJSON: []byte(`""`),
		},
		"set query all=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetQueryAll(true)
			}),
			expJSON: []byte(`"disabled_engines,enabled_engines,rebuild,space"`),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotJSON, err := tc.testMask.MarshalJSON()
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
			if diff := cmp.Diff(tc.expJSON, gotJSON); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDaos_PoolQueryMaskUnmarshalJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		testData  []byte
		expString string
		expErr    error
	}{
		"unknown value": {
			testData: []byte("rebuild,foo"),
			expErr:   errors.New("foo"),
		},
		"empty value": {
			expString: "",
		},
		"uint64 value": {
			testData:  []byte("18446744073709551603"),
			expString: "rebuild,space",
		},
		"string values": {
			testData:  []byte("rebuild,disabled_engines"),
			expString: "disabled_engines,rebuild",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var testMask PoolQueryMask

			gotErr := testMask.UnmarshalJSON(tc.testData)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expString, testMask.String()); diff != "" {
				t.Fatalf("Unexpected mask after UnmarshalJSON (-want, +got):\n%s\n", diff)
			}
		})
	}
}
