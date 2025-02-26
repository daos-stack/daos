//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"fmt"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

var (
	srvComp = &daos.ComponentBuild{
		Version: "1.2.3",
	}
	cliComp = &daos.ComponentBuild{
		Version:   "1.2.3",
		BuildInfo: "1.2.4-pre12345",
	}
)

func TestPretty_printBuildInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		testComp      build.Component
		testCompBuild *daos.ComponentBuild
		expPrintStr   string
	}{
		"nil build": {
			testComp: build.ComponentServer,
		},
		"version only": {
			testComp:      build.ComponentServer,
			testCompBuild: srvComp,
			expPrintStr: fmt.Sprintf(`
Server: %s
`, srvComp.Version),
		},
		"buildinfo overrides version": {
			testComp:      build.ComponentAgent,
			testCompBuild: cliComp,
			expPrintStr: fmt.Sprintf(`
Agent: %s
`, cliComp.BuildInfo),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			printBuildInfo(&bld, tc.testComp.String(), tc.testCompBuild)

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

var (
	sysName  = "test-system"
	provStr  = "test-provider"
	hostBase = "test-host"

	sysInfo = &daos.SystemInfo{
		Name:     sysName,
		Provider: provStr,
		AccessPointRankURIs: []*daos.RankURI{
			{
				Rank: 1,
				URI:  provStr + "://" + hostBase + "1",
			},
			{
				Rank: 0,
				URI:  provStr + "://" + hostBase + "0",
			},
		},
	}
)

func TestPretty_printSystemInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		testSysInfo *daos.SystemInfo
		verbose     bool
		expPrintStr string
	}{
		"nil sysinfo": {},
		"standard sysinfo": {
			testSysInfo: sysInfo,
			expPrintStr: fmt.Sprintf(`
System Name: %s
Fabric Provider: %s
Access Points: ["%s0","%s1"]
`, sysName, provStr, hostBase, hostBase),
		},
		"empty APs list": {
			testSysInfo: func() *daos.SystemInfo {
				newInfo := new(daos.SystemInfo)
				if err := convert.Types(sysInfo, newInfo); err != nil {
					t.Fatal(err)
				}
				newInfo.AccessPointRankURIs = nil
				return newInfo
			}(),
			expPrintStr: fmt.Sprintf(`
System Name: %s
Fabric Provider: %s
Access Points: []
`, sysName, provStr),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			printSystemInfo(&bld, tc.testSysInfo, tc.verbose)

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

var healthyPool = &daos.PoolInfo{
	QueryMask:       daos.DefaultPoolQueryMask,
	State:           daos.PoolServiceStateReady,
	UUID:            uuid.New(),
	Label:           "test-pool",
	TotalTargets:    24,
	ActiveTargets:   24,
	TotalEngines:    3,
	DisabledTargets: 0,
	Version:         1,
	ServiceLeader:   2,
	ServiceReplicas: []ranklist.Rank{0, 1, 2},
	Rebuild: &daos.PoolRebuildStatus{
		State: daos.PoolRebuildStateIdle,
	},
	TierStats: []*daos.StorageUsageStats{
		{
			MediaType: daos.StorageMediaTypeScm,
			Total:     128 * humanize.GByte,
			Free:      64 * humanize.GByte,
			Min:       2420 * humanize.MByte,
			Max:       5333 * humanize.MByte,
			Mean:      2666 * humanize.GiByte,
		},
		{
			MediaType: daos.StorageMediaTypeNvme,
			Total:     6 * humanize.TByte,
			Free:      4 * humanize.TByte,
			Min:       130 * humanize.GByte,
			Max:       240 * humanize.GByte,
			Mean:      166 * humanize.TByte,
		},
	},
	EnabledRanks:     ranklist.MustCreateRankSet("[0,1,2]"),
	PoolLayoutVer:    1,
	UpgradeLayoutVer: 1,
}

func TestPretty_printPoolHealth(t *testing.T) {
	busyRebuild := &daos.PoolRebuildStatus{
		State:        daos.PoolRebuildStateBusy,
		Objects:      42,
		Records:      7,
		TotalObjects: 100,
	}

	getTestPool := func(mut func(*daos.PoolInfo) *daos.PoolInfo) *daos.PoolInfo {
		testPool := new(daos.PoolInfo)
		if err := convert.Types(healthyPool, testPool); err != nil {
			t.Fatal(err)
		}
		return mut(testPool)
	}

	for name, tc := range map[string]struct {
		pi          *daos.PoolInfo
		verbose     bool
		expPrintStr string
	}{
		"nil PoolInfo": {},
		"disabled targets": {
			pi: getTestPool(func(pi *daos.PoolInfo) *daos.PoolInfo {
				pi.DisabledTargets = 8
				return pi
			}),
			expPrintStr: fmt.Sprintf(`
%s: Degraded
`, healthyPool.Label),
		},
		"disabled targets; verbose": {
			pi: getTestPool(func(pi *daos.PoolInfo) *daos.PoolInfo {
				pi.DisabledTargets = 8
				return pi
			}),
			verbose: true,
			expPrintStr: fmt.Sprintf(`
%s: Degraded (8/24 targets disabled)
`, healthyPool.Label),
		},
		"rebuilding": {
			pi: getTestPool(func(pi *daos.PoolInfo) *daos.PoolInfo {
				pi.Rebuild = busyRebuild
				return pi
			}),
			expPrintStr: fmt.Sprintf(`
%s: Rebuilding (42.0%% complete)
`, healthyPool.Label),
		},
		"rebuilding; verbose": {
			pi: getTestPool(func(pi *daos.PoolInfo) *daos.PoolInfo {
				pi.Rebuild = busyRebuild
				return pi
			}),
			verbose: true,
			expPrintStr: fmt.Sprintf(`
%s: Rebuilding (42.0%% complete) (42/100 objects; 7 records)
`, healthyPool.Label),
		},
		"degraded, rebuilding; verbose": {
			pi: getTestPool(func(pi *daos.PoolInfo) *daos.PoolInfo {
				pi.DisabledTargets = 8
				pi.Rebuild = busyRebuild
				return pi
			}),
			verbose: true,
			expPrintStr: fmt.Sprintf(`
%s: Degraded (8/24 targets disabled),Rebuilding (42.0%% complete) (42/100 objects; 7 records)
`, healthyPool.Label),
		},
		"healthy": {
			pi: healthyPool,
			expPrintStr: fmt.Sprintf(`
%s: Healthy (4.1 TB Free)
`, healthyPool.Label),
		},
		"healthy; verbose": {
			pi:      healthyPool,
			verbose: true,
			expPrintStr: fmt.Sprintf(`
%s: Healthy (meta: 64 GB/128 GB Free, data: 4.0 TB/6.0 TB Free) (imbalances: 54%% meta, 44%% data)
`, healthyPool.Label),
		},
		"healthy no imbalances; verbose": {
			pi: getTestPool(func(pi *daos.PoolInfo) *daos.PoolInfo {
				pi.TierStats[0].Min = pi.TierStats[0].Max
				pi.TierStats[1].Min = pi.TierStats[1].Max
				return pi
			}),
			verbose: true,
			expPrintStr: fmt.Sprintf(`
%s: Healthy (meta: 64 GB/128 GB Free, data: 4.0 TB/6.0 TB Free)
`, healthyPool.Label),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			printPoolHealth(&bld, tc.pi, tc.verbose)

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

var healthyContainer = &daos.ContainerInfo{
	PoolUUID:         healthyPool.UUID,
	ContainerUUID:    uuid.New(),
	ContainerLabel:   "test-container",
	LatestSnapshot:   0,
	RedundancyFactor: 1,
	NumHandles:       1,
	NumSnapshots:     0,
	OpenTime:         1782965550217953280,
	CloseModifyTime:  1782965553047535616,
	Health:           "HEALTHY",
}

func TestPretty_printContainerHealth(t *testing.T) {
	for name, tc := range map[string]struct {
		pi          *daos.PoolInfo
		ci          *daos.ContainerInfo
		verbose     bool
		expPrintStr string
	}{
		"nil ContainerInfo": {},
		"unhealthy pool, healthy container": {
			pi: &daos.PoolInfo{
				DisabledTargets: 1,
			},
			ci: healthyContainer,
			expPrintStr: fmt.Sprintf(`
%s: Healthy (Pool Degraded)
`, healthyContainer.ContainerLabel),
		},
		"unhealthy pool, unhealthy container": {
			pi: &daos.PoolInfo{
				DisabledTargets: 1,
			},
			ci: func() *daos.ContainerInfo {
				clone := *healthyContainer
				clone.Health = "UNHEALTHY"
				return &clone
			}(),
			expPrintStr: fmt.Sprintf(`
%s: Unhealthy (Pool Degraded)
`, healthyContainer.ContainerLabel),
		},
		"healthy pool, unhealthy container": {
			pi: healthyPool,
			ci: func() *daos.ContainerInfo {
				clone := *healthyContainer
				clone.Health = "UNHEALTHY"
				return &clone
			}(),
			expPrintStr: fmt.Sprintf(`
%s: Unhealthy
`, healthyContainer.ContainerLabel),
		},
		"healthy pool, healthy container": {
			pi: healthyPool,
			ci: healthyContainer,
			expPrintStr: fmt.Sprintf(`
%s: Healthy
`, healthyContainer.ContainerLabel),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			printContainerHealth(&bld, tc.pi, tc.ci, tc.verbose)

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}

}

func TestPretty_PrintSystemHealthInfo(t *testing.T) {
	buildInfo := map[string]daos.ComponentBuild{
		build.ComponentServer.String(): *srvComp,
		build.ComponentClient.String(): *cliComp,
	}

	for name, tc := range map[string]struct {
		shi         *daos.SystemHealthInfo
		verbose     bool
		expErr      error
		expPrintStr string
	}{
		"nil SystemHealthInfo": {
			expErr: errors.New("nil"),
		},
		"healthy": {
			shi: &daos.SystemHealthInfo{
				SystemInfo:         sysInfo,
				ComponentBuildInfo: buildInfo,
				Pools: map[uuid.UUID]*daos.PoolInfo{
					healthyPool.UUID: healthyPool,
				},
				Containers: map[uuid.UUID][]*daos.ContainerInfo{
					healthyContainer.PoolUUID: {healthyContainer},
				},
			},
			expPrintStr: fmt.Sprintf(`
Component Build Information
  Server: %s
  Client: %s
System Information
  System Name: %s
  Fabric Provider: %s
  Access Points: ["%s0","%s1"]
Pool Status
  %s: Healthy (4.1 TB Free)
    Container Status
      %s: Healthy
`, srvComp.Version, cliComp.BuildInfo, sysInfo.Name, sysInfo.Provider, hostBase, hostBase, healthyPool.Label, healthyContainer.ContainerLabel),
		},
		"healthy; no pools": {
			shi: &daos.SystemHealthInfo{
				SystemInfo:         sysInfo,
				ComponentBuildInfo: buildInfo,
				Pools:              map[uuid.UUID]*daos.PoolInfo{},
			},
			expPrintStr: fmt.Sprintf(`
Component Build Information
  Server: %s
  Client: %s
System Information
  System Name: %s
  Fabric Provider: %s
  Access Points: ["%s0","%s1"]
Pool Status
  No pools in system.
`, srvComp.Version, cliComp.BuildInfo, sysInfo.Name, sysInfo.Provider, hostBase, hostBase),
		},
		"healthy; no containers": {
			shi: &daos.SystemHealthInfo{
				SystemInfo:         sysInfo,
				ComponentBuildInfo: buildInfo,
				Pools: map[uuid.UUID]*daos.PoolInfo{
					healthyPool.UUID: healthyPool,
				},
				Containers: map[uuid.UUID][]*daos.ContainerInfo{},
			},
			expPrintStr: fmt.Sprintf(`
Component Build Information
  Server: %s
  Client: %s
System Information
  System Name: %s
  Fabric Provider: %s
  Access Points: ["%s0","%s1"]
Pool Status
  %s: Healthy (4.1 TB Free)
    Container Status
      No containers in pool.
`, srvComp.Version, cliComp.BuildInfo, sysInfo.Name, sysInfo.Provider, hostBase, hostBase, healthyPool.Label),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := PrintSystemHealthInfo(&bld, tc.shi, tc.verbose)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
