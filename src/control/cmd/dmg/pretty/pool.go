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
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const msgNoPools = "No pools in system"

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

func printTierBytesRow(fmtName string, tierBytes uint64, numRanks int) txtfmt.TableRow {
	return txtfmt.TableRow{
		fmtName: fmt.Sprintf("%s (%s / rank)",
			humanize.Bytes(tierBytes*uint64(numRanks)),
			humanize.Bytes(tierBytes)),
	}
}

func getPoolCreateRespRows(mdOnSSD bool, tierBytes []uint64, tierRatios []float64, numRanks int) (title string, rows []txtfmt.TableRow) {
	title = "Pool created with "
	tierName := "SCM"
	if mdOnSSD {
		tierName = "Metadata"
	}

	for tierIdx, tierRatio := range tierRatios {
		if tierIdx > 0 {
			title += ","
			tierName = "NVMe"
			if mdOnSSD {
				tierName = "Data"
			}
		}

		title += PrintTierRatio(tierRatio)
		fmtName := fmt.Sprintf("Storage tier %d (%s)", tierIdx, tierName)
		if mdOnSSD {
			fmtName = tierName + " Storage"
		}
		rows = append(rows, printTierBytesRow(fmtName, tierBytes[tierIdx], numRanks))
	}
	title += " storage tier ratio"

	return title, rows
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

	numRanks := len(pcr.TgtRanks)
	fmtArgs := make([]txtfmt.TableRow, 0, 6)
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"UUID": pcr.UUID})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Service Leader": fmt.Sprintf("%d", pcr.Leader)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Service Ranks": pretty.PrintRanks(pcr.SvcReps)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{"Storage Ranks": pretty.PrintRanks(pcr.TgtRanks)})
	fmtArgs = append(fmtArgs, txtfmt.TableRow{
		"Total Size": humanize.Bytes(totalSize * uint64(numRanks)),
	})

	mdOnSsdEnabled := pcr.MemFileBytes > 0

	title, tierRows := getPoolCreateRespRows(mdOnSsdEnabled, pcr.TierBytes, tierRatios,
		numRanks)

	// Print memory-file to meta-blob ratio for MD-on-SSD.
	if mdOnSsdEnabled {
		tierRows = append(tierRows, printTierBytesRow("Memory File Size",
			pcr.MemFileBytes, numRanks))
	}

	fmtArgs = append(fmtArgs, tierRows...)

	_, err := fmt.Fprintln(out, txtfmt.FormatEntity(title, fmtArgs))
	return err
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

	// Filter out any pools that had query errors.
	queriedPools := make([]*daos.PoolInfo, 0, len(resp.Pools))
	for _, pool := range resp.Pools {
		if _, found := resp.QueryErrors[pool.UUID]; found {
			continue
		}
		queriedPools = append(queriedPools, pool)
	}

	return pretty.PrintPoolList(queriedPools, out, verbose)
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
