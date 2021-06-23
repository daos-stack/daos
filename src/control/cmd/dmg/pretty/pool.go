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

func poolListGetTitles() []string {
	return []string{"Pool", "Size", "Used", "Imbalance", "Disabled"}
}

func poolListCreateRow(pool *control.Pool) txtfmt.TableRow {
	name := pool.Label
	if name == "" {
		name = pool.UUID
	}

	nvmeTotal := pool.Info.Nvme.Total
	scmTotal := pool.Info.Scm.Total

	size := nvmeTotal
	if size == 0 {
		size = scmTotal
	}

	scmUsage := (scmTotal - pool.Info.Scm.Free) / scmTotal
	nvmeUsage := (nvmeTotal - pool.Info.Nvme.Free) / nvmeTotal
	usage := scmUsage
	if nvmeUsage > usage {
		usage = nvmeUsage
	}

	scmSpread := pool.Info.Scm.Max - pool.Info.Scm.Min
	scmImbalance := scmSpread / (scmTotal / pool.Info.ActiveTargets)
	nvmeSpread := pool.Info.Nvme.Max - pool.Info.Nvme.Min
	nvmeImbalance := nvmeSpread / (nvmeTotal / pool.Info.ActiveTargets)
	imbalance := scmImbalance
	if nvmeImbalance > imbalance {
		imbalance = nvmeImbalance
	}

	row := txtfmt.TableRow{
		"Pool":      name,
		"Size":      humanize.Bytes(size),
		"Used":      fmt.Sprintf("%d%%", int(usage*100)),
		"Imbalance": fmt.Sprintf("%d%%", int(imbalance*100)),
		"Disabled":  fmt.Sprintf("%d/%d", pool.Info.DisabledTargets, pool.Info.TotalTargets),
	}

	return row
}

func printListPoolsResp(out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	formatter := txtfmt.NewTableFormatter(poolListGetTitles()...)

	var table []txtfmt.TableRow
	for _, pool := range resp.Pools {
		table = append(table, poolListCreateRow(pool))
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

func printListPoolsRespVerbose(out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	titles := poolListGetTitles()
	uuidTitle := "UUID"
	svcRepsTitle := "SvcReps"
	verboseTitles := []string{titles[0], uuidTitle, svcRepsTitle}
	verboseTitles = append(verboseTitles, titles[1:]...)

	formatter := txtfmt.NewTableFormatter(verboseTitles...)

	var table []txtfmt.TableRow
	for _, pool := range resp.Pools {
		row := poolListCreateRow(pool)
		row[uuidTitle] = pool.UUID
		row[svcRepsTitle] = "N/A"
		if len(pool.SvcReplicas) != 0 {
			row[svcRepsTitle] = formatRanks(pool.SvcReplicas)
		}
		table = append(table, row)
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

// PrintListPoolsResponse generates a human-readable representation of the
// supplied ListPoolsResp struct and writes it to the supplied io.Writer.
// Additional columns for pool UUID and service replicas if verbose is set.
func PrintListPoolsResponse(o io.Writer, r *control.ListPoolsResp, v bool) error {
	if v {
		return printListPoolsRespVerbose(o, r)
	}

	return printListPoolsResp(o, r)
}
