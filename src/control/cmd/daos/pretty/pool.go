//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const msgNoPools = "No pools in system"

func printPoolTiers(memFileBytes uint64, suss []*daos.StorageUsageStats, w *txtfmt.ErrWriter, fullStats bool) {
	mdOnSSD := memFileBytes != 0
	for tierIdx, tierStats := range suss {
		if mdOnSSD {
			if tierIdx == 0 {
				if fullStats {
					fmt.Fprintf(w, "- Total memory-file size: %s\n",
						humanize.Bytes(memFileBytes))
				}
				fmt.Fprintf(w, "- Metadata storage:\n")
			} else {
				fmt.Fprintf(w, "- Data storage:\n")
			}
		} else {
			if tierIdx >= int(daos.StorageMediaTypeMax) {
				// Print unknown type tiers.
				tierStats.MediaType = daos.StorageMediaTypeMax
			}
			fmt.Fprintf(w, "- Storage tier %d (%s):\n", tierIdx,
				strings.ToUpper(tierStats.MediaType.String()))
		}

		fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierStats.Total))
		if fullStats {
			fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
				humanize.Bytes(tierStats.Free), humanize.Bytes(tierStats.Min),
				humanize.Bytes(tierStats.Max), humanize.Bytes(tierStats.Mean))
		} else {
			fmt.Fprintf(w, "  Free: %s\n", humanize.Bytes(tierStats.Free))
		}
	}
}

