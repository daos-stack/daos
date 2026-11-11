//
// (C) Copyright 2020-2023 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const (
	idLabel     = "ID"
	classLabel  = "Class"
	poolLabel   = "Pool"
	contLabel   = "Cont"
	resLabel    = "Resolution"
	repairLabel = "Repair Options"
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

func countResultPools(resp *control.SystemCheckQueryResp) (int, int) {
	if resp == nil {
		return 0, 0
	}

	checkedMap := make(map[string]struct{})
	uncheckedMap := make(map[string]struct{})
	for _, pool := range resp.Pools {
		if pool.Unchecked() {
			uncheckedMap[pool.UUID] = struct{}{}
		} else {
			checkedMap[pool.UUID] = struct{}{}
		}
	}
	for _, report := range resp.Reports {
		if report.IsRemovedPool() && report.PoolUuid != "" {
			checkedMap[report.PoolUuid] = struct{}{}
		}
	}

	return len(checkedMap), len(uncheckedMap)
}

func printSystemCheckTargetInfo(out io.Writer, pool *control.SystemCheckPoolInfo) {
	byRank := make(map[ranklist.Rank]map[string][]*control.SystemCheckTargetInfo)
	allStatus := make(map[string]struct{})

	for _, r := range pool.Ranks {
		if _, ok := byRank[r.Rank]; !ok {
			byRank[r.Rank] = make(map[string][]*control.SystemCheckTargetInfo)
		}

		for _, t := range r.Targets {
			byRank[r.Rank][t.Status] = append(byRank[r.Rank][t.Status], t)
			allStatus[t.Status] = struct{}{}
		}
	}

	sort.Slice(pool.Ranks, func(i, j int) bool {
		return pool.Ranks[i].Rank < pool.Ranks[j].Rank
	})

	sortedStatus := make([]string, 0, len(allStatus))
	for s := range allStatus {
		sortedStatus = append(sortedStatus, s)
	}
	sort.Slice(sortedStatus, func(i, j int) bool {
		return sortedStatus[i] < sortedStatus[j]
	})

	condensedTargetString := func(targets []*control.SystemCheckTargetInfo) string {
		targetSet := ranklist.NewRankSet()
		for _, target := range targets {
			targetSet.Add(ranklist.Rank(target.Target))
		}
		if len(targets) == 1 {
			return "[" + targetSet.String() + "]"
		}
		return targetSet.RangedString()
	}

	iw := txtfmt.NewIndentWriter(out)
	for _, r := range pool.Ranks {
		unknownIfEmpty := func(in string) string {
			if in == "" {
				return "UNKNOWN"
			}
			return in
		}
		fmt.Fprintf(iw, "Rank %d, status: %s, phase: %s\n", r.Rank, unknownIfEmpty(r.Status), unknownIfEmpty(r.Phase))

		fmt.Fprintf(iw, "- Target status:\n")
		iw2 := txtfmt.NewIndentWriter(iw)
		statusMap := byRank[r.Rank]
		for _, s := range sortedStatus {
			targets, ok := statusMap[s]
			if !ok {
				continue
			}

			fmt.Fprintf(iw2, "%s: %s\n", s, condensedTargetString(targets))
		}
	}
}

func printSystemCheckPoolInfo(out io.Writer, pools []*control.SystemCheckPoolInfo, verbose bool) {
	if verbose {
		fmt.Fprintln(out, "\nPer-Pool Checker Info:")
	} else {
		fmt.Fprintln(out, "\nUnchecked Pools:")
	}
	for _, pool := range pools {
		iw := txtfmt.NewIndentWriter(out)
		if verbose || pool.Unchecked() {
			fmt.Fprintf(iw, "%+v\n", pool)

			if verbose {
				printSystemCheckTargetInfo(iw, pool)
			}
		}
	}
}

// PrintCheckQueryResp prints the checker results to the console.
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
	checkedCount, uncheckedCount := countResultPools(resp)
	if resp.Status == control.SystemCheckStatusCompleted {
		action = "Checked"
	}
	if checkedCount > 0 {
		fmt.Fprintf(out, "  %s %s\n", action, english.Plural(checkedCount, "pool", ""))
	}

	if len(resp.Pools) > 0 && (verbose || uncheckedCount > 0) {
		pools := make([]*control.SystemCheckPoolInfo, 0, len(resp.Pools))
		for _, pool := range resp.Pools {
			pools = append(pools, pool)
		}
		sort.Slice(pools, func(i, j int) bool {
			return pools[i].UUID < pools[j].UUID
		})
		printSystemCheckPoolInfo(out, pools, verbose)
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
	resolvedTable := []txtfmt.TableRow{}
	actionTable := []txtfmt.TableRow{}
	resolvedHasCont := false
	actionHasCont := false
	for _, report := range resp.Reports {
		tr := txtfmt.TableRow{}

		tr[idLabel] = fmt.Sprintf("0x%x", report.Seq)
		tr[classLabel] = control.SystemCheckFindingClass(report.Class).String()
		tr[poolLabel] = checkerPoolID(report, false)

		if report.ContUuid != "" {
			if report.IsInteractive() {
				actionHasCont = true
			} else {
				resolvedHasCont = true
			}
			tr[contLabel] = checkerContID(report, false)
		}

		if report.IsInteractive() {
			choices := report.RepairChoices()
			for idx, choice := range choices {
				if idx != 0 {
					// choices appear on multiple lines
					actionTable = append(actionTable, tr)
					tr = txtfmt.TableRow{
						idLabel:    "",
						classLabel: "",
						poolLabel:  "",
						contLabel:  "",
						resLabel:   "",
					}
				}
				tr[repairLabel] = fmt.Sprintf("%d: %s", idx, choice.Info)
			}

			actionTable = append(actionTable, tr)
		} else {
			if res := report.Resolution(); res != "" {
				tr[resLabel] = res
				if report.IsDryRun() {
					tr[resLabel] += " (dry run)"
				}
			}
			resolvedTable = append(resolvedTable, tr)
		}
	}

	printReportTable(out, "Resolved", resolvedHasCont, true, resolvedTable)
	printReportTable(out, "Action Required", actionHasCont, false, actionTable)
}

func printReportTable(out io.Writer, title string, hasCont, resolved bool, table []txtfmt.TableRow) {
	if len(table) == 0 {
		return
	}

	cols := []string{idLabel, classLabel, poolLabel}
	if hasCont {
		cols = append(cols, contLabel)
	}

	if resolved {
		cols = append(cols, resLabel)
	} else {
		cols = append(cols, repairLabel)
	}

	tw := txtfmt.NewTableFormatter(cols...)
	fmt.Fprintf(out, "- %s:\n%s\n", title, tw.Format(table))
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
		if ranklist.Rank(report.Rank) != ranklist.NilRank {
			fmt.Fprintf(iw, "Rank:       %d\n", report.Rank)
			fmt.Fprintf(iw, "Target:     %d\n", report.Target)
		}
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
			var dryRunMsg string
			if report.IsDryRun() {
				dryRunMsg = " (dry run - not applied)"
			}
			fmt.Fprintf(iw, "Resolution: %s%s\n\n", res, dryRunMsg)
		} else {
			fmt.Fprintf(iw, "No resolutions available\n\n")
			continue
		}
	}
}
