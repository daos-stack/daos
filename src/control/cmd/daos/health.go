//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
)

type healthCmds struct {
	Check   healthCheckCmd `command:"check" description:"Perform DAOS system health checks"`
	NetTest netTestCmd     `command:"net-test" description:"Perform non-destructive DAOS networking tests"`
}

type healthCheckCmd struct {
	daosCmd
	Verbose bool `short:"v" long:"verbose" description:"Display more detailed system health information"`
}

func collectBuildInfo(log logging.Logger, shi *daos.SystemHealthInfo) error {
	shi.ComponentBuildInfo[build.ComponentClient.String()] = daos.ComponentBuild{
		Version:   build.DaosVersion,
		BuildInfo: build.BuildInfo,
	}
	srvBuild, err := srvBuildInfo()
	if err != nil {
		log.Errorf("failed to get server build info: %v", err)
	} else {
		shi.ComponentBuildInfo[build.ComponentServer.String()] = daos.ComponentBuild{
			Version:   srvBuild.Version,
			BuildInfo: srvBuild.BuildInfo,
		}
	}

	for _, libName := range []string{"libfabric", "mercury", "libdaos"} {
		ver, path, err := build.GetLibraryInfo(libName)
		if err != nil {
			log.Debugf("failed to get %q info: %+v", libName, err)
			continue
		}
		shi.ComponentBuildInfo[libName] = daos.ComponentBuild{
			Version: ver.String(),
			LibPath: path,
		}
	}

	return nil
}

func (cmd *healthCheckCmd) Execute([]string) error {
	// TODO (DAOS-10028): Move this logic into the daos package once the API is available.
	systemHealth := &daos.SystemHealthInfo{
		ComponentBuildInfo: make(map[string]daos.ComponentBuild),
		Pools:              make(map[uuid.UUID]*daos.PoolInfo),
		Containers:         make(map[uuid.UUID][]*daos.ContainerInfo),
	}
	if err := collectBuildInfo(cmd.Logger, systemHealth); err != nil {
		return err
	}

	sysInfo, err := cmd.apiProvider.GetSystemInfo(cmd.MustLogCtx())
	if err != nil {
		cmd.Errorf("failed to query system information: %v", err)
	}
	systemHealth.SystemInfo = sysInfo

	cmd.Infof("Checking DAOS system: %s", systemHealth.SystemInfo.Name)

	pools, err := getPoolList(cmd.Logger, cmd.SysName, true)
	if err != nil {
		cmd.Errorf("failed to get pool list: %v", err)
	}

	for _, pool := range pools {
		systemHealth.Pools[pool.UUID] = pool

		poolHdl, _, err := poolConnect(pool.UUID.String(), cmd.SysName, daos.PoolConnectFlagReadOnly, false)
		if err != nil {
			cmd.Errorf("failed to connect to pool %s: %v", pool.Label, err)
			continue
		}
		defer func() {
			if err := poolDisconnectAPI(poolHdl); err != nil {
				cmd.Errorf("failed to disconnect from pool %s: %v", pool.Label, err)
			}
		}()

		queryMask := daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines,
			daos.PoolQueryOptionDeadEngines)
		if pool.DisabledTargets > 0 {
			queryMask.SetOptions(daos.PoolQueryOptionDisabledEngines)
		}
		tpi, err := queryPool(poolHdl, queryMask)
		if err != nil {
			cmd.Errorf("failed to query pool %s: %v", pool.Label, err)
			continue
		}
		pool.EnabledRanks = tpi.EnabledRanks
		pool.DisabledRanks = tpi.DisabledRanks
		pool.DeadRanks = tpi.DeadRanks

		poolConts, err := listContainers(poolHdl)
		if err != nil {
			cmd.Errorf("failed to list containers on pool %s: %v", pool.Label, err)
			continue
		}

		for _, cont := range poolConts {
			openFlags := uint(daos.ContainerOpenFlagReadOnly | daos.ContainerOpenFlagForce | daos.ContainerOpenFlagReadOnlyMetadata)
			contHdl, contInfo, err := containerOpen(poolHdl, cont.UUID.String(), openFlags, true)
			if err != nil {
				cmd.Errorf("failed to connect to container %s: %v", cont.Label, err)
				ci := &daos.ContainerInfo{
					PoolUUID:       pool.UUID,
					ContainerUUID:  cont.UUID,
					ContainerLabel: cont.Label,
					Health:         fmt.Sprintf("Unknown (%s)", err),
				}
				systemHealth.Containers[pool.UUID] = append(systemHealth.Containers[pool.UUID], ci)
				continue
			}
			ci := convertContainerInfo(pool.UUID, cont.UUID, contInfo)
			ci.ContainerLabel = cont.Label

			props, freeProps, err := getContainerProperties(contHdl, "status")
			if err != nil || len(props) == 0 {
				cmd.Errorf("failed to get container properties for %s: %v", cont.Label, err)
				ci.Health = fmt.Sprintf("Unknown (%s)", err)
			} else {
				ci.Health = props[0].String()
			}
			freeProps()

			if err := containerCloseAPI(contHdl); err != nil {
				cmd.Errorf("failed to close container %s: %v", cont.Label, err)
			}

			systemHealth.Containers[pool.UUID] = append(systemHealth.Containers[pool.UUID], ci)
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(systemHealth, nil)
	}

	var buf strings.Builder
	if err := pretty.PrintSystemHealthInfo(&buf, systemHealth, cmd.Verbose); err != nil {
		return err
	}
	cmd.Info(buf.String())

	return nil
}

