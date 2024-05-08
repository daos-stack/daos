//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	pretty "github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

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

// PrintPoolQueryResponse generates a human-readable representation of the supplied
// PoolQueryResp struct and writes it to the supplied io.Writer.
func PrintPoolQueryResponse(pqr *control.PoolQueryResp, out io.Writer, opts ...PrintConfigOption) error {
	return pretty.PrintPoolInfo(&pqr.PoolInfo, out)
}

// PrintPoolQueryTargetResponse generates a human-readable representation of the supplied
// PoolQueryTargetResp struct and writes it to the supplied io.Writer.
func PrintPoolQueryTargetResponse(pqtr *control.PoolQueryTargetResp, out io.Writer, opts ...PrintConfigOption) error {
	if pqtr == nil {
		return errors.Errorf("nil %T", pqtr)
	}

	for _, info := range pqtr.Infos {
		if err := pretty.PrintPoolQueryTargetInfo(info, out); err != nil {
			return err
		}
	}

	return nil
}

// PrintTierRatio generates a human-readable representation of the supplied
// tier ratio.
func PrintTierRatio(ratio float64) string {
	return fmt.Sprintf("%.2f%%", ratio*100)
}

// PrintPoolCreateResponse generates a human-readable representation of the pool create
// response and prints it to the supplied io.Writer.
func PrintPoolCreateResponse(pcr *control.PoolCreateResp, out io.Writer, opts ...PrintConfigOption) error {
	if pcr == nil {
		return errors.New("nil response")
	}

	if len(pcr.TierBytes) == 0 {
		return errors.New("create response had 0 storage tiers")
	}

	var totalSize uint64
	for _, tierBytes := range pcr.TierBytes {
		totalSize += tierBytes
	}

	tierRatios := make([]float64, len(pcr.TierBytes))
	if totalSize != 0 {
		for tierIdx, tierBytes := range pcr.TierBytes {
			tierRatios[tierIdx] = float64(tierBytes) / float64(totalSize)
		}
	}

	if len(pcr.TgtRanks) == 0 {
		return errors.New("create response had 0 target ranks")
	}

	numRanks := uint64(len(pcr.TgtRanks))
	fmtArgs := make([]txtfmt.TableRow, 0, 6)
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"UUID": pcr.UUID})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Service Leader": fmt.Sprintf("%d", pcr.Leader)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Service Ranks": formatRanks(pcr.SvcReps)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Storage Ranks": formatRanks(pcr.TgtRanks)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Total Size": humanize.Bytes(totalSize * numRanks)})

	title := "Pool created with "
	tierName := "SCM"
	for tierIdx, tierRatio := range tierRatios {
		if tierIdx > 0 {
			title += ","
			tierName = "NVMe"
		}

		title += PrintTierRatio(tierRatio)
		fmtName := fmt.Sprintf("Storage tier %d (%s)", tierIdx, tierName)
		fmtArgs = append(fmtArgs, txtfmt.TableRow{fmtName: fmt.Sprintf("%s (%s / rank)", humanize.Bytes(pcr.TierBytes[tierIdx]*numRanks), humanize.Bytes(pcr.TierBytes[tierIdx]))})
	}
	title += " storage tier ratio"

	_, err := fmt.Fprintln(out, txtfmt.FormatEntity(title, fmtArgs))
	return err
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

func printListPoolsResp(out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}
	upgrade := false
	for _, pool := range resp.Pools {
		if resp.PoolQueryError(pool.UUID) != nil {
			continue
		}
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
	for _, pool := range resp.Pools {
		if resp.PoolQueryError(pool.UUID) != nil {
			continue
		}
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

func poolListCreateRowVerbose(pool *daos.PoolInfo) txtfmt.TableRow {
	label := pool.Label
	if label == "" {
		label = "-"
	}

	svcReps := "N/A"
	if len(pool.ServiceReplicas) != 0 {
		rl := ranklist.RanksToUint32(pool.ServiceReplicas)
		svcReps = formatRanks(rl)
	}

	upgrade := "None"
	if pool.PoolLayoutVer != pool.UpgradeLayoutVer {
		upgrade = fmt.Sprintf("%d->%d", pool.PoolLayoutVer, pool.UpgradeLayoutVer)
	}

	row := txtfmt.TableRow{
		"Label":          label,
		"UUID":           pool.UUID.String(),
		"State":          pool.State.String(),
		"SvcReps":        svcReps,
		"Disabled":       fmt.Sprintf("%d/%d", pool.DisabledTargets, pool.TotalTargets),
		"UpgradeNeeded?": upgrade,
		"Rebuild State":  pool.RebuildState(),
	}

	for _, tu := range pool.Usage() {
		row = addVerboseTierUsage(row, tu)
	}

	return row
}

func printListPoolsRespVerbose(noQuery bool, out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	titles := []string{"Label", "UUID", "State", "SvcReps"}
	for _, t := range resp.Pools[0].Usage() {
		titles = append(titles,
			t.TierName+" Size",
			t.TierName+" Used",
			t.TierName+" Imbalance")
	}
	titles = append(titles, "Disabled")
	titles = append(titles, "UpgradeNeeded?")

	if !noQuery {
		titles = append(titles, "Rebuild State")
	}
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range resp.Pools {
		if resp.PoolQueryError(pool.UUID) != nil {
			continue
		}
		table = append(table, poolListCreateRowVerbose(pool))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

// PrintListPoolsResponse generates a human-readable representation of the
// supplied ListPoolsResp struct and writes it to the supplied io.Writer.
// Additional columns for pool UUID and service replicas if verbose is set.
func PrintListPoolsResponse(out, outErr io.Writer, resp *control.ListPoolsResp, verbose bool, noQuery bool) error {
	warn, err := resp.Validate()
	if err != nil {
		return err
	}
	if warn != "" {
		fmt.Fprintln(outErr, warn)
	}

	if verbose {
		return printListPoolsRespVerbose(noQuery, out, resp)
	}

	return printListPoolsResp(out, resp)
}

// PrintPoolProperties displays a two-column table of pool property names and values.
func PrintPoolProperties(poolID string, out io.Writer, properties ...*daos.PoolProperty) {
	fmt.Fprintf(out, "Pool %s properties:\n", poolID)

	nameTitle := "Name"
	valueTitle := "Value"
	table := []txtfmt.TableRow{}
	for _, prop := range properties {
		if prop == nil {
			continue
		}
		row := txtfmt.TableRow{}
		row[nameTitle] = fmt.Sprintf("%s (%s)", prop.Description, prop.Name)
		row[valueTitle] = prop.StringValue()
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(nameTitle, valueTitle)
	tf.InitWriter(out)
	tf.Format(table)
}
