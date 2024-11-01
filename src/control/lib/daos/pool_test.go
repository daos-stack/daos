//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"strings"
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

func genTestMask(modifyFn func(pqm *PoolQueryMask)) PoolQueryMask {
	testMask := PoolQueryMask(0)
	modifyFn(&testMask)
	return testMask
}

func genOptsStr(queryOpts ...string) string {
	return strings.Join(queryOpts, ",")
}

func TestDaos_PoolQueryMask(t *testing.T) {
	for name, tc := range map[string]struct {
		testMask  PoolQueryMask
		expString string
	}{
		"no mask": {
			expString: "",
		},
		"default query mask": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				*pqm = DefaultPoolQueryMask
			}),
			expString: genOptsStr(PoolQueryOptionDisabledEngines, PoolQueryOptionRebuild, PoolQueryOptionSpace),
		},
		"health-only query mask": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				*pqm = HealthOnlyPoolQueryMask
			}),
			expString: genOptsStr(PoolQueryOptionDisabledEngines, PoolQueryOptionRebuild),
		},
		"set query all=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetAll()
			}),
			expString: genOptsStr(PoolQueryOptionDisabledEngines, PoolQueryOptionEnabledEngines, PoolQueryOptionRebuild, PoolQueryOptionSpace),
		},
		"set query all=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				*pqm = PoolQueryMask(^uint64(0))
				pqm.ClearAll()
			}),
			expString: "",
		},
		"set query space=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetOptions(PoolQueryOptionSpace)
			}),
			expString: PoolQueryOptionSpace,
		},
		"set query space=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetAll()
				pqm.ClearOptions(PoolQueryOptionSpace)
			}),
			expString: genOptsStr(PoolQueryOptionDisabledEngines, PoolQueryOptionEnabledEngines, PoolQueryOptionRebuild),
		},
		"set query space=false (already false)": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.ClearOptions(PoolQueryOptionSpace)
			}),
			expString: "",
		},
		"set query rebuild=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetOptions(PoolQueryOptionRebuild)
			}),
			expString: PoolQueryOptionRebuild,
		},
		"set query rebuild=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetAll()
				pqm.ClearOptions(PoolQueryOptionRebuild)
			}),
			expString: genOptsStr(PoolQueryOptionDisabledEngines, PoolQueryOptionEnabledEngines, PoolQueryOptionSpace),
		},
		"set query enabled_engines=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetOptions(PoolQueryOptionEnabledEngines)
			}),
			expString: PoolQueryOptionEnabledEngines,
		},
		"set query enabled_engines=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetAll()
				pqm.ClearOptions(PoolQueryOptionEnabledEngines)
			}),
			expString: genOptsStr(PoolQueryOptionDisabledEngines, PoolQueryOptionRebuild, PoolQueryOptionSpace),
		},
		"set query disabled_engines=true": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetOptions(PoolQueryOptionDisabledEngines)
			}),
			expString: PoolQueryOptionDisabledEngines,
		},
		"set query disabled_engines=false": {
			testMask: genTestMask(func(pqm *PoolQueryMask) {
				pqm.SetAll()
				pqm.ClearOptions(PoolQueryOptionDisabledEngines)
			}),
			expString: genOptsStr(PoolQueryOptionEnabledEngines, PoolQueryOptionRebuild, PoolQueryOptionSpace),
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
				pqm.SetAll()
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
		"JSON-encoded string values": {
			testData: func() []byte {
				if b, err := json.Marshal("rebuild,enabled_engines"); err != nil {
					panic(err)
				} else {
					return b
				}
			}(),
			expString: "enabled_engines,rebuild",
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
