//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

// PrintPoolQueryResponse generates a human-readable representation of the supplied
// PoolQueryResp struct and writes it to the supplied io.Writer.
func PrintPoolQueryResponse(pqr *control.PoolQueryResp, out io.Writer, opts ...PrintConfigOption) error {
	if pqr == nil {
		return errors.Errorf("nil %T", pqr)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query` output.
	fmt.Fprintf(w, "Pool %s, ntarget=%d, disabled=%d, leader=%d, version=%d\n",
		pqr.UUID, pqr.TotalTargets, pqr.DisabledTargets, pqr.Leader, pqr.Version)
	fmt.Fprintln(w, "Pool space info:")
	if pqr.EnabledRanks != nil {
		fmt.Fprintf(w, "- Enabled targets: %s\n", pqr.EnabledRanks)
	}
	if pqr.DisabledRanks != nil {
		fmt.Fprintf(w, "- Disabled targets: %s\n", pqr.DisabledRanks)
	}
	fmt.Fprintf(w, "- Target(VOS) count:%d\n", pqr.ActiveTargets)
	if pqr.TierStats != nil {
		for tierIdx, tierStats := range pqr.TierStats {
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
	if pqr.Rebuild != nil {
		if pqr.Rebuild.Status == 0 {
			fmt.Fprintf(w, "Rebuild %s, %d objs, %d recs\n",
				pqr.Rebuild.State, pqr.Rebuild.Objects, pqr.Rebuild.Records)
		} else {
			fmt.Fprintf(w, "Rebuild failed, rc=%d, status=%d\n", pqr.Status, pqr.Rebuild.Status)
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

func poolListCreateRow(pool *control.Pool) txtfmt.TableRow {
	// display size of the largest non-empty tier
	var size uint64
	for ti := len(pool.Usage) - 1; ti >= 0; ti-- {
		if pool.Usage[ti].Size != 0 {
			size = pool.Usage[ti].Size
			break
		}
	}

	// display usage of the most used tier
	var used int
	for ti := 0; ti < len(pool.Usage); ti++ {
		t := pool.Usage[ti]
		u := float64(t.Size-t.Free) / float64(t.Size)

		if int(u*100) > used {
			used = int(u * 100)
		}
	}

	// display imbalance of the most imbalanced tier
	var imbalance uint32
	for ti := 0; ti < len(pool.Usage); ti++ {
		if pool.Usage[ti].Imbalance > imbalance {
			imbalance = pool.Usage[ti].Imbalance
		}
	}

	row := txtfmt.TableRow{
		"Pool":      pool.GetName(),
		"Size":      fmt.Sprintf("%s", humanize.Bytes(size)),
		"State":     pool.State,
		"Used":      fmt.Sprintf("%d%%", used),
		"Imbalance": fmt.Sprintf("%d%%", imbalance),
		"Disabled":  fmt.Sprintf("%d/%d", pool.TargetsDisabled, pool.TargetsTotal),
	}

	return row
}

func printListPoolsResp(out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	formatter := txtfmt.NewTableFormatter("Pool", "Size", "State", "Used", "Imbalance", "Disabled")

	var table []txtfmt.TableRow
	for _, pool := range resp.Pools {
		if pool.HasErrors() {
			continue
		}
		table = append(table, poolListCreateRow(pool))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

func addVerboseTierUsage(row txtfmt.TableRow, usage *control.PoolTierUsage) txtfmt.TableRow {
	row[usage.TierName+" Size"] = humanize.Bytes(usage.Size)
	row[usage.TierName+" Used"] = humanize.Bytes(usage.Size - usage.Free)
	row[usage.TierName+" Imbalance"] = fmt.Sprintf("%d%%", usage.Imbalance)

	return row
}

func poolListCreateRowVerbose(pool *control.Pool) txtfmt.TableRow {
	label := pool.Label
	if label == "" {
		label = "-"
	}

	svcReps := "N/A"
	if len(pool.ServiceReplicas) != 0 {
		rl := system.RanksToUint32(pool.ServiceReplicas)
		svcReps = formatRanks(rl)
	}

	row := txtfmt.TableRow{
		"Label":    label,
		"UUID":     pool.UUID,
		"State":    pool.State,
		"SvcReps":  svcReps,
		"Disabled": fmt.Sprintf("%d/%d", pool.TargetsDisabled, pool.TargetsTotal),
	}

	for _, tu := range pool.Usage {
		row = addVerboseTierUsage(row, tu)
	}

	return row
}

func printListPoolsRespVerbose(out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	titles := []string{"Label", "UUID", "State", "SvcReps"}
	for _, t := range resp.Pools[0].Usage {
		titles = append(titles,
			t.TierName+" Size",
			t.TierName+" Used",
			t.TierName+" Imbalance")
	}
	titles = append(titles, "Disabled")
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range resp.Pools {
		if pool.HasErrors() {
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
func PrintListPoolsResponse(out, outErr io.Writer, resp *control.ListPoolsResp, verbose bool) error {
	warn, err := resp.Validate()
	if err != nil {
		return err
	}
	if warn != "" {
		fmt.Fprintln(outErr, warn)
	}

	if verbose {
		return printListPoolsRespVerbose(out, resp)
	}

	return printListPoolsResp(out, resp)
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
