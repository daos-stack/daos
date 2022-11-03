//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

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

func PrintCheckQueryResp(out io.Writer, resp *control.SystemCheckQueryResp) {
	fmt.Fprintln(out, "DAOS System Checker Info")

	statusMsg := fmt.Sprintf("Current status: %s", resp.Status)
	if resp.Status > control.SystemCheckStatusInit && resp.Status < control.SystemCheckStatusCompleted {
		statusMsg += fmt.Sprintf(" (started at: %s)", resp.StartTime)
	}
	fmt.Fprintf(out, "  %s\n", statusMsg)
	fmt.Fprintf(out, "  Current phase: %s (%s)\n", resp.ScanPhase, resp.ScanPhase.Description())

	if resp.ScanPhase >= control.SystemCheckScanPhasePoolMembership && resp.ScanPhase < control.SystemCheckScanPhaseDone {
		fmt.Fprintf(out, "  Checking %d pools\n", len(resp.Pools))
		if len(resp.Pools) > 0 {
			fmt.Fprintln(out, "\nPer-Pool Checker Info:")
			for _, pool := range resp.Pools {
				// FIXME: Gross debug output for now.
				fmt.Fprintf(out, "  %+v\n", pool)
			}
			fmt.Fprintln(out)
		}
	}

	if len(resp.Reports) == 0 {
		fmt.Fprintln(out, "No reports to display")
		return
	}

	iw := txtfmt.NewIndentWriter(out)
	fmt.Fprintln(out, "Inconsistency Reports:")
	for _, report := range resp.Reports {
		cls := control.SystemCheckFindingClass(report.Class)
		fmt.Fprintf(iw, "0x%x %s: %s\n", report.Seq, cls, report.Msg)
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
