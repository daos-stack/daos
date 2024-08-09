//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const (
	minDisplayImbalance = 10
)

func printBuildInfo(out io.Writer, comp string, cb *daos.ComponentBuild) {
	if cb == nil {
		return
	}

	buildInfo := cb.BuildInfo
	if buildInfo == "" {
		buildInfo = cb.Version
	}
	fmt.Fprintf(out, "%s: %s\n", txtfmt.Title(comp), buildInfo)
}

func printSystemInfo(out io.Writer, si *daos.SystemInfo, verbose bool) {
	if si == nil {
		return
	}

	fmt.Fprintf(out, "System Name: %s\n", si.Name)
	fmt.Fprintf(out, "Fabric Provider: %s\n", si.Provider)
	if verbose {
		fmt.Fprintf(out, "Agent Path: %s\n", si.AgentPath)
	}
	apStr := "[]"
	if len(si.AccessPointRankURIs) > 0 {
		if apBytes, err := json.Marshal(si.AccessPoints()); err == nil {
			apStr = string(apBytes)
		}
	}
	fmt.Fprintf(out, "Access Points: %s\n", apStr)
}

func printPoolHealth(out io.Writer, pi *daos.PoolInfo, verbose bool) {
	if pi == nil {
		return
	}

	var healthStrings []string
	if pi.DisabledTargets > 0 {
		degStr := "Degraded"
		if verbose {
			degStr += fmt.Sprintf(" (%d/%d targets disabled)", pi.DisabledTargets, pi.TotalTargets)
		}
		healthStrings = append(healthStrings, degStr)
	}
	if pi.Rebuild != nil {
		rbi := pi.Rebuild
		if rbi.State == daos.PoolRebuildStateBusy {
			var pctCmp float64
			if rbi.TotalObjects > 0 {
				pctCmp = (float64(rbi.Objects) / float64(rbi.TotalObjects)) * 100
			}

			rbStr := fmt.Sprintf("Rebuilding (%.01f%% complete)", pctCmp)
			if verbose {
				rbStr += fmt.Sprintf(" (%d/%d objects; %d records)", rbi.Objects, rbi.TotalObjects, rbi.Records)
			}
			healthStrings = append(healthStrings, rbStr)
		}
	}

	// If there's no other pertinent info, just display some useful storage stats.
	if len(healthStrings) == 0 {
		healthStr := "Healthy"
		var metaFree uint64
		var metaTotal uint64
		var metaImbal uint32
		var dataFree uint64
		var dataTotal uint64
		var dataImbal uint32
		var totalFree uint64
		for _, tier := range pi.Usage() {
			switch tier.TierName {
			case strings.ToUpper(daos.StorageMediaTypeScm.String()):
				metaFree = tier.Free
				metaTotal = tier.Size
				metaImbal = tier.Imbalance
				totalFree += metaFree
			case strings.ToUpper(daos.StorageMediaTypeNvme.String()):
				dataFree = tier.Free
				dataTotal = tier.Size
				dataImbal = tier.Imbalance
				totalFree += dataFree
			}
		}
		if !verbose {
			healthStr += fmt.Sprintf(" (%s Free)", humanize.Bytes(totalFree))
		} else {
			healthStr += " (meta: "
			healthStr += fmt.Sprintf("%s/%s", humanize.Bytes(metaFree), humanize.Bytes(metaTotal))
			healthStr += " Free, data: "
			healthStr += fmt.Sprintf("%s/%s", humanize.Bytes(dataFree), humanize.Bytes(dataTotal))
			healthStr += " Free)"
			if metaImbal > minDisplayImbalance || dataImbal > minDisplayImbalance {
				healthStr += fmt.Sprintf(" (imbalances: %d%% meta, %d%% data)", metaImbal, dataImbal)
			}
		}
		healthStrings = append(healthStrings, healthStr)
	}
	fmt.Fprintf(out, "%s: %s\n", pi.Name(), strings.Join(healthStrings, ","))
}

func printContainerHealth(out io.Writer, ci *daos.ContainerInfo, verbose bool) {
	if ci == nil {
		return
	}

	fmt.Fprintf(out, "%s: %s\n", ci.Name(), txtfmt.Title(ci.Health))
}

// PrintSystemHealthInfo pretty-prints the supplied system health struct.
func PrintSystemHealthInfo(out io.Writer, shi *daos.SystemHealthInfo, verbose bool) error {
	if shi == nil {
		return errors.Errorf("nil %T", shi)
	}

	iw := txtfmt.NewIndentWriter(out)

	fmt.Fprintln(out, "Component Build Information")
	srvStr := build.ComponentServer.String()
	cliStr := build.ComponentClient.String()
	if srvBuild, found := shi.ComponentBuildInfo[srvStr]; found {
		printBuildInfo(iw, srvStr, &srvBuild)
	}
	if cliBuild, found := shi.ComponentBuildInfo[cliStr]; found {
		printBuildInfo(iw, cliStr, &cliBuild)
	}
	if verbose {
		fmt.Fprintln(out, "Client Library Information")
		for comp, bi := range shi.ComponentBuildInfo {
			if comp != srvStr && comp != cliStr {
				printBuildInfo(iw, comp, &bi)
			}
		}
	}

	if shi.SystemInfo != nil {
		fmt.Fprintln(out, "System Information")
		printSystemInfo(iw, shi.SystemInfo, verbose)
	}

	fmt.Fprintln(out, "Pool Status")
	if len(shi.Pools) > 0 {
		for _, pool := range shi.Pools {
			iiw := txtfmt.NewIndentWriter(iw)
			printPoolHealth(iw, pool, verbose)
			fmt.Fprintln(iiw, "Container Status")
			iiiw := txtfmt.NewIndentWriter(iiw)
			if len(shi.Containers[pool.UUID]) > 0 {
				for _, cont := range shi.Containers[pool.UUID] {
					printContainerHealth(iiiw, cont, verbose)
				}
			} else {
				fmt.Fprintln(iiiw, "No containers in pool.")
			}
		}
	} else {
		fmt.Fprintln(iw, "No pools in system.")
	}

	return nil
}
