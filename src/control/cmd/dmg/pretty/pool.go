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
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

func validatePoolQueryResp(pqr *control.PoolQueryResp) error {
	var zeroUUID uuid.UUID
	numTiers := -1

	for i, p := range pqr.Pools {
		if p.UUID == zeroUUID {
			return errors.Errorf("pool with index %d has invalid zero value uuid", i)
		}
		if p.Status != 0 {
			continue // Ignore entries for failed queries.
		}
		if numTiers != -1 && len(p.TierStats) != numTiers {
			return errors.Errorf("pool %s has %d storage tiers, previous had %d",
				p.GetName(), len(p.TierStats), numTiers)
		}
		numTiers = len(p.TierStats)
	}

	return nil
}

// PrintPoolInfo generates a human-readable representation of a PoolInfo struct and writes it to
// the supplied io.Writer.
func PrintPoolInfo(pi *control.PoolInfo, out io.Writer) error {
	if pi == nil {
		return errors.Errorf("nil %T", pi)
	}
	w := txtfmt.NewErrWriter(out)

	var zeroUUID uuid.UUID
	uuid := pi.UUID.String()
	if uuid == zeroUUID.String() {
		uuid = ""
	}

	// Maintain output compatibility with the `daos pool query` output.
	fmt.Fprintf(w, "Pool %s, ntarget=%d, disabled=%d, leader=%d, version=%d\n",
		uuid, pi.TotalTargets, pi.DisabledTargets, pi.Leader, pi.Version)
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

// PrintPoolQueryResponse generates a human-readable representation of the pool query response
// and prints it to the supplied io.Writer.
func PrintPoolQueryResponse(pqr *control.PoolQueryResp, out io.Writer) error {
	if pqr == nil {
		return errors.New("nil response")
	}

	if len(pqr.Pools) == 0 {
		return PrintPoolInfo(&control.PoolInfo{}, out)
	}

	return PrintPoolInfo(pqr.Pools[0], out)
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

type createRowFn func(*control.PoolInfo) txtfmt.TableRow

func printPoolListCommon(out, outErr io.Writer, pqr *control.PoolQueryResp, titles []string, rowCreator createRowFn) error {
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, pool := range pqr.Pools {
		if pool.Status == 0 {
			table = append(table, rowCreator(pool))
			continue
		}
		fmt.Fprintf(outErr, "query on pool %q failed: %s\n", pool,
			drpc.DaosStatus(pool.Status))
	}

	if len(table) != len(pqr.Pools) {
		fmt.Fprintln(out, "") // Add separator if errors printed.
	}

	if len(table) > 0 {
		fmt.Fprintln(out, formatter.Format(table))
	} else {
		fmt.Fprintln(out, "no pool data to display")
	}

	return nil
}

func printPoolList(out, outErr io.Writer, pqr *control.PoolQueryResp) error {
	titles := []string{"Pool", "Size", "Used", "Imbalance", "Disabled"}
	return printPoolListCommon(out, outErr, pqr, titles, createPoolListRow)
}

func getStorageStatsTitles(tu *control.StorageUsageStats, idx int) (string, string, string) {
	// NOTE: When multiple tiers of the same type are common, label tiers with an integer
	//       but for the moment just use the media type as the column title prefix.
	//name := fmt.Sprintf("%s-%d", strings.ToUpper(tu.MediaType), idx)
	name := strings.ToUpper(tu.MediaType)
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

func printPoolListVerbose(out, outErr io.Writer, pqr *control.PoolQueryResp) error {
	titles := []string{"Label", "UUID", "SvcReps"}

	if pqr.Pools[0].TierStats != nil {
		for idx, ts := range pqr.Pools[0].TierStats {
			size, used, imbalance := getStorageStatsTitles(ts, idx)
			titles = append(titles, size, used, imbalance)
		}
	}
	titles = append(titles, "Disabled")

	return printPoolListCommon(out, outErr, pqr, titles, createPoolListRowVerbose)
}

// PrintPoolList generates a human-readable representation of the supplied PoolQueryResp struct and
// writes it to the supplied io.Writer. Additional columns for pool UUID and service replicas are
// displayed if verbose is set.
func PrintPoolList(out, outErr io.Writer, pqr *control.PoolQueryResp, verbose bool) error {
	if len(pqr.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	if err := validatePoolQueryResp(pqr); err != nil {
		return err
	}

	if verbose {
		return printPoolListVerbose(out, outErr, pqr)
	}

	return printPoolList(out, outErr, pqr)
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
