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
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

type healthCmds struct {
	Check healthCheckCmd `command:"check" description:"Perform DAOS system health checks"`
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

	sysInfo, err := cmd.apiProvider.GetSystemInfo()
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

		queryMask := daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines)
		tpi, err := queryPool(poolHdl, queryMask)
		if err != nil {
			cmd.Errorf("failed to query pool %s: %v", pool.Label, err)
			continue
		}
		pool.EnabledRanks = tpi.EnabledRanks

		if pool.DisabledTargets > 0 {
			queryMask.ClearAll()
			queryMask.SetOptions(daos.PoolQueryOptionDisabledEngines)
			tpi, err = queryPool(poolHdl, queryMask)
			if err != nil {
				cmd.Errorf("failed to query pool %s: %v", pool.Label, err)
				continue
			}
			pool.DisabledRanks = tpi.DisabledRanks
		}

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
