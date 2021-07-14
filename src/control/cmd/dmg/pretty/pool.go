//
// (C) Copyright 2020-2021 Intel Corporation.
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
	fmt.Fprintf(w, "- Target(VOS) count:%d\n", pqr.ActiveTargets)
	if pqr.Scm != nil {
		fmt.Fprintln(w, "- SCM:")
		fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(pqr.Scm.Total))
		fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
			humanize.Bytes(pqr.Scm.Free), humanize.Bytes(pqr.Scm.Min),
			humanize.Bytes(pqr.Scm.Max), humanize.Bytes(pqr.Scm.Mean))
	}
	if pqr.Nvme != nil {
		fmt.Fprintln(w, "- NVMe:")
		fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(pqr.Nvme.Total))
		fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
			humanize.Bytes(pqr.Nvme.Free), humanize.Bytes(pqr.Nvme.Min),
			humanize.Bytes(pqr.Nvme.Max), humanize.Bytes(pqr.Nvme.Mean))
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

	ratio := 1.0
	if pcr.NvmeBytes > 0 {
		ratio = float64(pcr.ScmBytes) / float64(pcr.NvmeBytes)
	}

	if len(pcr.TgtRanks) == 0 {
		return errors.New("create response had 0 target ranks")
	}

	numRanks := uint64(len(pcr.TgtRanks))
	title := fmt.Sprintf("Pool created with %0.2f%%%% SCM/NVMe ratio", ratio*100)
	_, err := fmt.Fprintln(out, txtfmt.FormatEntity(title, []txtfmt.TableRow{
		{"UUID": pcr.UUID},
		{"Service Ranks": formatRanks(pcr.SvcReps)},
		{"Storage Ranks": formatRanks(pcr.TgtRanks)},
		{"Total Size": humanize.Bytes((pcr.ScmBytes + pcr.NvmeBytes) * numRanks)},
		{"SCM": fmt.Sprintf("%s (%s / rank)", humanize.Bytes(pcr.ScmBytes*numRanks), humanize.Bytes(pcr.ScmBytes))},
		{"NVMe": fmt.Sprintf("%s (%s / rank)", humanize.Bytes(pcr.NvmeBytes*numRanks), humanize.Bytes(pcr.NvmeBytes))},
	}))

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
	imbalance := pool.Usage[0].Imbalance
	for ti := 0; ti < len(pool.Usage); ti++ {
		if pool.Usage[ti].Imbalance > imbalance {
			imbalance = pool.Usage[ti].Imbalance
		}
	}

	row := txtfmt.TableRow{
		"Pool":      pool.GetName(),
		"Size":      fmt.Sprintf("%s", humanize.Bytes(size)),
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

	formatter := txtfmt.NewTableFormatter("Pool", "Size", "Used", "Imbalance", "Disabled")

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

	titles := []string{"Label", "UUID", "SvcReps"}
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