// PrintPoolInfo generates a human-readable representation of the supplied
// PoolQueryResp struct and writes it to the supplied io.Writer.
func PrintPoolInfo(pi *daos.PoolInfo, out io.Writer) error {
	if pi == nil {
		return errors.Errorf("nil %T", pi)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query` output.
	fmt.Fprintf(w, "Pool %s, ntarget=%d, disabled=%d, leader=%d, version=%d, state=%s\n",
		pi.UUID, pi.TotalTargets, pi.DisabledTargets, pi.ServiceLeader, pi.Version, pi.State)

	if pi.PoolLayoutVer != pi.UpgradeLayoutVer {
		fmt.Fprintf(w, "Pool layout out of date (%d < %d) -- see `dmg pool upgrade` for details.\n",
			pi.PoolLayoutVer, pi.UpgradeLayoutVer)
	}
	fmt.Fprintln(w, "Pool health info:")
	if pi.EnabledRanks != nil && pi.EnabledRanks.Count() > 0 {
		fmt.Fprintf(w, "- Enabled ranks: %s\n", pi.EnabledRanks)
	}
	if pi.DisabledRanks.Count() > 0 {
		fmt.Fprintf(w, "- Disabled ranks: %s\n", pi.DisabledRanks)
	}
	if pi.QueryMask.HasOption(daos.PoolQueryOptionDeadEngines) &&
		pi.DeadRanks != nil && pi.DeadRanks.Count() > 0 {
		fmt.Fprintf(w, "- Dead ranks: %s\n", pi.DeadRanks)
	}
	if pi.Rebuild != nil {
		if pi.Rebuild.Status == 0 {
			fmt.Fprintf(w, "- Rebuild %s, %d objs, %d recs\n",
				pi.Rebuild.State, pi.Rebuild.Objects, pi.Rebuild.Records)
		} else {
			fmt.Fprintf(w, "- Rebuild failed, status=%d\n", pi.Rebuild.Status)
		}
	} else {
		fmt.Fprintln(w, "- No rebuild status available.")
	}

	if pi.QueryMask.HasOption(daos.PoolQueryOptionSpace) && pi.TierStats != nil {
		fmt.Fprintln(w, "Pool space info:")
		fmt.Fprintf(w, "- Target count:%d\n", pi.ActiveTargets)
		printPoolTiers(pi.MemFileBytes, pi.TierStats, w, true)
	}
	return w.Err
}

// PrintPoolQueryTargetInfo generates a human-readable representation of the supplied
// PoolQueryTargetResp struct and writes it to the supplied io.Writer.
func PrintPoolQueryTargetInfo(pqti *daos.PoolQueryTargetInfo, out io.Writer) error {
	if pqti == nil {
		return errors.Errorf("nil %T", pqti)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query-targets` output.
	fmt.Fprintf(w, "Target: type %s, state %s\n", pqti.Type, pqti.State)
	if pqti.Space != nil {
		printPoolTiers(pqti.MemFileBytes, pqti.Space, w, false)
	}

	return w.Err
}

// Display info of NVMe or DATA tier in non-verbose mode. Show single tier if there is only one
// non-empty tier.
func poolListCreateRow(pool *daos.PoolInfo, upgradeNeeded, hasSpaceQuery bool) txtfmt.TableRow {
	var size uint64
	var imbalance uint32
	var used int
	poolUsage := pool.Usage()

	if hasSpaceQuery && len(poolUsage) != 0 {
		// Display stats of the last non-empty tier.
		tierIdx := -1
		for ti := len(poolUsage) - 1; ti >= 0; ti-- {
			if poolUsage[ti].Size > 0 {
				tierIdx = ti
				break
			}
		}
		if tierIdx != -1 {
			tier := poolUsage[tierIdx]
			size = tier.Size
			used = int((float64(size-tier.Free) / float64(size)) * 100)
			if used < 0 {
				used = 0
			}
			imbalance = tier.Imbalance
		}
	}

	row := txtfmt.TableRow{
		"Pool":  pool.Name(),
		"State": pool.State.String(),
	}
	if hasSpaceQuery {
		row = txtfmt.TableRow{
			"Pool":      pool.Name(),
			"Size":      humanize.Bytes(size),
			"State":     pool.State.String(),
			"Used":      fmt.Sprintf("%d%%", used),
			"Imbalance": fmt.Sprintf("%d%%", imbalance),
			"Disabled":  fmt.Sprintf("%d/%d", pool.DisabledTargets, pool.TotalTargets),
		}
	}

	if upgradeNeeded {
		upgradeString := "None"

		if pool.PoolLayoutVer != pool.UpgradeLayoutVer {
			upgradeString = fmt.Sprintf("%d->%d", pool.PoolLayoutVer,
				pool.UpgradeLayoutVer)
		}
		row["UpgradeNeeded?"] = upgradeString
	}

	return row
}

func printPoolList(pools []*daos.PoolInfo, out io.Writer) error {
	upgradeNeeded := false
	hasSpaceQuery := false
	for _, pool := range pools {
		if upgradeNeeded && hasSpaceQuery {
			break
		}
		if pool.PoolLayoutVer != pool.UpgradeLayoutVer {
			upgradeNeeded = true
		}
		if pool.QueryMask.HasOption(daos.PoolQueryOptionSpace) {
			hasSpaceQuery = true
		}
	}

	titles := []string{"Pool", "State"}
	if hasSpaceQuery {
		titles = []string{"Pool", "Size", "State", "Used", "Imbalance", "Disabled"}
	}
	if upgradeNeeded {
		titles = append(titles, "UpgradeNeeded?")
	}
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range pools {
		table = append(table, poolListCreateRow(pool, upgradeNeeded, hasSpaceQuery))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

func getRowUsageTitles(pool *daos.PoolInfo, hasMdOnSsd bool) []string {
	titles := []string{}

	for i, tu := range pool.Usage() {
		tn := tu.TierName
		if hasMdOnSsd {
			if i == 0 {
				tn = "Meta"
			} else {
				tn = "Data"
			}
		}
		titles = append(titles, tn+" Size", tn+" Used", tn+" Imbalance")
	}

	return titles
}

func addVerboseTierUsage(pool *daos.PoolInfo, titles []string, row txtfmt.TableRow) txtfmt.TableRow {
	var ti int

	for _, tu := range pool.Usage() {
		if len(titles) < ti+3 {
			break
		}
		row[titles[ti]] = humanize.Bytes(tu.Size)
		row[titles[ti+1]] = humanize.Bytes(tu.Size - tu.Free)
		row[titles[ti+2]] = fmt.Sprintf("%d%%", tu.Imbalance)
		ti += 3
	}

	return row
}

func poolListCreateRowVerbose(pool *daos.PoolInfo, hasSpace, hasRebuild bool, usageTitles []string) txtfmt.TableRow {
	label := pool.Label
	if label == "" {
		label = "-"
	}

	svcReps := "N/A"
	if len(pool.ServiceReplicas) != 0 {
		svcReps = PrintRanks(pool.ServiceReplicas)
	}

	upgrade := "None"
	if pool.PoolLayoutVer != pool.UpgradeLayoutVer {
		upgrade = fmt.Sprintf("%d->%d", pool.PoolLayoutVer, pool.UpgradeLayoutVer)
	}

	row := txtfmt.TableRow{
		"Label":   label,
		"UUID":    pool.UUID.String(),
		"State":   pool.State.String(),
		"SvcReps": svcReps,
	}
	if hasSpace {
		row["Disabled"] = fmt.Sprintf("%d/%d", pool.DisabledTargets, pool.TotalTargets)
		row["UpgradeNeeded?"] = upgrade
	}
	if hasRebuild {
		row["Rebuild State"] = pool.RebuildState()
	}

	if hasSpace {
		row = addVerboseTierUsage(pool, usageTitles, row)
	}

	return row
}

func printVerbosePoolList(pools []*daos.PoolInfo, out io.Writer) error {
	// Basic pool info should be available without a query.
	titles := []string{"Label", "UUID", "State", "SvcReps"}

	hasSpaceQuery := false
	hasRebuildQuery := false
	hasMdOnSsd := false
	for _, pool := range pools {
		if hasSpaceQuery && hasRebuildQuery {
			break
		}
		if pool.QueryMask.HasOption(daos.PoolQueryOptionSpace) {
			hasSpaceQuery = true
			// All pools will have the same PMem/MD-on-SSD mode.
			hasMdOnSsd = pool.MemFileBytes != 0
		}
		if pool.QueryMask.HasOption(daos.PoolQueryOptionRebuild) {
			hasRebuildQuery = true
		}
	}

	// If any of the pools was queried, then we'll need to show more fields.
	usageTitles := []string{}
	if hasSpaceQuery {
		usageTitles = getRowUsageTitles(pools[0], hasMdOnSsd)
		titles = append(titles, usageTitles...)
		titles = append(titles, "Disabled")
		titles = append(titles, "UpgradeNeeded?")
	}

	if hasRebuildQuery {
		titles = append(titles, "Rebuild State")
	}

	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range pools {
		table = append(table,
			poolListCreateRowVerbose(pool, hasSpaceQuery, hasRebuildQuery, usageTitles))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

// PrintPoolList generates a human-readable representation of the supplied
// slice of daos.PoolInfo structs and writes it to the supplied io.Writer.
func PrintPoolList(pools []*daos.PoolInfo, out io.Writer, verbose bool) error {
	if len(pools) == 0 {
		fmt.Fprintln(out, "No pools in system")
		return nil
	}

	if verbose {
		return printVerbosePoolList(pools, out)
	}

	return printPoolList(pools, out)
}
