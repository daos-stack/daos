//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const msgNoPools = "No pools in system"

func getTierNameText(tierIdx int) string {
	switch tierIdx {
	case int(daos.StorageMediaTypeScm):
		return fmt.Sprintf("- Storage tier %d (SCM):", tierIdx)
	case int(daos.StorageMediaTypeNvme):
		return fmt.Sprintf("- Storage tier %d (NVMe):", tierIdx)
	default:
		return fmt.Sprintf("- Storage tier %d (unknown):", tierIdx)
	}
}

// PrintPoolInfo generates a human-readable representation of the supplied
// PoolInfo struct and writes it to the supplied io.Writer.
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
		fmt.Fprintf(w, "- Target(VOS) count:%d\n", pi.ActiveTargets)
		for tierIdx, tierStats := range pi.TierStats {
			fmt.Fprintln(w, getTierNameText(tierIdx))
			fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierStats.Total))
			fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
				humanize.Bytes(tierStats.Free), humanize.Bytes(tierStats.Min),
				humanize.Bytes(tierStats.Max), humanize.Bytes(tierStats.Mean))
		}
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
		for tierIdx, tierUsage := range pqti.Space {
			fmt.Fprintln(w, getTierNameText(tierIdx))
			fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierUsage.Total))
			fmt.Fprintf(w, "  Free: %s\n", humanize.Bytes(tierUsage.Free))
		}
	}

	return w.Err
}

func poolListCreateRow(pool *daos.PoolInfo, upgrade bool) txtfmt.TableRow {
	// display size of the largest non-empty tier
	var size uint64
	poolUsage := pool.Usage()
	for ti := len(poolUsage) - 1; ti >= 0; ti-- {
		if poolUsage[ti].Size != 0 {
			size = poolUsage[ti].Size
			break
		}
	}

	// display usage of the most used tier
	var used int
	for ti := 0; ti < len(poolUsage); ti++ {
		t := poolUsage[ti]
		u := float64(t.Size-t.Free) / float64(t.Size)

		if int(u*100) > used {
			used = int(u * 100)
		}
	}

	// display imbalance of the most imbalanced tier
	var imbalance uint32
	for ti := 0; ti < len(poolUsage); ti++ {
		if poolUsage[ti].Imbalance > imbalance {
			imbalance = poolUsage[ti].Imbalance
		}
	}

	row := txtfmt.TableRow{
		"Pool":      pool.Name(),
		"Size":      humanize.Bytes(size),
		"State":     pool.State.String(),
		"Used":      fmt.Sprintf("%d%%", used),
		"Imbalance": fmt.Sprintf("%d%%", imbalance),
		"Disabled":  fmt.Sprintf("%d/%d", pool.DisabledTargets, pool.TotalTargets),
	}

	if upgrade {
		upgradeString := "None"

		if pool.PoolLayoutVer != pool.UpgradeLayoutVer {
			upgradeString = fmt.Sprintf("%d->%d", pool.PoolLayoutVer, pool.UpgradeLayoutVer)
		}
		row["UpgradeNeeded?"] = upgradeString
	}

	return row
}

func printPoolList(pools []*daos.PoolInfo, out io.Writer) error {
	upgrade := false
	for _, pool := range pools {
		if pool.PoolLayoutVer != pool.UpgradeLayoutVer {
			upgrade = true
		}
	}

	titles := []string{"Pool", "Size", "State", "Used", "Imbalance", "Disabled"}
	if upgrade {
		titles = append(titles, "UpgradeNeeded?")
	}
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range pools {
		table = append(table, poolListCreateRow(pool, upgrade))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

func addVerboseTierUsage(row txtfmt.TableRow, usage *daos.PoolTierUsage) txtfmt.TableRow {
	row[usage.TierName+" Size"] = humanize.Bytes(usage.Size)
	row[usage.TierName+" Used"] = humanize.Bytes(usage.Size - usage.Free)
	row[usage.TierName+" Imbalance"] = fmt.Sprintf("%d%%", usage.Imbalance)

	return row
}

func poolListCreateRowVerbose(pool *daos.PoolInfo, hasSpace, hasRebuild bool) txtfmt.TableRow {
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
		for _, tu := range pool.Usage() {
			row = addVerboseTierUsage(row, tu)
		}
	}

	return row
}

func printVerbosePoolList(pools []*daos.PoolInfo, out io.Writer) error {
	// Basic pool info should be available without a query.
	titles := []string{"Label", "UUID", "State", "SvcReps"}

	hasSpaceQuery := false
	hasRebuildQuery := false
	for _, pool := range pools {
		if hasSpaceQuery && hasRebuildQuery {
			break
		}

		if pool.QueryMask.HasOption(daos.PoolQueryOptionSpace) {
			hasSpaceQuery = true
		}
		if pool.QueryMask.HasOption(daos.PoolQueryOptionRebuild) {
			hasRebuildQuery = true
		}
	}

	// If any of the pools was queried, then we'll need to show more fields.
	if hasSpaceQuery {
		for _, t := range pools[0].Usage() {
			titles = append(titles,
				t.TierName+" Size",
				t.TierName+" Used",
				t.TierName+" Imbalance")
		}
		titles = append(titles, "Disabled")
		titles = append(titles, "UpgradeNeeded?")
	}

	if hasRebuildQuery {
		titles = append(titles, "Rebuild State")
	}

	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range pools {
		table = append(table, poolListCreateRowVerbose(pool, hasSpaceQuery, hasRebuildQuery))
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

// PrintAttributes generates a human-readable representation of the supplied
// list of daos.Attributes and writes it to the supplied io.Writer.
func PrintAttributes(out io.Writer, header string, attrs ...*daos.Attribute) {
	fmt.Fprintf(out, "%s\n", header)

	if len(attrs) == 0 {
		fmt.Fprintln(out, "  No attributes found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	titles := []string{nameTitle}

	table := []txtfmt.TableRow{}
	for _, attr := range attrs {
		row := txtfmt.TableRow{}
		row[nameTitle] = attr.Name
		if len(attr.Value) != 0 {
			row[valueTitle] = string(attr.Value)
			if len(titles) == 1 {
				titles = append(titles, valueTitle)
			}
		}
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(out)
	tf.Format(table)
}
