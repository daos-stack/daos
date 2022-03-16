//
// (C) Copyright 2020-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

// PrintPoolInfo generates a human-readable representation of the supplied
// PoolQueryResp struct and writes it to the supplied io.Writer.
func PrintPoolInfo(pi *control.PoolInfo, out io.Writer, opts ...PrintConfigOption) error {
	if pi == nil {
		return errors.Errorf("nil %T", pi)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query` output.
	fmt.Fprintf(w, "Pool %s, ntarget=%d, disabled=%d, leader=%d, version=%d\n",
		pi.UUID, pi.TotalTargets, pi.DisabledTargets, pi.Leader, pi.Version)
	fmt.Fprintln(w, "Pool space info:")
	fmt.Fprintf(w, "- Target(VOS) count:%d\n", pi.ActiveTargets)
	if pi.TierStats != nil {
		for tierIdx, tierStats := range pi.TierStats {
			var tierName string
			if tierIdx == 0 {
				tierName = "- Storage tier 0 (SCM):"
			} else {
				tierName = fmt.Sprintf("- Storage tier %d (NVMe):", tierIdx)
			}
			fmt.Fprintln(w, tierName)
			fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierStats.Total))
			fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
				humanize.Bytes(tierStats.Free), humanize.Bytes(tierStats.Min),
				humanize.Bytes(tierStats.Max), humanize.Bytes(tierStats.Mean))
		}
	}
	if pi.Rebuild != nil {
		if pi.Rebuild.Status == 0 {
			fmt.Fprintf(w, "Rebuild %s, %d objs, %d recs\n",
				pi.Rebuild.State, pi.Rebuild.Objects, pi.Rebuild.Records)
		} else {
			fmt.Fprintf(w, "Rebuild failed, rc=%d, status=%d\n", pi.Status, pi.Rebuild.Status)
		}
	}

	return w.Err
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

	tierRatio := make([]float64, len(pcr.TierBytes))
	if totalSize != 0 {
		for tierIdx, tierBytes := range pcr.TierBytes {
			tierRatio[tierIdx] = float64(tierBytes) / float64(totalSize)
		}
	}

	if len(pcr.TgtRanks) == 0 {
		return errors.New("create response had 0 target ranks")
	}

	numRanks := uint64(len(pcr.TgtRanks))
	fmtArgs := make([]txtfmt.TableRow, 0, 6)
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"UUID": pcr.UUID})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Service Ranks": formatRanks(pcr.SvcReps)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Storage Ranks": formatRanks(pcr.TgtRanks)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Total Size": humanize.Bytes(totalSize * numRanks)})

	title := "Pool created with "
	tierName := "SCM"
	for tierIdx, tierRatio := range tierRatio {
		if tierIdx > 0 {
			title += ","
			tierName = "NVMe"
		}

		title += fmt.Sprintf("%0.2f%%", tierRatio*100)
		fmtName := fmt.Sprintf("Storage tier %d (%s)", tierIdx, tierName)
		fmtArgs = append(fmtArgs, txtfmt.TableRow{fmtName: fmt.Sprintf("%s (%s / rank)", humanize.Bytes(pcr.TierBytes[tierIdx]*numRanks), humanize.Bytes(pcr.TierBytes[tierIdx]))})
	}
	title += " storage tier ratio"

	_, err := fmt.Fprintln(out, txtfmt.FormatEntity(title, fmtArgs))
	return err
}

func createPoolListRow(pool *control.PoolInfo) txtfmt.TableRow {
	// display size of the largest non-empty tier
	var size uint64
	for ti := len(pool.TierStats) - 1; ti >= 0; ti-- {
		if pool.TierStats[ti].Total != 0 {
			size = pool.TierStats[ti].Total
			break
		}
	}

	// display usage of the most used tier
	var used int
	for ti := 0; ti < len(pool.TierStats); ti++ {
		t := pool.TierStats[ti]
		u := float64(t.Total-t.Free) / float64(t.Total)

		if int(u*100) > used {
			used = int(u * 100)
		}
	}

	// display imbalance of the most imbalanced tier
	var imbalance uint32
	for ti := 0; ti < len(pool.TierStats); ti++ {
		if pool.TierStats[ti].Imbalance > imbalance {
			imbalance = pool.TierStats[ti].Imbalance
		}
	}

	row := txtfmt.TableRow{
		"Pool":      pool.GetName(),
		"Size":      humanize.Bytes(size),
		"Used":      fmt.Sprintf("%d%%", used),
		"Imbalance": fmt.Sprintf("%d%%", imbalance),
		"Disabled":  fmt.Sprintf("%d/%d", pool.DisabledTargets, pool.TotalTargets),
	}

	return row
}

func printPoolList(out io.Writer, pqr *control.PoolQueryResp) error {
	if len(pqr.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	formatter := txtfmt.NewTableFormatter("Pool", "Size", "Used", "Imbalance", "Disabled")

	var table []txtfmt.TableRow
	for _, pool := range pqr.Pools {
		table = append(table, createPoolListRow(pool))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

func getStorageStatsTitles(tu *control.StorageUsageStats, idx int) (string, string, string) {
	name := fmt.Sprintf("%s-%d", strings.ToUpper(tu.MediaType), idx)
	return name + " Size", name + " Used", name + " Imbalance"
}

func createPoolListRowVerbose(pi *control.PoolInfo) txtfmt.TableRow {
	label := pi.Label
	if label == "" {
		label = "-"
	}

	svcReps := "N/A"
	if len(pi.ServiceReplicas) != 0 {
		rl := system.RanksToUint32(pi.ServiceReplicas)
		svcReps = formatRanks(rl)
	}

	row := txtfmt.TableRow{
		"Label":    label,
		"UUID":     pi.UUID.String(),
		"SvcReps":  svcReps,
		"Disabled": fmt.Sprintf("%d/%d", pi.DisabledTargets, pi.TotalTargets),
	}

	for idx, ts := range pi.TierStats {
		size, used, imbalance := getStorageStatsTitles(ts, idx)
		row[size] = humanize.Bytes(ts.Total)
		row[used] = humanize.Bytes(ts.Total - ts.Free)
		row[imbalance] = fmt.Sprintf("%d%%", ts.Imbalance)
	}

	return row
}

func printPoolListVerbose(out io.Writer, pqr *control.PoolQueryResp) error {
	if len(pqr.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	titles := []string{"Label", "UUID", "SvcReps"}
	for idx, ts := range pqr.Pools[0].TierStats {
		size, used, imbalance := getStorageStatsTitles(ts, idx)
		titles = append(titles, size, used, imbalance)
	}
	titles = append(titles, "Disabled")
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pi := range pqr.Pools {
		table = append(table, createPoolListRowVerbose(pi))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

// PrintPoolList generates a human-readable representation of the supplied PoolQueryResp struct and
// writes it to the supplied io.Writer. Additional columns for pool UUID and service replicas are
// displayed if verbose is set.
func PrintPoolList(out, outErr io.Writer, pqr *control.PoolQueryResp, verbose bool) error {
	if verbose {
		return printPoolListVerbose(out, pqr)
	}

	return printPoolList(out, pqr)
}

// PrintPoolProperties displays a two-column table of pool property names and values.
func PrintPoolProperties(poolID string, out io.Writer, properties ...*control.PoolProperty) {
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
