//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"sort"

	"github.com/dustin/go-humanize/english"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

// PrintCheckerPolicies displays a two-column table of checker policy classes and actions.
func PrintCheckerPolicies(out io.Writer, flags control.SystemCheckFlags, policies ...*control.SystemCheckPolicy) {
	fmt.Fprintf(out, "Checker flags: %s\n\n", flags)

	nameTitle := "Inconsistency Class"
	valueTitle := "Repair Action"
	table := []txtfmt.TableRow{}
	for _, policy := range policies {
		if policy == nil {
			continue
		}
		row := txtfmt.TableRow{}
		row[nameTitle] = policy.FindingClass.String()
		row[valueTitle] = policy.RepairAction.String()
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(nameTitle, valueTitle)
	tf.InitWriter(out)
	tf.Format(table)
}

func countResultPools(resp *control.SystemCheckQueryResp) int {
	if resp == nil {
		return 0
	}

	poolMap := make(map[string]struct{})
	for _, pool := range resp.Pools {
		// Don't include pools that were not checked.
		if pool.Unchecked() {
			continue
		}
		poolMap[pool.UUID] = struct{}{}
	}
	for _, report := range resp.Reports {
		if report.IsRemovedPool() && report.PoolUuid != "" {
			poolMap[report.PoolUuid] = struct{}{}
		}
	}

	return len(poolMap)
}

func PrintCheckQueryResp(out io.Writer, resp *control.SystemCheckQueryResp, verbose bool) {
	fmt.Fprintln(out, "DAOS System Checker Info")
	if resp == nil {
		fmt.Fprintln(out, "  No results found.")
		return
	}

	statusMsg := fmt.Sprintf("Current status: %s", resp.Status)
	if resp.Status > control.SystemCheckStatusInit && resp.Status < control.SystemCheckStatusCompleted {
		statusMsg += fmt.Sprintf(" (started at: %s)", common.FormatTime(resp.StartTime))
	}
	fmt.Fprintf(out, "  %s\n", statusMsg)
	fmt.Fprintf(out, "  Current phase: %s (%s)\n", resp.ScanPhase, resp.ScanPhase.Description())

	// Toggle this output based on the status. If the checker is still running, we
	// should show the number of pools being checked. If the checker has completed,
	// we should show the number of unique pools found in the reports.
	action := "Checking"
	poolCount := countResultPools(resp)
	if resp.Status == control.SystemCheckStatusCompleted {
		action = "Checked"
	}
	if poolCount > 0 {
		fmt.Fprintf(out, "  %s %s\n", action, english.Plural(poolCount, "pool", ""))
	}

	if len(resp.Pools) > 0 && verbose {
		pools := make([]*control.SystemCheckPoolInfo, 0, len(resp.Pools))
		for _, pool := range resp.Pools {
			pools = append(pools, pool)
		}
		sort.Slice(pools, func(i, j int) bool {
			return pools[i].UUID < pools[j].UUID
		})
		fmt.Fprintln(out, "\nPer-Pool Checker Info:")
		for _, pool := range pools {
			fmt.Fprintf(out, "  %+v\n", pool)
		}
	}

	fmt.Fprintln(out)
	if len(resp.Reports) == 0 {
		fmt.Fprintln(out, "No reports to display.")
		return
	}

	fmt.Fprintln(out, "Inconsistency Reports:")
	if verbose {
		printInconsistencyReportsVerbose(out, resp)
	} else {
		printInconsistencyReportsTable(out, resp)
	}
}

func printInconsistencyReportsTable(out io.Writer, resp *control.SystemCheckQueryResp) {
	const (
		idLabel     = "ID"
		classLabel  = "Class"
		poolLabel   = "Pool"
		contLabel   = "Cont"
		resLabel    = "Resolution"
		repairLabel = "Suggested Repair"
	)
	table := []txtfmt.TableRow{}
	hasCont := false
	hasRepairChoices := false
	for _, report := range resp.Reports {
		tr := txtfmt.TableRow{}

		tr[idLabel] = fmt.Sprintf("0x%x", report.Seq)
		tr[classLabel] = control.SystemCheckFindingClass(report.Class).String()
		tr[poolLabel] = checkerPoolID(report, false)

		if report.ContUuid != "" {
			hasCont = true
			tr[contLabel] = checkerContID(report, false)
		}

		if report.IsInteractive() {
			tr[resLabel] = "Action required"
			choices := report.RepairChoices()
			if len(choices) > 0 {
				// First repair choice is the recommended option
				tr[repairLabel] = fmt.Sprintf("0: %s", choices[0].Info)
				hasRepairChoices = true
			}
		} else if res := report.Resolution(); res != "" {
			tr[resLabel] = res
		}

		table = append(table, tr)
	}

	cols := []string{idLabel, classLabel, poolLabel}
	if hasCont {
		cols = append(cols, contLabel)
	}
	cols = append(cols, resLabel)
	if hasRepairChoices {
		cols = append(cols, repairLabel)
	}
	tw := txtfmt.NewTableFormatter(cols...)
	fmt.Fprintf(out, "%s\n", tw.Format(table))
}

func checkerPoolID(report *control.SystemCheckReport, verbose bool) string {
	poolID := report.PoolUuid
	if report.PoolLabel != "" {
		poolID = report.PoolLabel
		if verbose {
			poolID += fmt.Sprintf(" (%s)", report.PoolUuid)
		}
	}
	return poolID
}

func checkerContID(report *control.SystemCheckReport, verbose bool) string {
	contID := report.ContUuid
	if report.ContLabel != "" {
		contID = report.ContLabel
		if verbose {
			contID += fmt.Sprintf(" (%s)", report.ContUuid)
		}
	}
	return contID
}

func printInconsistencyReportsVerbose(out io.Writer, resp *control.SystemCheckQueryResp) {
	iw := txtfmt.NewIndentWriter(out)
	for _, report := range resp.Reports {
		cls := control.SystemCheckFindingClass(report.Class)
		fmt.Fprintf(iw, "ID:         0x%x\n", report.Seq)
		fmt.Fprintf(iw, "Class:      %s\n", cls)
		fmt.Fprintf(iw, "Message:    %s\n", report.Msg)
		fmt.Fprintf(iw, "Pool:       %s\n", checkerPoolID(report, true))
		if report.ContUuid != "" {
			fmt.Fprintf(iw, "Container:  %s\n", checkerContID(report, true))
		}
		if report.IsInteractive() {
			fmt.Fprintf(iw, "Potential resolution actions:\n")
			iw2 := txtfmt.NewIndentWriter(iw)
			for i, choice := range report.RepairChoices() {
				fmt.Fprintf(iw2, "%d: %s\n", i, choice.Info)
			}
			fmt.Fprintln(iw)
		} else if res := report.Resolution(); res != "" {
			fmt.Fprintf(iw, "Resolution: %s\n\n", res)
		} else {
			fmt.Fprintf(iw, "No resolutions available\n\n")
			continue
		}
	}
}