type netTestCmd struct {
	cmdutil.JSONOutputCmd
	cmdutil.LogCmd
	sysCmd
	Ranks       ui.RankSetFlag  `short:"r" long:"ranks" description:"Use the specified ranks as test endpoints (default: all)"`
	Tags        ui.RankSetFlag  `short:"t" long:"tags" description:"Use the specified tags on ranks" default:"0"`
	XferSize    ui.ByteSizeFlag `short:"s" long:"size" description:"Per-RPC transfer size (send/reply)"`
	MaxInflight uint            `short:"m" long:"max-inflight" description:"Maximum number of inflight RPCs"`
	RepCount    uint            `short:"c" long:"rep-count" description:"Number of times to repeat the RPCs, per endpoint"`
	TpsBytes    bool            `short:"y" long:"bytes" description:"Show throughput values in bytes per second"`
	Verbose     bool            `short:"v" long:"verbose" description:"Display more detailed DAOS network testing information"`
}

func (cmd *netTestCmd) Execute(_ []string) error {
	cfg := &daos.SelfTestConfig{
		GroupName:       cmd.SysName,
		EndpointRanks:   cmd.Ranks.Ranks(),
		EndpointTags:    ranklist.RanksToUint32(cmd.Tags.Ranks()),
		MaxInflightRPCs: cmd.MaxInflight,
		Repetitions:     cmd.RepCount,
	}
	if cmd.XferSize.IsSet() {
		// If set, use that size, otherwise use the zero value.
		cfg.SendSizes = []uint64{cmd.XferSize.Bytes}
		cfg.ReplySizes = cfg.SendSizes
	}
	if err := cfg.SetDefaults(); err != nil {
		return err
	}

	if !cmd.JSONOutputEnabled() {
		var cfgBuf strings.Builder
		if err := pretty.PrintSelfTestConfig(&cfgBuf, cfg, cmd.Verbose); err != nil {
			return err
		}
		cmd.Info(cfgBuf.String())
		cmd.Info("Starting non-destructive network test (duration depends on performance)...\n\n")
	}

	res, err := RunSelfTest(cmd.MustLogCtx(), cfg)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(struct {
			Cfg *daos.SelfTestConfig   `json:"configuration"`
			Res []*daos.SelfTestResult `json:"results"`
		}{
			Cfg: cfg,
			Res: res,
		}, nil)
	}

	var resBuf strings.Builder
	if err := pretty.PrintSelfTestResults(&resBuf, res, cmd.Verbose, cmd.TpsBytes); err != nil {
		return err
	}
	cmd.Info(resBuf.String())

	return nil
}
